#include "daysetta/daemon.hpp"

#include <algorithm>
#include <bit>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <map>
#include <memory>
#include <poll.h>
#include <sstream>
#include <string>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

#include "daysetta/event.hpp"
#include "daysetta/ipc.hpp"
#include "daysetta/protocol.hpp"
#include "daysetta/render.hpp"
#include "daysetta/scene.hpp"
#include "daysetta/sdk.hpp"

namespace ds {
namespace {

volatile std::sig_atomic_t g_stop = 0;

void on_signal(int) { g_stop = 1; }

std::string default_runtime_dir() {
    if (const char* xdg = std::getenv("XDG_RUNTIME_DIR"); xdg != nullptr && *xdg != '\0') {
        return std::string(xdg) + "/daysetta";
    }
    return "/tmp/daysetta-" + std::to_string(::geteuid());
}

Result<int> create_memfd(const char* name, std::size_t size) {
    int fd = ::memfd_create(name, MFD_CLOEXEC);
    if (fd < 0) return Result<int>::fail(Code::io, std::strerror(errno));
    if (::ftruncate(fd, static_cast<off_t>(size)) != 0) {
        int saved = errno;
        ::close(fd);
        return Result<int>::fail(Code::io, std::strerror(saved));
    }
    return Result<int>::ok(fd);
}

Result<void> read_fd_exact(int fd, std::span<std::uint8_t> out) {
    std::size_t done = 0;
    while (done < out.size()) {
        ssize_t n = ::pread(fd, out.data() + done, out.size() - done, static_cast<off_t>(done));
        if (n < 0) {
            if (errno == EINTR) continue;
            return Result<void>::fail(Code::io, std::strerror(errno));
        }
        if (n == 0) return Result<void>::fail(Code::protocol, "surface memfd is short");
        done += static_cast<std::size_t>(n);
    }
    return Result<void>::ok();
}

std::string json_escape(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 8);
    for (unsigned char c : value) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[7];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out.push_back(static_cast<char>(c));
                }
        }
    }
    return out;
}

struct Client {
    scene::ClientId id = 0;
    ipc::Channel channel;
    ipc::PeerCredentials peer;
    bool hello = false;
    proto::Role role = proto::Role::app;
    std::string app_name;
    bool subscribed = false;
    event::Ring ring;

    std::uint64_t capabilities() const {
        switch (role) {
            case proto::Role::app: return 0x07;       // windows, surfaces, events
            case proto::Role::control: return 0x1f;   // + capture, input injection
            case proto::Role::test: return 0x1f;
        }
        return 0x07;
    }
};

class Server {
public:
    explicit Server(ServerConfig config) : config_(std::move(config)) {
        if (config_.runtime_dir.empty()) config_.runtime_dir = default_runtime_dir();
        if (config_.broker_path.empty()) {
            config_.broker_path = config_.runtime_dir + "/broker.sock";
        }
        if (config_.server_socket.empty()) {
            config_.server_socket = config_.runtime_dir + "/server.sock";
        }
    }

    Result<void> run() {
        auto dir = ipc::make_private_dir(config_.runtime_dir);
        if (!dir) return dir;
        auto listen = ipc::Channel::listen(config_.server_socket);
        if (!listen) return Result<void>::fail(listen.error().code, listen.error().message);
        listener_ = std::move(listen.value());

        std::signal(SIGINT, on_signal);
        std::signal(SIGTERM, on_signal);
        std::signal(SIGPIPE, SIG_IGN);

        auto registered = register_with_broker();
        if (!registered) return registered;

        std::fprintf(stderr,
                     "daysetta-server: service=%s socket=%s size=%ux%u broker=%s\n",
                     config_.service_name.c_str(), config_.server_socket.c_str(),
                     config_.width, config_.height, config_.broker_path.c_str());

        auto result = loop();
        unregister_from_broker();
        listener_.close();
        ipc::unlink_if_exists(config_.server_socket);
        return result;
    }

private:
    Result<void> register_with_broker() {
        auto broker = ipc::Channel::connect(config_.broker_path);
        if (!broker) {
            return Result<void>::fail(
                broker.error().code,
                "cannot connect to broker at " + config_.broker_path + ": " +
                    broker.error().message);
        }
        sdk::broker_proto::Request request;
        request.op = sdk::broker_proto::Op::register_service;
        request.name = config_.service_name;
        request.path = config_.server_socket;
        auto sent = sdk::broker_proto::send_request(broker.value(), request);
        if (!sent) return sent;
        auto response = sdk::broker_proto::receive_response(broker.value());
        if (!response) return Result<void>::fail(response.error().code, response.error().message);
        if (response.value().code != Code::ok) {
            return Result<void>::fail(response.value().code,
                                      "broker refused registration: " + response.value().detail);
        }
        if (response.value().token == 0) {
            return Result<void>::fail(Code::protocol, "broker returned a zero token");
        }
        token_ = response.value().token;
        return Result<void>::ok();
    }

    void unregister_from_broker() {
        if (token_ == 0) return;
        auto broker = ipc::Channel::connect(config_.broker_path);
        if (!broker) return;
        sdk::broker_proto::Request request;
        request.op = sdk::broker_proto::Op::unregister_service;
        request.name = config_.service_name;
        (void)sdk::broker_proto::send_request(broker.value(), request);
        (void)sdk::broker_proto::receive_response(broker.value());
    }

    Result<void> loop() {
        while (!g_stop) {
            std::vector<pollfd> fds;
            fds.push_back(pollfd{listener_.fd(), POLLIN, 0});
            std::vector<int> order;
            for (auto& [fd, client] : clients_) {
                (void)client;
                order.push_back(fd);
                fds.push_back(pollfd{fd, POLLIN, 0});
            }
            int rc = ::poll(fds.data(), fds.size(), static_cast<int>(config_.tick_ms));
            if (rc < 0) {
                if (errno == EINTR) continue;
                return Result<void>::fail(Code::io, std::strerror(errno));
            }
            if ((fds[0].revents & POLLIN) != 0) accept_clients();

            for (std::size_t i = 0; i < order.size(); ++i) {
                short revents = fds[i + 1].revents;
                if ((revents & (POLLERR | POLLHUP | POLLNVAL)) != 0 &&
                    (revents & POLLIN) == 0) {
                    drop_client(order[i]);
                } else if ((revents & POLLIN) != 0) {
                    service_client(order[i]);
                }
            }

            if (dirty_) {
                auto presented = present_frame();
                if (!presented) return presented;
                dirty_ = false;
            }
            if (config_.max_frames != 0 && frames_ >= config_.max_frames) break;
        }
        return Result<void>::ok();
    }

    void accept_clients() {
        // The listener is blocking.  poll() reported a pending connection, so accept
        // exactly one; the next loop iteration accepts the next if several queued.
        ipc::PeerCredentials peer;
        auto accepted = listener_.accept(&peer);
        if (!accepted) return;
        if (peer.uid != static_cast<std::uint32_t>(::geteuid())) {
            std::fprintf(stderr, "daysetta-server: rejecting peer %s\n",
                         ipc::peer_credentials_string(peer).c_str());
            return;
        }
        int fd = accepted.value().fd();
        Client client;
        client.id = scene_.add_client();
        client.channel = std::move(accepted.value());
        client.peer = peer;
        clients_.emplace(fd, std::move(client));
        std::fprintf(stderr, "daysetta-server: client %d connected (%s)\n", fd,
                     ipc::peer_credentials_string(peer).c_str());
    }

    void drop_client(int fd) {
        auto it = clients_.find(fd);
        if (it == clients_.end()) return;
        scene_.remove_client(it->second.id);
        clients_.erase(it);
        dirty_ = true;
    }

    void service_client(int fd) {
        auto it = clients_.find(fd);
        if (it == clients_.end()) return;
        for (;;) {
            auto message = proto::receive_message(it->second.channel, 0);
            if (!message) {
                if (message.error().code == Code::timeout) return;
                drop_client(fd);
                return;
            }
            auto reply = handle_message(it->second, message.value());
            if (!reply) {
                drop_client(fd);
                return;
            }
            auto sent = proto::send_message(it->second.channel, reply.value());
            if (!sent) {
                drop_client(fd);
                return;
            }
            if ((message.value().header.flags & proto::kOneWay) != 0) {
                // Replies are still sent in M1; one-way is reserved.
            }
        }
    }

    Result<proto::Message> handle_message(Client& client, const proto::Message& request) {
        if (request.is_reply()) {
            return Result<proto::Message>::ok(
                proto::make_error(request, Code::protocol, "server received a reply"));
        }
        if (!client.hello && request.op() != proto::Op::hello &&
            request.op() != proto::Op::ping) {
            return Result<proto::Message>::ok(
                proto::make_error(request, Code::state, "hello required"));
        }

        switch (request.op()) {
            case proto::Op::hello: return handle_hello(client, request);
            case proto::Op::create_window: return handle_create_window(client, request);
            case proto::Op::destroy_window: return handle_destroy_window(client, request);
            case proto::Op::set_window_frame: return handle_set_frame(client, request);
            case proto::Op::set_window_visible: return handle_set_visible(client, request);
            case proto::Op::order_window: return handle_order(client, request);
            case proto::Op::set_window_opacity: return handle_set_opacity(client, request);
            case proto::Op::set_window_title: return handle_set_title(client, request);
            case proto::Op::commit_surface: return handle_commit(client, request);
            case proto::Op::subscribe_events: return handle_subscribe(client, request);
            case proto::Op::ack_events: return handle_ack(client, request);
            case proto::Op::scene_dump: return handle_scene_dump(client, request);
            case proto::Op::inject_input: return handle_inject(client, request);
            case proto::Op::capture: return handle_capture(client, request);
            case proto::Op::ping: return handle_ping(client, request);
            case proto::Op::status: return handle_status(client, request);
        }
        return Result<proto::Message>::ok(
            proto::make_error(request, Code::unsupported, "unknown ds/1 operation"));
    }

    Result<proto::Message> handle_hello(Client& client, const proto::Message& request) {
        proto::Reader reader(request.payload);
        std::uint32_t version = reader.get_u32();
        std::uint32_t role = reader.get_u32();
        std::string app = reader.get_string(255);
        std::uint64_t token = reader.get_u64();
        if (!reader.ok() || !reader.empty()) {
            return Result<proto::Message>::ok(
                proto::make_error(request, Code::invalid, "malformed hello"));
        }
        if (version != proto::kVersion) {
            return Result<proto::Message>::ok(
                proto::make_error(request, Code::unsupported, "unsupported ds/1 version"));
        }
        if (token != token_) {
            return Result<proto::Message>::ok(
                proto::make_error(request, Code::denied, "bad broker token"));
        }
        if (role > static_cast<std::uint32_t>(proto::Role::test)) {
            return Result<proto::Message>::ok(
                proto::make_error(request, Code::invalid, "unknown role"));
        }
        if (client.hello) {
            return Result<proto::Message>::ok(
                proto::make_error(request, Code::state, "hello already completed"));
        }
        client.hello = true;
        client.role = static_cast<proto::Role>(role);
        client.app_name = std::move(app);

        proto::Writer writer;
        writer.put_u32(proto::kVersion);
        writer.put_u32(static_cast<std::uint32_t>(client.id));
        writer.put_u64(client.capabilities());
        writer.put_u32(static_cast<std::uint32_t>(ipc::kMaxMessage));
        writer.put_u32(1024);
        return Result<proto::Message>::ok(proto::make_reply(request, writer.take()));
    }

    bool owns(const Client& client, scene::WindowId id) {
        auto found = scene_.find(id);
        return found && found.value().client == client.id;
    }

    Result<proto::Message> handle_create_window(Client& client,
                                                const proto::Message& request) {
        proto::Reader reader(request.payload);
        proto::Rect frame = reader.get_rect();
        std::int32_t level = reader.get_i32();
        std::string title = reader.get_string(4096);
        if (!reader.ok() || !reader.empty()) {
            return Result<proto::Message>::ok(
                proto::make_error(request, Code::invalid, "malformed create_window"));
        }
        auto id = scene_.create_window(client.id, frame, level, std::move(title));
        if (!id) {
            return Result<proto::Message>::ok(
                proto::make_error(request, id.error().code, id.error().message));
        }
        dirty_ = true;
        proto::Writer writer;
        writer.put_u64(id.value());
        return Result<proto::Message>::ok(proto::make_reply(request, writer.take()));
    }

    Result<proto::Message> handle_destroy_window(Client& client,
                                                 const proto::Message& request) {
        proto::Reader reader(request.payload);
        auto id = reader.get_u64();
        if (!reader.ok() || !reader.empty()) {
            return Result<proto::Message>::ok(
                proto::make_error(request, Code::invalid, "malformed destroy_window"));
        }
        if (!owns(client, id)) {
            return Result<proto::Message>::ok(
                proto::make_error(request, Code::denied, "window is not owned by this client"));
        }
        auto removed = scene_.destroy_window(id);
        if (!removed) {
            return Result<proto::Message>::ok(
                proto::make_error(request, removed.error().code, removed.error().message));
        }
        dirty_ = true;
        return Result<proto::Message>::ok(proto::make_reply(request));
    }

    Result<proto::Message> handle_set_frame(Client& client, const proto::Message& request) {
        proto::Reader reader(request.payload);
        auto id = reader.get_u64();
        proto::Rect frame = reader.get_rect();
        if (!reader.ok() || !reader.empty()) {
            return Result<proto::Message>::ok(
                proto::make_error(request, Code::invalid, "malformed set_window_frame"));
        }
        if (!owns(client, id)) {
            return Result<proto::Message>::ok(
                proto::make_error(request, Code::denied, "window is not owned by this client"));
        }
        auto changed = scene_.set_frame(id, frame);
        if (!changed) {
            return Result<proto::Message>::ok(
                proto::make_error(request, changed.error().code, changed.error().message));
        }
        dirty_ = true;
        return Result<proto::Message>::ok(proto::make_reply(request));
    }

    Result<proto::Message> handle_set_visible(Client& client, const proto::Message& request) {
        proto::Reader reader(request.payload);
        auto id = reader.get_u64();
        std::uint8_t visible = reader.get_u8();
        if (!reader.ok() || !reader.empty() || visible > 1) {
            return Result<proto::Message>::ok(
                proto::make_error(request, Code::invalid, "malformed set_window_visible"));
        }
        if (!owns(client, id)) {
            return Result<proto::Message>::ok(
                proto::make_error(request, Code::denied, "window is not owned by this client"));
        }
        auto changed = scene_.set_visible(id, visible != 0);
        if (!changed) {
            return Result<proto::Message>::ok(
                proto::make_error(request, changed.error().code, changed.error().message));
        }
        dirty_ = true;
        return Result<proto::Message>::ok(proto::make_reply(request));
    }

    Result<proto::Message> handle_order(Client& client, const proto::Message& request) {
        proto::Reader reader(request.payload);
        auto id = reader.get_u64();
        std::uint32_t order = reader.get_u32();
        if (!reader.ok() || !reader.empty() || order > 1) {
            return Result<proto::Message>::ok(
                proto::make_error(request, Code::invalid, "malformed order_window"));
        }
        if (!owns(client, id)) {
            return Result<proto::Message>::ok(
                proto::make_error(request, Code::denied, "window is not owned by this client"));
        }
        auto changed = scene_.order(id, static_cast<proto::WindowOrder>(order));
        if (!changed) {
            return Result<proto::Message>::ok(
                proto::make_error(request, changed.error().code, changed.error().message));
        }
        dirty_ = true;
        return Result<proto::Message>::ok(proto::make_reply(request));
    }

    Result<proto::Message> handle_set_opacity(Client& client, const proto::Message& request) {
        proto::Reader reader(request.payload);
        auto id = reader.get_u64();
        std::uint32_t bits = reader.get_u32();
        if (!reader.ok() || !reader.empty()) {
            return Result<proto::Message>::ok(
                proto::make_error(request, Code::invalid, "malformed set_window_opacity"));
        }
        if (!owns(client, id)) {
            return Result<proto::Message>::ok(
                proto::make_error(request, Code::denied, "window is not owned by this client"));
        }
        float opacity = std::bit_cast<float>(bits);
        auto changed = scene_.set_opacity(id, opacity);
        if (!changed) {
            return Result<proto::Message>::ok(
                proto::make_error(request, changed.error().code, changed.error().message));
        }
        dirty_ = true;
        return Result<proto::Message>::ok(proto::make_reply(request));
    }

    Result<proto::Message> handle_set_title(Client& client, const proto::Message& request) {
        proto::Reader reader(request.payload);
        auto id = reader.get_u64();
        std::string title = reader.get_string(4096);
        if (!reader.ok() || !reader.empty()) {
            return Result<proto::Message>::ok(
                proto::make_error(request, Code::invalid, "malformed set_window_title"));
        }
        if (!owns(client, id)) {
            return Result<proto::Message>::ok(
                proto::make_error(request, Code::denied, "window is not owned by this client"));
        }
        auto changed = scene_.set_title(id, std::move(title));
        if (!changed) {
            return Result<proto::Message>::ok(
                proto::make_error(request, changed.error().code, changed.error().message));
        }
        return Result<proto::Message>::ok(proto::make_reply(request));
    }

    Result<proto::Message> handle_commit(Client& client, const proto::Message& request) {
        proto::Reader reader(request.payload);
        auto window = reader.get_u64();
        auto width = reader.get_u32();
        auto height = reader.get_u32();
        auto stride = reader.get_u32();
        auto format = reader.get_u32();
        auto surface_sequence = reader.get_u64();
        auto damage_count = reader.get_u32();
        if (!reader.ok() || damage_count > 64) {
            return Result<proto::Message>::ok(
                proto::make_error(request, Code::invalid, "malformed commit_surface"));
        }
        std::vector<proto::Rect> damage;
        damage.reserve(damage_count);
        for (std::uint32_t i = 0; i < damage_count; ++i) damage.push_back(reader.get_rect());
        if (!reader.ok() || !reader.empty() || request.fds.size() != 1) {
            return Result<proto::Message>::ok(
                proto::make_error(request, Code::invalid, "commit_surface needs one fd"));
        }
        if (!owns(client, window)) {
            return Result<proto::Message>::ok(
                proto::make_error(request, Code::denied, "window is not owned by this client"));
        }
        if (width == 0 || height == 0 || width > 16384 || height > 16384 ||
            stride < width * 4 || stride > 65536 ||
            format > static_cast<std::uint32_t>(proto::PixelFormat::rgba8888)) {
            return Result<proto::Message>::ok(
                proto::make_error(request, Code::invalid, "bad surface geometry"));
        }
        std::size_t bytes = static_cast<std::size_t>(stride) * height;
        struct stat st{};
        if (::fstat(request.fds[0].get(), &st) != 0 ||
            static_cast<std::size_t>(st.st_size) < bytes) {
            return Result<proto::Message>::ok(
                proto::make_error(request, Code::invalid, "surface memfd is too small"));
        }
        auto surface = std::make_shared<scene::Surface>();
        surface->id = next_surface_++;
        surface->width = width;
        surface->height = height;
        surface->stride = stride;
        surface->format = static_cast<proto::PixelFormat>(format);
        surface->sequence = surface_sequence;
        surface->damage = std::move(damage);
        surface->pixels.resize(bytes);
        auto copied = read_fd_exact(request.fds[0].get(), surface->pixels);
        if (!copied) {
            return Result<proto::Message>::ok(
                proto::make_error(request, copied.error().code, copied.error().message));
        }
        auto committed = scene_.commit(window, std::move(surface));
        if (!committed) {
            return Result<proto::Message>::ok(
                proto::make_error(request, committed.error().code, committed.error().message));
        }
        dirty_ = true;
        proto::Writer writer;
        writer.put_u64(committed.value());
        writer.put_u32(damage_count);
        return Result<proto::Message>::ok(proto::make_reply(request, writer.take()));
    }

    Result<proto::Message> handle_subscribe(Client& client, const proto::Message& request) {
        if (!request.payload.empty()) {
            return Result<proto::Message>::ok(
                proto::make_error(request, Code::invalid, "subscribe_events takes no payload"));
        }
        if (client.subscribed) {
            return Result<proto::Message>::ok(
                proto::make_error(request, Code::state, "already subscribed"));
        }
        auto fd = event::create_ring_fd(event::kDefaultCapacity);
        if (!fd) {
            return Result<proto::Message>::ok(
                proto::make_error(request, fd.error().code, fd.error().message));
        }
        auto ring = event::Ring::map(fd.value(), true);
        if (!ring) {
            ::close(fd.value());
            return Result<proto::Message>::ok(
                proto::make_error(request, ring.error().code, ring.error().message));
        }
        client.ring = std::move(ring.value());
        client.subscribed = true;
        proto::Writer writer;
        writer.put_u32(event::kDefaultCapacity);
        writer.put_u32(static_cast<std::uint32_t>(event::kEventSize));
        std::vector<ipc::Fd> fds;
        fds.emplace_back(fd.value());
        return Result<proto::Message>::ok(
            proto::make_reply(request, writer.take(), std::move(fds)));
    }

    Result<proto::Message> handle_ack(Client& client, const proto::Message& request) {
        (void)client;
        proto::Reader reader(request.payload);
        auto count = reader.get_u32();
        if (!reader.ok() || !reader.empty()) {
            return Result<proto::Message>::ok(
                proto::make_error(request, Code::invalid, "malformed ack_events"));
        }
        (void)count;
        return Result<proto::Message>::ok(proto::make_reply(request));
    }

    Result<proto::Message> handle_scene_dump(Client& client, const proto::Message& request) {
        if (client.role == proto::Role::app) {
            return Result<proto::Message>::ok(
                proto::make_error(request, Code::denied, "scene_dump needs control role"));
        }
        proto::Writer writer;
        writer.put_string(scene_.dump_json());
        return Result<proto::Message>::ok(proto::make_reply(request, writer.take()));
    }

    void push_to_window(scene::WindowId window, const event::Event& event) {
        auto found = scene_.find(window);
        if (!found) return;
        for (auto& [fd, client] : clients_) {
            (void)fd;
            if (client.id == found.value().client && client.subscribed) {
                event::Event copy = event;
                copy.target_window = window;
                (void)client.ring.push(copy);
                return;
            }
        }
    }

    Result<proto::Message> handle_inject(Client& client, const proto::Message& request) {
        if ((client.capabilities() & 0x10) == 0) {
            return Result<proto::Message>::ok(
                proto::make_error(request, Code::denied, "input injection not permitted"));
        }
        proto::Reader reader(request.payload);
        event::Event event;
        event.type = static_cast<event::Type>(reader.get_u32());
        event.flags = reader.get_u32();
        event.timestamp_ns = reader.get_u64();
        event.target_window = reader.get_u64();
        event.a = reader.get_i32();
        event.b = reader.get_i32();
        event.c = reader.get_i32();
        event.d = reader.get_i32();
        if (!reader.ok() || !reader.empty()) {
            return Result<proto::Message>::ok(
                proto::make_error(request, Code::invalid, "malformed inject_input"));
        }
        std::uint32_t type = static_cast<std::uint32_t>(event.type);
        if (type < 1 || type > 10) {
            return Result<proto::Message>::ok(
                proto::make_error(request, Code::invalid, "unknown event type"));
        }
        scene::WindowId target = event.target_window;
        if (target == 0) target = scene_.hit_test(event.a, event.b);
        if (target != 0) {
            auto found = scene_.find(target);
            if (!found) target = 0;
        }
        event.target_window = target;
        scene_.note_input_event();
        if (target != 0) push_to_window(target, event);
        proto::Writer writer;
        writer.put_u64(target);
        return Result<proto::Message>::ok(proto::make_reply(request, writer.take()));
    }

    Result<proto::Message> handle_capture(Client& client, const proto::Message& request) {
        if ((client.capabilities() & 0x08) == 0) {
            return Result<proto::Message>::ok(
                proto::make_error(request, Code::denied, "capture not permitted"));
        }
        proto::Reader reader(request.payload);
        auto width = reader.get_u32();
        auto height = reader.get_u32();
        if (!reader.ok() || !reader.empty() || width == 0 || height == 0 || width > 16384 ||
            height > 16384) {
            return Result<proto::Message>::ok(
                proto::make_error(request, Code::invalid, "bad capture size"));
        }
        render::CpuCompositor compositor;
        auto frame = compositor.render(scene_.snapshot(), width, height,
                                       render::Color{0, 0, 0, 255});
        if (!frame) {
            return Result<proto::Message>::ok(
                proto::make_error(request, frame.error().code, frame.error().message));
        }
        auto fd = create_memfd("ds-capture", frame.value().pixels.size());
        if (!fd) {
            return Result<proto::Message>::ok(
                proto::make_error(request, fd.error().code, fd.error().message));
        }
        std::size_t done = 0;
        while (done < frame.value().pixels.size()) {
            ssize_t n = ::pwrite(fd.value(), frame.value().pixels.data() + done,
                                 frame.value().pixels.size() - done, static_cast<off_t>(done));
            if (n < 0) {
                if (errno == EINTR) continue;
                int saved = errno;
                ::close(fd.value());
                return Result<proto::Message>::ok(
                    proto::make_error(request, Code::io, std::strerror(saved)));
            }
            done += static_cast<std::size_t>(n);
        }
        proto::Writer writer;
        writer.put_u32(frame.value().width);
        writer.put_u32(frame.value().height);
        writer.put_u32(frame.value().stride);
        writer.put_u32(static_cast<std::uint32_t>(proto::PixelFormat::bgra8888));
        std::vector<ipc::Fd> fds;
        fds.emplace_back(fd.value());
        return Result<proto::Message>::ok(
            proto::make_reply(request, writer.take(), std::move(fds)));
    }

    Result<proto::Message> handle_ping(Client& client, const proto::Message& request) {
        (void)client;
        proto::Reader reader(request.payload);
        auto token = reader.get_u64();
        if (!reader.ok() || !reader.empty()) {
            return Result<proto::Message>::ok(
                proto::make_error(request, Code::invalid, "malformed ping"));
        }
        proto::Writer writer;
        writer.put_u64(token);
        return Result<proto::Message>::ok(proto::make_reply(request, writer.take()));
    }

    Result<proto::Message> handle_status(Client& client, const proto::Message& request) {
        (void)client;
        auto counters = scene_.counters();
        std::ostringstream json;
        json << "{\"server_version\":" << proto::kVersion
             << ",\"width\":" << config_.width
             << ",\"height\":" << config_.height
             << ",\"frames\":" << frames_
             << ",\"clients\":" << clients_.size()
             << ",\"windows_created\":" << counters.windows_created
             << ",\"windows_destroyed\":" << counters.windows_destroyed
             << ",\"commits\":" << counters.commits
             << ",\"input_events\":" << counters.input_events
             << ",\"frames_rendered\":" << counters.frames_rendered
             << ",\"service\":\"" << json_escape(config_.service_name) << "\"}";
        proto::Writer writer;
        writer.put_string(json.str());
        return Result<proto::Message>::ok(proto::make_reply(request, writer.take()));
    }

    Result<void> present_frame() {
        render::CpuCompositor compositor;
        auto frame = compositor.render(scene_.snapshot(), config_.width, config_.height,
                                       render::Color{0, 0, 0, 255});
        if (!frame) return Result<void>::fail(frame.error().code, frame.error().message);
        scene_.note_frame_rendered();
        ++frames_;
        if (!config_.frame_dir.empty()) {
            auto dir = ipc::make_private_dir(config_.frame_dir);
            if (!dir) return dir;
            char name[64];
            std::snprintf(name, sizeof(name), "/frame-%06u.ppm", frames_);
            auto written = render::write_ppm(config_.frame_dir + name, frame.value());
            if (!written) return written;
            auto latest = render::write_ppm(config_.frame_dir + "/latest.ppm", frame.value());
            if (!latest) return latest;
            std::snprintf(name, sizeof(name), "/frame-%06u.bgra", frames_);
            auto raw = render::write_bgra(config_.frame_dir + name, frame.value());
            if (!raw) return raw;
        }
        if (config_.verbose) {
            std::fprintf(stderr, "daysetta-server: frame %u hash %s\n", frames_,
                         render::sha256_hex(frame.value().pixels).c_str());
        }
        return Result<void>::ok();
    }

    ServerConfig config_;
    ipc::Channel listener_;
    scene::Scene scene_;
    std::map<int, Client> clients_;
    std::uint64_t token_ = 0;
    std::uint64_t next_surface_ = 1;
    std::uint32_t frames_ = 0;
    bool dirty_ = true;
};

}  // namespace

Result<void> run_server(const ServerConfig& config) {
    Server server(config);
    return server.run();
}

}  // namespace ds
