// Native client SDK: broker lookup, ds/1 calls and event-ring consumption.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include "daysetta/event.hpp"
#include "daysetta/ipc.hpp"
#include "daysetta/protocol.hpp"
#include "daysetta/render.hpp"
#include "daysetta/result.hpp"

namespace ds::sdk {

struct HelloInfo {
    std::uint32_t server_version = 0;
    std::uint32_t session_id = 0;
    std::uint64_t capabilities = 0;
    std::uint32_t max_message = 0;
    std::uint32_t max_windows = 0;
};

class Client {
public:
    Client() = default;
    Client(Client&&) noexcept;
    Client& operator=(Client&&) noexcept;
    ~Client();

    // Looks the service up through the broker, connects, and performs the ds/1
    // handshake.  `runtime_dir` is the directory that holds broker.sock.
    static Result<Client> connect(const std::string& runtime_dir,
                                  std::string_view app_name,
                                  proto::Role role = proto::Role::app,
                                  std::string_view service = "org.daysetta.server");

    const HelloInfo& hello() const { return hello_; }
    std::uint32_t session_id() const { return hello_.session_id; }

    Result<std::uint64_t> create_window(const proto::Rect& frame, std::int32_t level,
                                        std::string_view title);
    Result<void> destroy_window(std::uint64_t window);
    Result<void> set_window_frame(std::uint64_t window, const proto::Rect& frame);
    Result<void> set_window_visible(std::uint64_t window, bool visible);
    Result<void> set_window_opacity(std::uint64_t window, float opacity);
    Result<void> set_window_title(std::uint64_t window, std::string_view title);
    Result<void> order_window(std::uint64_t window, proto::WindowOrder order);

    // Copies the pixels into a memfd and passes it to the server.  The pixels must be
    // BGRA8888 (or RGBA8888 when `format` says so), stride bytes per row.
    Result<std::uint64_t> commit_surface(std::uint64_t window, std::uint32_t width,
                                         std::uint32_t height, std::uint32_t stride,
                                         proto::PixelFormat format,
                                         std::span<const std::uint8_t> pixels,
                                         const std::vector<proto::Rect>& damage = {});

    // Lazily subscribes and returns the next event, waiting up to timeout_ms.
    Result<event::Event> poll_event(int timeout_ms);
    Result<void> subscribe_events();

    Result<std::string> scene_dump();
    Result<std::uint64_t> inject_input(const event::Event& event);
    Result<render::Frame> capture(std::uint32_t width, std::uint32_t height);
    Result<std::uint64_t> ping(std::uint64_t token);

    int fd() const { return channel_.fd(); }

private:
    Result<proto::Message> call(proto::Op op, std::vector<std::uint8_t> payload,
                                std::vector<ipc::Fd> fds = {});

    ipc::Channel channel_;
    HelloInfo hello_;
    std::uint64_t sequence_ = 1;
    event::Ring events_;
    ipc::Fd wake_;  // eventfd: the server writes it after each ring publish
};

// Broker protocol used by the SDK and by daysetta-ctl.  Exposed for tests.
namespace broker_proto {
constexpr std::uint32_t kMagic = 0x314b5344;  // "DSK1"
constexpr std::uint32_t kVersion = 1;

enum class Op : std::uint32_t {
    register_service = 1,
    lookup_service = 2,
    unregister_service = 3,
    list_services = 4,
    ping = 5,
};

struct Request {
    Op op = Op::ping;
    std::string name;
    std::string path;
};

struct Response {
    Code code = Code::ok;
    std::string detail;
    std::string name;
    std::string path;
    std::uint64_t token = 0;
    std::string json;
};

Result<void> send_request(ipc::Channel& channel, const Request& request);
Result<Response> receive_response(ipc::Channel& channel);
std::vector<std::uint8_t> encode_request(const Request& request);
Request decode_request(std::span<const std::uint8_t> bytes, bool* ok);
std::vector<std::uint8_t> encode_response(const Response& response);
Response decode_response(std::span<const std::uint8_t> bytes, bool* ok);
}  // namespace broker_proto

}  // namespace ds::sdk
