#include "daysetta/sdk.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace ds::sdk::broker_proto {
namespace {

constexpr std::size_t kPrefixSize = 16;
constexpr std::size_t kMaxString = 65536;
constexpr std::size_t kMaxDatagram = 64 * 1024;

void put_u32(std::span<std::uint8_t> out, std::uint32_t v) {
    out[0] = static_cast<std::uint8_t>(v);
    out[1] = static_cast<std::uint8_t>(v >> 8);
    out[2] = static_cast<std::uint8_t>(v >> 16);
    out[3] = static_cast<std::uint8_t>(v >> 24);
}

std::uint32_t get_u32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) |
           (static_cast<std::uint32_t>(p[3]) << 24);
}

std::vector<std::uint8_t> encode_prefix(std::uint32_t op, std::uint32_t status) {
    std::vector<std::uint8_t> bytes(kPrefixSize);
    put_u32(std::span<std::uint8_t>(bytes).subspan(0, 4), kMagic);
    put_u32(std::span<std::uint8_t>(bytes).subspan(4, 4), kVersion);
    put_u32(std::span<std::uint8_t>(bytes).subspan(8, 4), op);
    put_u32(std::span<std::uint8_t>(bytes).subspan(12, 4), status);
    return bytes;
}

// Reads the shared prefix and all five trailing fields.  A zero op is written for
// responses because Response does not carry the request op; the field is not
// meaningful on a broker reply.
bool decode_common(std::span<const std::uint8_t> bytes, std::uint32_t* status) {
    if (bytes.size() < kPrefixSize) return false;
    if (get_u32(bytes.data() + 0) != kMagic) return false;
    if (get_u32(bytes.data() + 4) != kVersion) return false;
    *status = get_u32(bytes.data() + 12);
    return true;
}

}  // namespace

std::vector<std::uint8_t> encode_request(const Request& request) {
    std::vector<std::uint8_t> bytes = encode_prefix(static_cast<std::uint32_t>(request.op), 0);
    proto::Writer writer;
    writer.put_string("");
    writer.put_string(request.name);
    writer.put_string(request.path);
    writer.put_u64(0);
    writer.put_string("");
    const auto& tail = writer.bytes();
    bytes.insert(bytes.end(), tail.begin(), tail.end());
    return bytes;
}

Request decode_request(std::span<const std::uint8_t> bytes, bool* ok) {
    auto fail = [&]() {
        if (ok != nullptr) *ok = false;
        return Request{};
    };

    std::uint32_t status = 0;
    if (!decode_common(bytes, &status)) return fail();
    proto::Reader reader(bytes.subspan(kPrefixSize));
    Request request;
    request.op = static_cast<Op>(get_u32(bytes.data() + 8));
    reader.get_string(kMaxString);  // detail is unused on requests
    request.name = reader.get_string(kMaxString);
    request.path = reader.get_string(kMaxString);
    reader.get_u64();  // token is unused on requests
    reader.get_string(kMaxString);  // json is unused on requests
    if (!reader.ok() || !reader.empty()) return fail();
    if (ok != nullptr) *ok = true;
    return request;
}

std::vector<std::uint8_t> encode_response(const Response& response) {
    std::vector<std::uint8_t> bytes =
        encode_prefix(0, static_cast<std::uint32_t>(response.code));
    proto::Writer writer;
    writer.put_string(response.detail);
    writer.put_string(response.name);
    writer.put_string(response.path);
    writer.put_u64(response.token);
    writer.put_string(response.json);
    const auto& tail = writer.bytes();
    bytes.insert(bytes.end(), tail.begin(), tail.end());
    return bytes;
}

Response decode_response(std::span<const std::uint8_t> bytes, bool* ok) {
    auto fail = [&]() {
        if (ok != nullptr) *ok = false;
        return Response{};
    };

    std::uint32_t status = 0;
    if (!decode_common(bytes, &status)) return fail();
    if (status > static_cast<std::uint32_t>(Code::timeout)) return fail();

    proto::Reader reader(bytes.subspan(kPrefixSize));
    Response response;
    response.code = static_cast<Code>(status);
    response.detail = reader.get_string(kMaxString);
    response.name = reader.get_string(kMaxString);
    response.path = reader.get_string(kMaxString);
    response.token = reader.get_u64();
    response.json = reader.get_string(kMaxString);
    if (!reader.ok() || !reader.empty()) return fail();
    if (ok != nullptr) *ok = true;
    return response;
}

Result<void> send_request(ipc::Channel& channel, const Request& request) {
    std::vector<std::uint8_t> bytes = encode_request(request);
    if (bytes.size() > kMaxDatagram) {
        return Result<void>::fail(Code::too_large, "broker request exceeds 64 KiB");
    }
    auto sent = channel.send(bytes);
    if (!sent) return Result<void>::fail(sent.error().code, sent.error().message);
    return Result<void>::ok();
}

Result<Response> receive_response(ipc::Channel& channel) {
    std::vector<ipc::Fd> fds;
    auto datagram = channel.receive(&fds);
    if (!datagram) {
        return Result<Response>::fail(datagram.error().code, datagram.error().message);
    }
    if (!fds.empty()) {
        return Result<Response>::fail(Code::protocol,
                                      "broker response carried unexpected descriptors");
    }
    if (datagram->size() > kMaxDatagram) {
        return Result<Response>::fail(Code::too_large,
                                      "broker response exceeds 64 KiB");
    }
    bool ok = false;
    Response response = decode_response(*datagram, &ok);
    if (!ok) {
        return Result<Response>::fail(Code::protocol, "malformed broker response");
    }
    return Result<Response>::ok(std::move(response));
}

}  // namespace ds::sdk::broker_proto
