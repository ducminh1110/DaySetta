#include "daysetta/sdk.hpp"

#include <algorithm>
#include <atomic>
#include <bit>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <limits>
#include <poll.h>
#include <span>
#include <string>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace ds::sdk {
namespace {

constexpr int kReplyTimeoutMs = 5000;
constexpr std::uint32_t kMaxDimension = 16384;
constexpr std::uint64_t kMaxPixelBytes = 1ull << 30;

Code code_from_raw(std::uint32_t raw) {
    if (raw <= static_cast<std::uint32_t>(Code::timeout)) return static_cast<Code>(raw);
    return Code::protocol;
}

std::string join_path(const std::string& dir, const char* leaf) {
    std::filesystem::path path(dir);
    path /= leaf;
    return path.string();
}

Result<proto::ErrorPayload> read_error_payload(const proto::Message& reply) {
    proto::Reader reader(reply.payload);
    std::uint32_t raw = reader.get_u32();
    std::string detail = reader.get_string(65536);
    if (!reader.ok() || !reader.empty()) {
        return Result<proto::ErrorPayload>::fail(Code::protocol,
                                                 "malformed error reply payload");
    }
    proto::ErrorPayload error;
    error.code = code_from_raw(raw);
    if (raw > static_cast<std::uint32_t>(Code::timeout)) {
        error.detail = "unknown server error code " + std::to_string(raw);
        if (!detail.empty()) error.detail += ": " + detail;
    } else if (detail.empty()) {
        error.detail = "server error";
    } else {
        error.detail = std::move(detail);
    }
    return Result<proto::ErrorPayload>::ok(std::move(error));
}

Result<void> expect_empty(const proto::Message& reply, const char* what) {
    if (!reply.payload.empty()) {
        return Result<void>::fail(Code::protocol,
                                  std::string(what) + " returned a non-empty payload");
    }
    return Result<void>::ok();
}

Result<ipc::Fd> make_memfd(std::string_view name, std::size_t bytes) {
    std::string label(name);
    int fd = ::memfd_create(label.c_str(), MFD_CLOEXEC);
    if (fd < 0) {
        static std::atomic<std::uint64_t> counter{0};
        std::string shm_name = "/ds-sdk-" + std::to_string(::getpid()) + "-" +
                               std::to_string(counter.fetch_add(1));
        fd = ::shm_open(shm_name.c_str(), O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
        if (fd >= 0) ::shm_unlink(shm_name.c_str());
    }
    if (fd < 0) {
        return Result<ipc::Fd>::fail(Code::io,
                                     std::string("cannot create shared memory: ") +
                                         std::strerror(errno));
    }
    if (bytes > static_cast<std::size_t>(std::numeric_limits<off_t>::max())) {
        ::close(fd);
        return Result<ipc::Fd>::fail(Code::too_large, "shared memory size is too large");
    }
    if (::ftruncate(fd, static_cast<off_t>(bytes)) != 0) {
        int saved = errno;
        ::close(fd);
        return Result<ipc::Fd>::fail(Code::io,
                                     std::string("cannot size shared memory: ") +
                                         std::strerror(saved));
    }
    return Result<ipc::Fd>::ok(ipc::Fd(fd));
}

Result<void> write_all(int fd, const std::uint8_t* data, std::size_t bytes) {
    std::size_t offset = 0;
    while (offset < bytes) {
        ssize_t written = ::pwrite(fd, data + offset, bytes - offset,
                                   static_cast<off_t>(offset));
        if (written < 0) {
            if (errno == EINTR) continue;
            return Result<void>::fail(Code::io,
                                      std::string("surface write failed: ") +
                                          std::strerror(errno));
        }
        if (written == 0) {
            return Result<void>::fail(Code::io, "surface write made no progress");
        }
        offset += static_cast<std::size_t>(written);
    }
    return Result<void>::ok();
}

bool valid_commit_format(proto::PixelFormat format) {
    return format == proto::PixelFormat::bgra8888 ||
           format == proto::PixelFormat::rgba8888;
}

}  // namespace

Client::Client(Client&& other) noexcept
    : channel_(std::move(other.channel_)),
      hello_(other.hello_),
      sequence_(other.sequence_) {}

Client& Client::operator=(Client&& other) noexcept {
    if (this != &other) {
        channel_ = std::move(other.channel_);
        hello_ = other.hello_;
        sequence_ = other.sequence_;
        // A moved client starts without the old subscription; poll_event()
        // subscribes again on demand.
        events_ = event::Ring{};
    }
    return *this;
}

Client::~Client() = default;

Result<Client> Client::connect(const std::string& runtime_dir, std::string_view app_name,
                               proto::Role role, std::string_view service) {
    if (runtime_dir.empty()) {
        return Result<Client>::fail(Code::invalid, "runtime directory is empty");
    }
    if (app_name.size() > 255) {
        return Result<Client>::fail(Code::invalid,
                                    "application name exceeds 255 bytes");
    }
    if (service.empty()) {
        return Result<Client>::fail(Code::invalid, "service name is empty");
    }
    if (role != proto::Role::app && role != proto::Role::control &&
        role != proto::Role::test) {
        return Result<Client>::fail(Code::invalid, "invalid client role");
    }

    std::error_code ec;
    std::filesystem::path root(runtime_dir);
    if (!std::filesystem::exists(root, ec)) {
        if (ec) {
            return Result<Client>::fail(Code::io,
                                        "cannot inspect runtime directory " +
                                            runtime_dir + ": " + ec.message());
        }
        return Result<Client>::fail(Code::not_found,
                                    "runtime directory does not exist: " + runtime_dir);
    }
    if (!std::filesystem::is_directory(root, ec) || ec) {
        return Result<Client>::fail(Code::invalid,
                                    "runtime path is not a directory: " + runtime_dir);
    }

    const std::string broker_path = join_path(runtime_dir, "broker.sock");
    auto broker = ipc::Channel::connect(broker_path);
    if (!broker) {
        return Result<Client>::fail(broker.error().code,
                                    "cannot connect to broker " + broker_path + ": " +
                                        broker.error().message);
    }

    broker_proto::Request lookup;
    lookup.op = broker_proto::Op::lookup_service;
    lookup.name = std::string(service);
    auto sent = broker_proto::send_request(broker.value(), lookup);
    if (!sent) {
        return Result<Client>::fail(sent.error().code,
                                    "cannot send broker lookup: " +
                                        sent.error().message);
    }
    auto looked_up = broker_proto::receive_response(broker.value());
    if (!looked_up) {
        return Result<Client>::fail(looked_up.error().code,
                                    "broker lookup failed: " +
                                        looked_up.error().message);
    }
    const broker_proto::Response& response = looked_up.value();
    if (response.code != Code::ok) {
        std::string detail = response.detail.empty() ? std::string("lookup failed")
                                                     : response.detail;
        return Result<Client>::fail(response.code, "broker lookup: " + detail);
    }
    if (response.path.empty() ||
        !std::filesystem::path(response.path).is_absolute()) {
        return Result<Client>::fail(Code::protocol,
                                    "broker returned a non-absolute service path");
    }
    if (response.token == 0) {
        return Result<Client>::fail(Code::protocol,
                                    "broker returned a zero service token");
    }

    auto channel = ipc::Channel::connect(response.path);
    if (!channel) {
        return Result<Client>::fail(channel.error().code,
                                    "cannot connect to service " + response.path +
                                        ": " + channel.error().message);
    }

    proto::Writer hello_writer;
    hello_writer.put_u32(1);  // client version
    hello_writer.put_u32(static_cast<std::uint32_t>(role));
    hello_writer.put_string(app_name);
    hello_writer.put_u64(response.token);

    proto::Message hello_request =
        proto::make_request(proto::Op::hello, 0, hello_writer.take());
    auto hello_sent = proto::send_message(channel.value(), hello_request);
    if (!hello_sent) {
        return Result<Client>::fail(hello_sent.error().code,
                                    "cannot send hello: " + hello_sent.error().message);
    }
    auto hello_reply = proto::receive_message(channel.value(), kReplyTimeoutMs);
    if (!hello_reply) {
        Code code = hello_reply.error().code;
        std::string message = hello_reply.error().message;
        if (code == Code::timeout) message = "timed out waiting for hello reply";
        return Result<Client>::fail(code, message);
    }
    const proto::Message& reply = hello_reply.value();
    if (!reply.is_reply() || reply.header.op != static_cast<std::uint32_t>(proto::Op::hello)) {
        return Result<Client>::fail(Code::protocol,
                                    "hello reply has the wrong framing or opcode");
    }
    if (reply.is_error()) {
        auto error = read_error_payload(reply);
        if (!error) return Result<Client>::fail(error.error().code, error.error().message);
        return Result<Client>::fail(error.value().code,
                                    "hello rejected: " + error.value().detail);
    }
    if (!reply.fds.empty()) {
        return Result<Client>::fail(Code::protocol,
                                    "hello reply carried unexpected descriptors");
    }
    if (reply.payload.size() != 24) {
        return Result<Client>::fail(Code::protocol,
                                    "hello reply payload is not 24 bytes");
    }
    proto::Reader hello_reader(reply.payload);
    HelloInfo info;
    info.server_version = hello_reader.get_u32();
    info.session_id = hello_reader.get_u32();
    info.capabilities = hello_reader.get_u64();
    info.max_message = hello_reader.get_u32();
    info.max_windows = hello_reader.get_u32();
    if (!hello_reader.ok() || !hello_reader.empty()) {
        return Result<Client>::fail(Code::protocol, "hello reply payload is malformed");
    }
    if (info.server_version != 1) {
        return Result<Client>::fail(Code::unsupported,
                                    "server protocol version " +
                                        std::to_string(info.server_version) +
                                        " is not supported");
    }

    Client client;
    client.channel_ = std::move(channel.value());
    client.hello_ = info;
    client.sequence_ = 1;
    return Result<Client>::ok(std::move(client));
}

Result<proto::Message> Client::call(proto::Op op, std::vector<std::uint8_t> payload,
                                    std::vector<ipc::Fd> fds) {
    if (!channel_.valid()) {
        return Result<proto::Message>::fail(Code::state, "client is not connected");
    }

    const std::uint64_t sequence = sequence_++;
    proto::Message request =
        proto::make_request(op, sequence, std::move(payload), std::move(fds));
    auto sent = proto::send_message(channel_, request);
    if (!sent) {
        return Result<proto::Message>::fail(sent.error().code,
                                            "cannot send request: " +
                                                sent.error().message);
    }

    auto reply = proto::receive_message(channel_, kReplyTimeoutMs);
    if (!reply) {
        Code code = reply.error().code;
        std::string message = reply.error().message;
        if (code == Code::timeout) {
            message = "timed out after " + std::to_string(kReplyTimeoutMs) +
                      " ms waiting for a reply";
        }
        return Result<proto::Message>::fail(code, message);
    }

    const proto::Message& message = reply.value();
    if (!message.is_reply()) {
        return Result<proto::Message>::fail(Code::protocol,
                                            "server sent a request instead of a reply");
    }
    if (message.header.op != static_cast<std::uint32_t>(op)) {
        return Result<proto::Message>::fail(Code::protocol,
                                            "reply opcode does not match the request");
    }
    if (message.header.sequence != sequence) {
        return Result<proto::Message>::fail(Code::protocol,
                                            "reply sequence does not match the request");
    }
    if (message.is_error()) {
        auto error = read_error_payload(message);
        if (!error) {
            return Result<proto::Message>::fail(error.error().code,
                                                error.error().message);
        }
        return Result<proto::Message>::fail(error.value().code, error.value().detail);
    }

    std::size_t expected_fds = 0;
    if (op == proto::Op::subscribe_events || op == proto::Op::capture) expected_fds = 1;
    if (message.fds.size() != expected_fds) {
        return Result<proto::Message>::fail(
            Code::protocol, "reply carried " + std::to_string(message.fds.size()) +
                                " descriptors, expected " +
                                std::to_string(expected_fds));
    }
    return reply;
}

Result<std::uint64_t> Client::create_window(const proto::Rect& frame, std::int32_t level,
                                            std::string_view title) {
    proto::Writer writer;
    writer.put_rect(frame);
    writer.put_i32(level);
    writer.put_string(title);
    auto reply = call(proto::Op::create_window, writer.take());
    if (!reply) return Result<std::uint64_t>::fail(reply.error().code, reply.error().message);
    if (reply.value().payload.size() != 8) {
        return Result<std::uint64_t>::fail(Code::protocol,
                                           "create_window reply payload is not 8 bytes");
    }
    proto::Reader reader(reply.value().payload);
    std::uint64_t id = reader.get_u64();
    if (!reader.ok() || !reader.empty()) {
        return Result<std::uint64_t>::fail(Code::protocol,
                                           "create_window reply payload is malformed");
    }
    return Result<std::uint64_t>::ok(id);
}

Result<void> Client::destroy_window(std::uint64_t window) {
    proto::Writer writer;
    writer.put_u64(window);
    auto reply = call(proto::Op::destroy_window, writer.take());
    if (!reply) return Result<void>::fail(reply.error().code, reply.error().message);
    return expect_empty(reply.value(), "destroy_window");
}

Result<void> Client::set_window_frame(std::uint64_t window, const proto::Rect& frame) {
    proto::Writer writer;
    writer.put_u64(window);
    writer.put_rect(frame);
    auto reply = call(proto::Op::set_window_frame, writer.take());
    if (!reply) return Result<void>::fail(reply.error().code, reply.error().message);
    return expect_empty(reply.value(), "set_window_frame");
}

Result<void> Client::set_window_visible(std::uint64_t window, bool visible) {
    proto::Writer writer;
    writer.put_u64(window);
    writer.put_u8(visible ? 1 : 0);
    auto reply = call(proto::Op::set_window_visible, writer.take());
    if (!reply) return Result<void>::fail(reply.error().code, reply.error().message);
    return expect_empty(reply.value(), "set_window_visible");
}

Result<void> Client::set_window_opacity(std::uint64_t window, float opacity) {
    proto::Writer writer;
    writer.put_u64(window);
    writer.put_u32(std::bit_cast<std::uint32_t>(opacity));
    auto reply = call(proto::Op::set_window_opacity, writer.take());
    if (!reply) return Result<void>::fail(reply.error().code, reply.error().message);
    return expect_empty(reply.value(), "set_window_opacity");
}

Result<void> Client::set_window_title(std::uint64_t window, std::string_view title) {
    proto::Writer writer;
    writer.put_u64(window);
    writer.put_string(title);
    auto reply = call(proto::Op::set_window_title, writer.take());
    if (!reply) return Result<void>::fail(reply.error().code, reply.error().message);
    return expect_empty(reply.value(), "set_window_title");
}

Result<void> Client::order_window(std::uint64_t window, proto::WindowOrder order) {
    proto::Writer writer;
    writer.put_u64(window);
    writer.put_u32(static_cast<std::uint32_t>(order));
    auto reply = call(proto::Op::order_window, writer.take());
    if (!reply) return Result<void>::fail(reply.error().code, reply.error().message);
    return expect_empty(reply.value(), "order_window");
}

Result<std::uint64_t> Client::commit_surface(std::uint64_t window, std::uint32_t width,
                                             std::uint32_t height, std::uint32_t stride,
                                             proto::PixelFormat format,
                                             std::span<const std::uint8_t> pixels,
                                             const std::vector<proto::Rect>& damage) {
    if (width == 0 || height == 0 || width > kMaxDimension || height > kMaxDimension) {
        return Result<std::uint64_t>::fail(Code::invalid,
                                           "commit dimensions are out of range");
    }
    if (stride % 4 != 0 || static_cast<std::uint64_t>(stride) <
                               static_cast<std::uint64_t>(width) * 4) {
        return Result<std::uint64_t>::fail(Code::invalid,
                                           "commit stride is smaller than width * 4");
    }
    if (!valid_commit_format(format)) {
        return Result<std::uint64_t>::fail(Code::invalid,
                                           "commit pixel format is not supported");
    }
    if (damage.size() > 64) {
        return Result<std::uint64_t>::fail(Code::invalid,
                                           "commit carries more than 64 damage rectangles");
    }

    const std::uint64_t total64 = static_cast<std::uint64_t>(stride) * height;
    if (total64 > kMaxPixelBytes) {
        return Result<std::uint64_t>::fail(Code::too_large,
                                           "commit surface exceeds the size limit");
    }
    const std::size_t total = static_cast<std::size_t>(total64);
    if (pixels.size() < total) {
        return Result<std::uint64_t>::fail(
            Code::invalid, "pixel buffer is smaller than stride * height");
    }

    auto fd = make_memfd("ds-surface", total);
    if (!fd) return Result<std::uint64_t>::fail(fd.error().code, fd.error().message);
    auto written = write_all(fd.value().get(), pixels.data(), total);
    if (!written) {
        return Result<std::uint64_t>::fail(written.error().code, written.error().message);
    }

    proto::Writer writer;
    writer.put_u64(window);
    writer.put_u32(width);
    writer.put_u32(height);
    writer.put_u32(stride);
    writer.put_u32(static_cast<std::uint32_t>(format));
    writer.put_u64(sequence_);  // client surface generation
    writer.put_u32(static_cast<std::uint32_t>(damage.size()));
    for (const proto::Rect& rect : damage) writer.put_rect(rect);

    std::vector<ipc::Fd> descriptors;
    descriptors.emplace_back(fd.value().release());
    auto reply = call(proto::Op::commit_surface, writer.take(), std::move(descriptors));
    if (!reply) return Result<std::uint64_t>::fail(reply.error().code, reply.error().message);
    if (reply.value().payload.size() != 12) {
        return Result<std::uint64_t>::fail(Code::protocol,
                                           "commit_surface reply payload is not 12 bytes");
    }
    proto::Reader reader(reply.value().payload);
    std::uint64_t commit_sequence = reader.get_u64();
    reader.get_u32();  // accepted damage count
    if (!reader.ok() || !reader.empty()) {
        return Result<std::uint64_t>::fail(Code::protocol,
                                           "commit_surface reply payload is malformed");
    }
    return Result<std::uint64_t>::ok(commit_sequence);
}

Result<void> Client::subscribe_events() {
    if (events_.valid()) return Result<void>::ok();

    auto reply = call(proto::Op::subscribe_events, {});
    if (!reply) return Result<void>::fail(reply.error().code, reply.error().message);
    if (reply.value().payload.size() != 8) {
        return Result<void>::fail(Code::protocol,
                                  "subscribe_events reply payload is not 8 bytes");
    }
    proto::Reader reader(reply.value().payload);
    std::uint32_t capacity = reader.get_u32();
    std::uint32_t event_size = reader.get_u32();
    if (!reader.ok() || !reader.empty()) {
        return Result<void>::fail(Code::protocol,
                                  "subscribe_events reply payload is malformed");
    }
    if (capacity == 0 || event_size != event::kEventSize) {
        return Result<void>::fail(Code::protocol,
                                  "subscribe_events reply has an invalid event ring");
    }
    if (reply.value().fds.size() != 1) {
        return Result<void>::fail(Code::protocol,
                                  "subscribe_events reply did not carry one descriptor");
    }
    int ring_fd = reply.value().fds.front().get();
    auto ring = event::Ring::map(ring_fd, true);
    if (!ring) return Result<void>::fail(ring.error().code, ring.error().message);
    if (ring.value().capacity() != capacity) {
        return Result<void>::fail(Code::protocol,
                                  "event ring capacity does not match the reply");
    }

    events_ = std::move(ring.value());
    return Result<void>::ok();
}

Result<event::Event> Client::poll_event(int timeout_ms) {
    if (!events_.valid()) {
        auto subscribed = subscribe_events();
        if (!subscribed) {
            return Result<event::Event>::fail(subscribed.error().code,
                                              subscribed.error().message);
        }
    }

    using Clock = std::chrono::steady_clock;
    const Clock::time_point start = Clock::now();
    Clock::time_point deadline = Clock::time_point::max();
    if (timeout_ms >= 0) {
        deadline = start + std::chrono::milliseconds(timeout_ms);
    }

    for (;;) {
        event::Event event;
        if (events_.pop(&event)) return Result<event::Event>::ok(event);

        Clock::time_point now = Clock::now();
        if (now >= deadline) {
            return Result<event::Event>::fail(Code::timeout,
                                              "timed out waiting for an event");
        }

        int sleep_ms = 5;
        if (timeout_ms >= 0) {
            auto remaining =
                std::chrono::ceil<std::chrono::milliseconds>(deadline - now).count();
            if (remaining < 1) remaining = 1;
            if (remaining < sleep_ms) sleep_ms = static_cast<int>(remaining);
        }

        // M1 has no wakeup descriptor for the ring: the server writes shared memory
        // only.  Check the socket without blocking to reject unsolicited messages,
        // then sleep briefly and re-check the ring.
        auto ready = ipc::wait_readable(channel_.fd(), 0);
        if (ready) {
            return Result<event::Event>::fail(
                Code::protocol,
                "server sent unsolicited data while no reply was pending");
        }
        if (ready.error().code != Code::timeout) {
            return Result<event::Event>::fail(ready.error().code,
                                              ready.error().message);
        }
        ::poll(nullptr, 0, sleep_ms);
    }
}

Result<std::string> Client::scene_dump() {
    auto reply = call(proto::Op::scene_dump, {});
    if (!reply) return Result<std::string>::fail(reply.error().code, reply.error().message);
    proto::Reader reader(reply.value().payload);
    std::string json = reader.get_string(ipc::kMaxMessage);
    if (!reader.ok() || !reader.empty()) {
        return Result<std::string>::fail(Code::protocol,
                                         "scene_dump reply payload is malformed");
    }
    return Result<std::string>::ok(std::move(json));
}

Result<std::uint64_t> Client::inject_input(const event::Event& input) {
    proto::Writer writer;
    writer.put_u32(static_cast<std::uint32_t>(input.type));
    writer.put_u32(input.flags);
    writer.put_u64(input.timestamp_ns);
    writer.put_u64(input.target_window);
    writer.put_i32(input.a);
    writer.put_i32(input.b);
    writer.put_i32(input.c);
    writer.put_i32(input.d);

    auto reply = call(proto::Op::inject_input, writer.take());
    if (!reply) return Result<std::uint64_t>::fail(reply.error().code, reply.error().message);
    if (reply.value().payload.size() != 8) {
        return Result<std::uint64_t>::fail(Code::protocol,
                                           "inject_input reply payload is not 8 bytes");
    }
    proto::Reader reader(reply.value().payload);
    std::uint64_t target = reader.get_u64();
    if (!reader.ok() || !reader.empty()) {
        return Result<std::uint64_t>::fail(Code::protocol,
                                           "inject_input reply payload is malformed");
    }
    return Result<std::uint64_t>::ok(target);
}

Result<render::Frame> Client::capture(std::uint32_t width, std::uint32_t height) {
    if (width == 0 || height == 0 || width > kMaxDimension || height > kMaxDimension) {
        return Result<render::Frame>::fail(Code::invalid,
                                           "capture dimensions are out of range");
    }

    proto::Writer writer;
    writer.put_u32(width);
    writer.put_u32(height);
    auto reply = call(proto::Op::capture, writer.take());
    if (!reply) return Result<render::Frame>::fail(reply.error().code, reply.error().message);
    if (reply.value().payload.size() != 16) {
        return Result<render::Frame>::fail(Code::protocol,
                                           "capture reply payload is not 16 bytes");
    }
    proto::Reader reader(reply.value().payload);
    render::Frame frame;
    frame.width = reader.get_u32();
    frame.height = reader.get_u32();
    frame.stride = reader.get_u32();
    std::uint32_t format = reader.get_u32();
    if (!reader.ok() || !reader.empty()) {
        return Result<render::Frame>::fail(Code::protocol,
                                           "capture reply payload is malformed");
    }
    if (frame.width == 0 || frame.height == 0 || frame.width > kMaxDimension ||
        frame.height > kMaxDimension) {
        return Result<render::Frame>::fail(Code::protocol,
                                           "capture reply has invalid dimensions");
    }
    if (format != static_cast<std::uint32_t>(proto::PixelFormat::bgra8888)) {
        return Result<render::Frame>::fail(Code::protocol,
                                           "capture reply is not BGRA8888");
    }
    if (frame.stride % 4 != 0 ||
        static_cast<std::uint64_t>(frame.stride) <
            static_cast<std::uint64_t>(frame.width) * 4) {
        return Result<render::Frame>::fail(Code::protocol,
                                           "capture reply has an invalid stride");
    }
    const std::uint64_t total64 =
        static_cast<std::uint64_t>(frame.stride) * frame.height;
    if (total64 > kMaxPixelBytes) {
        return Result<render::Frame>::fail(Code::protocol,
                                           "capture frame exceeds the size limit");
    }
    const std::size_t total = static_cast<std::size_t>(total64);
    if (reply.value().fds.size() != 1) {
        return Result<render::Frame>::fail(Code::protocol,
                                           "capture reply did not carry one descriptor");
    }
    int fd = reply.value().fds.front().get();
    struct stat st{};
    if (::fstat(fd, &st) != 0) {
        return Result<render::Frame>::fail(Code::io,
                                           std::string("cannot stat capture frame: ") +
                                               std::strerror(errno));
    }
    if (st.st_size < 0 || static_cast<std::uint64_t>(st.st_size) < total64) {
        return Result<render::Frame>::fail(Code::protocol,
                                           "capture descriptor is too small");
    }

    void* mapping = ::mmap(nullptr, total, PROT_READ, MAP_PRIVATE, fd, 0);
    if (mapping == MAP_FAILED) {
        return Result<render::Frame>::fail(Code::io,
                                           std::string("cannot map capture frame: ") +
                                               std::strerror(errno));
    }
    frame.pixels.resize(total);
    std::memcpy(frame.pixels.data(), mapping, total);
    ::munmap(mapping, total);
    return Result<render::Frame>::ok(std::move(frame));
}

Result<std::uint64_t> Client::ping(std::uint64_t token) {
    proto::Writer writer;
    writer.put_u64(token);
    auto reply = call(proto::Op::ping, writer.take());
    if (!reply) return Result<std::uint64_t>::fail(reply.error().code, reply.error().message);
    if (reply.value().payload.size() != 8) {
        return Result<std::uint64_t>::fail(Code::protocol,
                                           "ping reply payload is not 8 bytes");
    }
    proto::Reader reader(reply.value().payload);
    std::uint64_t echoed = reader.get_u64();
    if (!reader.ok() || !reader.empty()) {
        return Result<std::uint64_t>::fail(Code::protocol,
                                           "ping reply payload is malformed");
    }
    return Result<std::uint64_t>::ok(echoed);
}

}  // namespace ds::sdk
