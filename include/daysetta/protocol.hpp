// ds/1 message header, typed payload reader/writer, and request/reply helpers.
#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "daysetta/ipc.hpp"
#include "daysetta/result.hpp"

namespace ds::proto {

constexpr std::uint32_t kMagic = 0x314d5344;  // "DSM1" little-endian
constexpr std::uint32_t kVersion = 1;
constexpr std::uint32_t kMaxPayload = ipc::kMaxMessage - 32;
constexpr std::size_t kHeaderSize = 32;

enum Flags : std::uint32_t {
    kReply = 1u << 0,
    kError = 1u << 1,
    kOneWay = 1u << 2,
    kHandleBearing = 1u << 3,
};

enum class Op : std::uint32_t {
    hello = 0x0001,
    create_window = 0x0010,
    destroy_window = 0x0011,
    set_window_frame = 0x0012,
    set_window_visible = 0x0013,
    order_window = 0x0014,
    set_window_opacity = 0x0015,
    set_window_title = 0x0016,
    commit_surface = 0x0020,
    subscribe_events = 0x0030,
    ack_events = 0x0031,
    scene_dump = 0x0040,
    inject_input = 0x0041,
    capture = 0x0042,
    ping = 0x00f0,
    status = 0x00f1,
};

enum class Role : std::uint32_t {
    app = 0,
    control = 1,
    test = 2,
};

enum class WindowOrder : std::uint32_t {
    raise = 0,
    lower = 1,
};

enum class PixelFormat : std::uint32_t {
    bgra8888 = 1,
    rgba8888 = 2,
};

struct Rect {
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t w = 0;
    std::int32_t h = 0;
};

struct Header {
    std::uint32_t magic = kMagic;
    std::uint32_t size = kHeaderSize;
    std::uint32_t op = 0;
    std::uint32_t flags = 0;
    std::uint64_t sequence = 0;
    std::uint16_t handle_count = 0;
    std::uint16_t reserved = 0;
    std::uint32_t payload_len = 0;
};

static_assert(sizeof(Header) == kHeaderSize, "ds/1 header must stay 32 bytes");

std::array<std::uint8_t, kHeaderSize> encode_header(const Header& header);
Header decode_header(std::span<const std::uint8_t> bytes);
bool validate_header(const Header& header, std::size_t actual_size, std::string* why);

struct Message {
    Header header;
    std::vector<std::uint8_t> payload;
    std::vector<ipc::Fd> fds;

    Op op() const { return static_cast<Op>(header.op); }
    bool is_reply() const { return (header.flags & kReply) != 0; }
    bool is_error() const { return (header.flags & kError) != 0; }
};

Result<Message> receive_message(ipc::Channel& channel, int timeout_ms = -1);
Result<void> send_message(ipc::Channel& channel, const Message& message);

Message make_request(Op op, std::uint64_t sequence,
                     std::vector<std::uint8_t> payload = {},
                     std::vector<ipc::Fd> fds = {});
Message make_reply(const Message& request, std::vector<std::uint8_t> payload = {},
                   std::vector<ipc::Fd> fds = {});
Message make_error(const Message& request, Code code, std::string_view detail = {});

struct ErrorPayload {
    Code code = Code::protocol;
    std::string detail;
};

// Bounds-checked little-endian payload writer.
class Writer {
public:
    void put_u8(std::uint8_t v);
    void put_u16(std::uint16_t v);
    void put_u32(std::uint32_t v);
    void put_i32(std::int32_t v);
    void put_u64(std::uint64_t v);
    void put_string(std::string_view v);  // u32 length + bytes, no NUL
    void put_rect(const Rect& r);
    void put_bytes(std::span<const std::uint8_t> bytes);

    std::vector<std::uint8_t> take() { return std::move(buffer_); }
    const std::vector<std::uint8_t>& bytes() const { return buffer_; }
    std::size_t size() const { return buffer_.size(); }

private:
    std::vector<std::uint8_t> buffer_;
};

// Bounds-checked reader.  A failed read poisons the reader; callers check ok() once.
class Reader {
public:
    explicit Reader(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}

    std::uint8_t get_u8();
    std::uint16_t get_u16();
    std::uint32_t get_u32();
    std::int32_t get_i32();
    std::uint64_t get_u64();
    std::string get_string(std::size_t max_length = 4096);
    Rect get_rect();
    std::span<const std::uint8_t> get_bytes(std::size_t count);

    bool ok() const { return ok_; }
    bool empty() const { return pos_ == bytes_.size(); }
    std::size_t remaining() const { return bytes_.size() - pos_; }
    const std::string& error() const { return error_; }

private:
    bool need(std::size_t count);

    std::span<const std::uint8_t> bytes_;
    std::size_t pos_ = 0;
    bool ok_ = true;
    std::string error_;
};

}  // namespace ds::proto
