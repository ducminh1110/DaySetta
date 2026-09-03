#include "daysetta/protocol.hpp"

#include <algorithm>
#include <cstring>
#include <limits>

namespace ds::proto {
namespace {

void put32(std::uint8_t* p, std::uint32_t v) {
    p[0] = static_cast<std::uint8_t>(v);
    p[1] = static_cast<std::uint8_t>(v >> 8);
    p[2] = static_cast<std::uint8_t>(v >> 16);
    p[3] = static_cast<std::uint8_t>(v >> 24);
}

void put64(std::uint8_t* p, std::uint64_t v) {
    put32(p, static_cast<std::uint32_t>(v));
    put32(p + 4, static_cast<std::uint32_t>(v >> 32));
}

std::uint32_t get32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) |
           (static_cast<std::uint32_t>(p[3]) << 24);
}

std::uint64_t get64(const std::uint8_t* p) {
    return static_cast<std::uint64_t>(get32(p)) |
           (static_cast<std::uint64_t>(get32(p + 4)) << 32);
}

}  // namespace

std::array<std::uint8_t, kHeaderSize> encode_header(const Header& header) {
    std::array<std::uint8_t, kHeaderSize> out{};
    put32(out.data() + 0, header.magic);
    put32(out.data() + 4, header.size);
    put32(out.data() + 8, header.op);
    put32(out.data() + 12, header.flags);
    put64(out.data() + 16, header.sequence);
    out[24] = static_cast<std::uint8_t>(header.handle_count);
    out[25] = static_cast<std::uint8_t>(header.handle_count >> 8);
    out[26] = static_cast<std::uint8_t>(header.reserved);
    out[27] = static_cast<std::uint8_t>(header.reserved >> 8);
    put32(out.data() + 28, header.payload_len);
    return out;
}

Header decode_header(std::span<const std::uint8_t> bytes) {
    Header header;
    if (bytes.size() < kHeaderSize) return Header{0, 0, 0, 0, 0, 0, 0, 0};
    header.magic = get32(bytes.data() + 0);
    header.size = get32(bytes.data() + 4);
    header.op = get32(bytes.data() + 8);
    header.flags = get32(bytes.data() + 12);
    header.sequence = get64(bytes.data() + 16);
    header.handle_count = static_cast<std::uint16_t>(bytes[24] | (bytes[25] << 8));
    header.reserved = static_cast<std::uint16_t>(bytes[26] | (bytes[27] << 8));
    header.payload_len = get32(bytes.data() + 28);
    return header;
}

bool validate_header(const Header& header, std::size_t actual_size, std::string* why) {
    auto fail = [&](const char* reason) {
        if (why != nullptr) *why = reason;
        return false;
    };
    if (header.magic != kMagic) return fail("bad magic");
    if (header.size < kHeaderSize) return fail("size below header");
    if (header.size != actual_size) return fail("size does not match datagram");
    if (header.payload_len != header.size - kHeaderSize) {
        return fail("payload_len does not match size");
    }
    if (header.handle_count > ipc::kMaxFds) return fail("handle_count above limit");
    if (header.reserved != 0) return fail("reserved field is not zero");
    constexpr std::uint32_t known = kReply | kError | kOneWay | kHandleBearing;
    if ((header.flags & ~known) != 0) return fail("unknown flags");
    if ((header.flags & kError) != 0 && (header.flags & kReply) == 0) {
        return fail("error without reply");
    }
    if (why != nullptr) why->clear();
    return true;
}

Result<Message> receive_message(ipc::Channel& channel, int timeout_ms) {
    if (timeout_ms >= 0) {
        auto ready = ipc::wait_readable(channel.fd(), timeout_ms);
        if (!ready) return Result<Message>::fail(ready.error().code, ready.error().message);
    }
    std::vector<ipc::Fd> fds;
    auto bytes = channel.receive(&fds);
    if (!bytes) return Result<Message>::fail(bytes.error().code, bytes.error().message);

    Header header = decode_header(*bytes);
    std::string why;
    if (!validate_header(header, bytes->size(), &why)) {
        return Result<Message>::fail(Code::protocol, why);
    }
    if (header.handle_count != fds.size()) {
        return Result<Message>::fail(Code::protocol,
                                     "header handle_count does not match descriptors");
    }

    Message message;
    message.header = header;
    message.payload.assign(bytes->begin() + kHeaderSize, bytes->end());
    message.fds = std::move(fds);
    return Result<Message>::ok(std::move(message));
}

Result<void> send_message(ipc::Channel& channel, const Message& message) {
    if (message.payload.size() > kMaxPayload) {
        return Result<void>::fail(Code::too_large, "payload exceeds ds/1 limit");
    }
    if (message.fds.size() > ipc::kMaxFds) {
        return Result<void>::fail(Code::too_large, "too many descriptors");
    }
    Header header = message.header;
    header.magic = kMagic;
    header.size = static_cast<std::uint32_t>(kHeaderSize + message.payload.size());
    header.payload_len = static_cast<std::uint32_t>(message.payload.size());
    header.handle_count = static_cast<std::uint16_t>(message.fds.size());
    header.reserved = 0;
    if (!message.fds.empty()) header.flags |= kHandleBearing;

    auto encoded = encode_header(header);
    std::vector<std::uint8_t> datagram;
    datagram.reserve(kHeaderSize + message.payload.size());
    datagram.insert(datagram.end(), encoded.begin(), encoded.end());
    datagram.insert(datagram.end(), message.payload.begin(), message.payload.end());

    std::vector<int> raw;
    raw.reserve(message.fds.size());
    for (const auto& fd : message.fds) raw.push_back(fd.get());
    return channel.send(datagram, raw);
}

Message make_request(Op op, std::uint64_t sequence, std::vector<std::uint8_t> payload,
                     std::vector<ipc::Fd> fds) {
    Message message;
    message.header.magic = kMagic;
    message.header.op = static_cast<std::uint32_t>(op);
    message.header.flags = 0;
    message.header.sequence = sequence;
    message.header.size = static_cast<std::uint32_t>(kHeaderSize + payload.size());
    message.header.payload_len = static_cast<std::uint32_t>(payload.size());
    message.header.handle_count = static_cast<std::uint16_t>(fds.size());
    if (!fds.empty()) message.header.flags |= kHandleBearing;
    message.payload = std::move(payload);
    message.fds = std::move(fds);
    return message;
}

Message make_reply(const Message& request, std::vector<std::uint8_t> payload,
                   std::vector<ipc::Fd> fds) {
    Message message;
    message.header.magic = kMagic;
    message.header.op = request.header.op;
    message.header.flags = kReply;
    if (!fds.empty()) message.header.flags |= kHandleBearing;
    message.header.sequence = request.header.sequence;
    message.header.size = static_cast<std::uint32_t>(kHeaderSize + payload.size());
    message.header.payload_len = static_cast<std::uint32_t>(payload.size());
    message.header.handle_count = static_cast<std::uint16_t>(fds.size());
    message.payload = std::move(payload);
    message.fds = std::move(fds);
    return message;
}

Message make_error(const Message& request, Code code, std::string_view detail) {
    Writer writer;
    writer.put_u32(static_cast<std::uint32_t>(code));
    writer.put_string(detail);
    Message message = make_reply(request, writer.take());
    message.header.flags |= kError;
    return message;
}

void Writer::put_u8(std::uint8_t v) { buffer_.push_back(v); }

void Writer::put_u16(std::uint16_t v) {
    buffer_.push_back(static_cast<std::uint8_t>(v));
    buffer_.push_back(static_cast<std::uint8_t>(v >> 8));
}

void Writer::put_u32(std::uint32_t v) {
    std::uint8_t bytes[4];
    put32(bytes, v);
    buffer_.insert(buffer_.end(), bytes, bytes + 4);
}

void Writer::put_i32(std::int32_t v) { put_u32(static_cast<std::uint32_t>(v)); }

void Writer::put_u64(std::uint64_t v) {
    std::uint8_t bytes[8];
    put64(bytes, v);
    buffer_.insert(buffer_.end(), bytes, bytes + 8);
}

void Writer::put_string(std::string_view v) {
    put_u32(static_cast<std::uint32_t>(v.size()));
    buffer_.insert(buffer_.end(), v.begin(), v.end());
}

void Writer::put_rect(const Rect& r) {
    put_i32(r.x);
    put_i32(r.y);
    put_i32(r.w);
    put_i32(r.h);
}

void Writer::put_bytes(std::span<const std::uint8_t> bytes) {
    buffer_.insert(buffer_.end(), bytes.begin(), bytes.end());
}

bool Reader::need(std::size_t count) {
    if (!ok_) return false;
    if (count > bytes_.size() - pos_) {
        ok_ = false;
        error_ = "truncated payload";
        return false;
    }
    return true;
}

std::uint8_t Reader::get_u8() {
    if (!need(1)) return 0;
    return bytes_[pos_++];
}

std::uint16_t Reader::get_u16() {
    if (!need(2)) return 0;
    std::uint16_t v = static_cast<std::uint16_t>(bytes_[pos_] | (bytes_[pos_ + 1] << 8));
    pos_ += 2;
    return v;
}

std::uint32_t Reader::get_u32() {
    if (!need(4)) return 0;
    std::uint32_t v = get32(bytes_.data() + pos_);
    pos_ += 4;
    return v;
}

std::int32_t Reader::get_i32() { return static_cast<std::int32_t>(get_u32()); }

std::uint64_t Reader::get_u64() {
    if (!need(8)) return 0;
    std::uint64_t v = get64(bytes_.data() + pos_);
    pos_ += 8;
    return v;
}

std::string Reader::get_string(std::size_t max_length) {
    std::uint32_t length = get_u32();
    if (!ok_) return {};
    if (length > max_length) {
        ok_ = false;
        error_ = "string exceeds limit";
        return {};
    }
    auto bytes = get_bytes(length);
    return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

Rect Reader::get_rect() {
    Rect r;
    r.x = get_i32();
    r.y = get_i32();
    r.w = get_i32();
    r.h = get_i32();
    return r;
}

std::span<const std::uint8_t> Reader::get_bytes(std::size_t count) {
    if (!need(count)) return {};
    auto span = bytes_.subspan(pos_, count);
    pos_ += count;
    return span;
}

}  // namespace ds::proto
