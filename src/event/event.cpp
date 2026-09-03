#include "daysetta/event.hpp"

#include <atomic>
#include <cerrno>
#include <cstring>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace ds::event {
namespace {

bool power_of_two(std::uint32_t v) { return v >= 2 && (v & (v - 1)) == 0; }

}  // namespace

std::size_t ring_bytes(std::uint32_t capacity) {
    return kRingHeaderSize + static_cast<std::size_t>(capacity) * kEventSize;
}

Result<int> create_ring_fd(std::uint32_t capacity) {
    if (!power_of_two(capacity)) {
        return Result<int>::fail(Code::invalid, "ring capacity must be a power of two >= 2");
    }
    int fd = ::memfd_create("ds-event-ring", MFD_CLOEXEC);
    if (fd < 0) return Result<int>::fail(Code::io, std::strerror(errno));

    std::size_t bytes = ring_bytes(capacity);
    if (::ftruncate(fd, static_cast<off_t>(bytes)) != 0) {
        int saved = errno;
        ::close(fd);
        return Result<int>::fail(Code::io, std::strerror(saved));
    }

    RingHeader header{};
    header.magic = kRingMagic;
    header.version = kRingVersion;
    header.capacity = capacity;
    if (::pwrite(fd, &header, sizeof(header), 0) != static_cast<ssize_t>(sizeof(header))) {
        int saved = errno;
        ::close(fd);
        return Result<int>::fail(Code::io, std::strerror(saved));
    }
    return Result<int>::ok(fd);
}

Ring::Ring(Ring&& other) noexcept
    : mapping_(other.mapping_), bytes_(other.bytes_), header_(other.header_),
      writable_(other.writable_) {
    // Transfer ownership without unmapping: reset() would munmap the mapping now
    // owned by this object.
    other.mapping_ = nullptr;
    other.bytes_ = 0;
    other.header_ = nullptr;
    other.writable_ = false;
}

Ring& Ring::operator=(Ring&& other) noexcept {
    if (this != &other) {
        reset();
        mapping_ = other.mapping_;
        bytes_ = other.bytes_;
        header_ = other.header_;
        writable_ = other.writable_;
        other.mapping_ = nullptr;
        other.bytes_ = 0;
        other.header_ = nullptr;
        other.writable_ = false;
    }
    return *this;
}

Ring::~Ring() { reset(); }

void Ring::reset() {
    if (mapping_ != nullptr) ::munmap(mapping_, bytes_);
    mapping_ = nullptr;
    bytes_ = 0;
    header_ = nullptr;
    writable_ = false;
}

Result<Ring> Ring::map(int fd, bool writable) {
    struct stat st{};
    if (::fstat(fd, &st) != 0) return Result<Ring>::fail(Code::io, std::strerror(errno));
    if (st.st_size < static_cast<off_t>(kRingHeaderSize)) {
        return Result<Ring>::fail(Code::invalid, "ring file too small");
    }

    int prot = PROT_READ | (writable ? PROT_WRITE : 0);
    void* mapping = ::mmap(nullptr, static_cast<std::size_t>(st.st_size), prot, MAP_SHARED, fd, 0);
    if (mapping == MAP_FAILED) {
        return Result<Ring>::fail(Code::io, std::strerror(errno));
    }

    Ring ring;
    ring.mapping_ = static_cast<std::uint8_t*>(mapping);
    ring.bytes_ = static_cast<std::size_t>(st.st_size);
    ring.header_ = reinterpret_cast<RingHeader*>(mapping);
    ring.writable_ = writable;

    if (ring.header_->magic != kRingMagic || ring.header_->version != kRingVersion ||
        !power_of_two(ring.header_->capacity) ||
        ring.bytes_ < ring_bytes(ring.header_->capacity)) {
        ring.reset();
        return Result<Ring>::fail(Code::protocol, "bad event ring header");
    }
    return Result<Ring>::ok(std::move(ring));
}

Result<Ring> Ring::create(std::uint32_t capacity, bool writable) {
    auto fd = create_ring_fd(capacity);
    if (!fd) return Result<Ring>::fail(fd.error().code, fd.error().message);
    auto ring = map(fd.value(), writable);
    ::close(fd.value());
    return ring;
}

Event* Ring::slots() const {
    return reinterpret_cast<Event*>(mapping_ + kRingHeaderSize);
}

bool Ring::push(const Event& event) {
    if (header_ == nullptr || !writable_) return false;
    std::uint32_t capacity = header_->capacity;
    std::uint32_t tail = __atomic_load_n(&header_->tail, __ATOMIC_RELAXED);
    std::uint32_t head = __atomic_load_n(&header_->head, __ATOMIC_ACQUIRE);
    std::uint32_t next = (tail + 1) & (capacity - 1);
    if (next == head) {
        __atomic_add_fetch(&header_->dropped, 1, __ATOMIC_RELAXED);
        return false;
    }
    std::memcpy(&slots()[tail], &event, sizeof(Event));
    __atomic_store_n(&header_->tail, next, __ATOMIC_RELEASE);
    return true;
}

bool Ring::pop(Event* event) {
    if (header_ == nullptr) return false;
    std::uint32_t capacity = header_->capacity;
    std::uint32_t head = __atomic_load_n(&header_->head, __ATOMIC_RELAXED);
    std::uint32_t tail = __atomic_load_n(&header_->tail, __ATOMIC_ACQUIRE);
    if (head == tail) return false;
    std::memcpy(event, &slots()[head], sizeof(Event));
    __atomic_store_n(&header_->head, (head + 1) & (capacity - 1), __ATOMIC_RELEASE);
    return true;
}

const char* type_name(Type type) {
    switch (type) {
        case Type::pointer_move: return "pointer_move";
        case Type::button_down: return "button_down";
        case Type::button_up: return "button_up";
        case Type::key_down: return "key_down";
        case Type::key_up: return "key_up";
        case Type::scroll: return "scroll";
        case Type::window_resize: return "window_resize";
        case Type::window_close: return "window_close";
        case Type::focus: return "focus";
        case Type::note: return "note";
    }
    return "unknown";
}

}  // namespace ds::event
