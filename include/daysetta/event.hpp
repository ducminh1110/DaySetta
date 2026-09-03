// Input events and the per-client event ring.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

#include "daysetta/result.hpp"

namespace ds::event {

constexpr std::uint32_t kRingMagic = 0x56455344;  // "DSEV"
constexpr std::uint32_t kRingVersion = 1;
constexpr std::size_t kEventSize = 40;
constexpr std::size_t kRingHeaderSize = 32;
constexpr std::uint32_t kDefaultCapacity = 256;  // must be a power of two

enum class Type : std::uint32_t {
    pointer_move = 1,
    button_down = 2,
    button_up = 3,
    key_down = 4,
    key_up = 5,
    scroll = 6,
    window_resize = 7,
    window_close = 8,
    focus = 9,
    note = 10,
};

struct Event {
    Type type = Type::pointer_move;
    std::uint32_t flags = 0;
    std::uint64_t timestamp_ns = 0;
    std::uint64_t target_window = 0;
    std::int32_t a = 0;  // x, keycode, note id, ...
    std::int32_t b = 0;  // y, button, ...
    std::int32_t c = 0;  // delta, ...
    std::int32_t d = 0;
};

static_assert(sizeof(Event) == kEventSize, "event record must stay 40 bytes");
static_assert(alignof(Event) == 8, "event record must stay 8-byte aligned");

// The ring is a single-producer/single-consumer shared mapping:
//   header (32 bytes) followed by `capacity` event slots.
struct RingHeader {
    std::uint32_t magic;
    std::uint32_t version;
    std::uint32_t capacity;
    std::uint32_t head;      // consumer index
    std::uint32_t tail;      // producer index
    std::uint32_t dropped;
    std::uint32_t reserved0;
    std::uint32_t reserved1;
};

static_assert(sizeof(RingHeader) == kRingHeaderSize, "ring header must stay 32 bytes");

std::size_t ring_bytes(std::uint32_t capacity);

// Creates a sealed memfd of ring_bytes(capacity), mapped for the caller.
Result<int> create_ring_fd(std::uint32_t capacity);

// A mapped ring.  The mapping is owned by this object.
class Ring {
public:
    Ring() = default;
    Ring(Ring&& other) noexcept;
    Ring& operator=(Ring&& other) noexcept;
    ~Ring();

    static Result<Ring> map(int fd, bool writable);
    static Result<Ring> create(std::uint32_t capacity, bool writable);

    bool valid() const { return header_ != nullptr; }
    RingHeader* header() const { return header_; }
    Event* slots() const;
    std::uint32_t capacity() const { return header_ ? header_->capacity : 0; }

    // Producer side.  Returns false when the ring is full.
    bool push(const Event& event);
    // Consumer side.  Returns false when empty.
    bool pop(Event* event);

    // Shared facts for diagnostics.
    std::uint32_t dropped() const { return header_ ? __atomic_load_n(&header_->dropped, __ATOMIC_RELAXED) : 0; }

private:
    void reset();

    std::uint8_t* mapping_ = nullptr;
    std::size_t bytes_ = 0;
    RingHeader* header_ = nullptr;
    bool writable_ = false;
};

const char* type_name(Type type);

}  // namespace ds::event
