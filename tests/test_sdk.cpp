#include "test.hpp"

#include <atomic>
#include <bit>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <mutex>
#include <poll.h>
#include <string>
#include <string_view>
#include <sys/mman.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

#include "daysetta/event.hpp"
#include "daysetta/ipc.hpp"
#include "daysetta/protocol.hpp"
#include "daysetta/sdk.hpp"

namespace {

constexpr std::uint64_t kToken = 0x1122334455667788ull;
constexpr std::uint64_t kWindowId = 0x123456789abcdef0ull;
constexpr std::uint64_t kCommitSequence = 77;
constexpr std::uint32_t kRingCapacity = 64;
constexpr std::uint32_t kCommitWidth = 4;
constexpr std::uint32_t kCommitHeight = 3;
constexpr std::uint32_t kCommitStride = 20;

std::string make_temp_dir(const char* tag) {
    std::string pattern =
        "/tmp/daysetta-test-" + std::to_string(::getpid()) + "-" + tag + "-XXXXXX";
    std::vector<char> buffer(pattern.begin(), pattern.end());
    buffer.push_back('\0');
    char* made = ::mkdtemp(buffer.data());
    if (made == nullptr) {
        throw ds::test::Failure(std::string("mkdtemp failed: ") + std::strerror(errno));
    }
    return std::string(made);
}

struct TempDir {
    std::string path;
    ~TempDir() {
        if (!path.empty()) {
            std::error_code ignored;
            std::filesystem::remove_all(path, ignored);
        }
    }
};

std::vector<std::uint8_t> make_commit_buffer() {
    std::vector<std::uint8_t> pixels(
        static_cast<std::size_t>(kCommitStride) * kCommitHeight + 8);
    for (std::size_t i = 0; i < pixels.size(); ++i) {
        pixels[i] = static_cast<std::uint8_t>((i * 7 + 3) & 0xff);
    }
    return pixels;
}

std::vector<std::uint8_t> make_capture_pixels(std::uint32_t width,
                                              std::uint32_t height) {
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * height * 4);
    for (std::size_t i = 0; i < pixels.size(); ++i) {
        pixels[i] = static_cast<std::uint8_t>((i * 11 + 5) & 0xff);
    }
    return pixels;
}

int write_full(int fd, const std::vector<std::uint8_t>& bytes) {
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        ssize_t written = ::pwrite(fd, bytes.data() + offset, bytes.size() - offset,
                                   static_cast<off_t>(offset));
        if (written < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (written == 0) return -1;
        offset += static_cast<std::size_t>(written);
    }
    return 0;
}

int make_test_memfd(const std::vector<std::uint8_t>& bytes) {
    static std::atomic<std::uint64_t> counter{0};
    int fd = ::memfd_create("ds-sdk-test", MFD_CLOEXEC);
    if (fd < 0) {
        std::string name = "/ds-sdk-test-" + std::to_string(::getpid()) + "-" +
                           std::to_string(counter.fetch_add(1));
        fd = ::shm_open(name.c_str(), O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
        if (fd >= 0) ::shm_unlink(name.c_str());
    }
    if (fd < 0) return -1;
    if (::ftruncate(fd, static_cast<off_t>(bytes.size())) != 0 ||
        write_full(fd, bytes) != 0) {
        int saved = errno;
        ::close(fd);
        errno = saved;
        return -1;
    }
    return fd;
}

class FakeBroker {
public:
    FakeBroker(std::string runtime_dir, std::string service, std::string path,
               std::uint64_t token)
        : service_(std::move(service)),
          path_(std::move(path)),
          token_(token),
          socket_path_(std::move(runtime_dir) + "/broker.sock") {
        auto listener = ds::ipc::Channel::listen(socket_path_);
        if (!listener) {
            throw ds::test::Failure("fake broker listen: " +
                                    listener.error().message);
        }
        listener_ = std::move(listener.value());
    }

    ~FakeBroker() { stop(); }

    void start() { thread_ = std::thread([this] { run(); }); }

    void stop() {
        if (stopping_.exchange(true)) return;
        if (thread_.joinable()) thread_.join();
        listener_.close();
    }

private:
    void run() {
        while (!stopping_.load()) {
            pollfd descriptor{listener_.fd(), POLLIN, 0};
            int rc = ::poll(&descriptor, 1, 25);
            if (rc < 0) {
                if (errno == EINTR) continue;
                break;
            }
            if (rc == 0) continue;
            if ((descriptor.revents & POLLIN) == 0) continue;
            auto channel = listener_.accept();
            if (!channel) {
                if (stopping_.load()) break;
                continue;
            }
            handle(std::move(channel.value()));
        }
    }

    void handle(ds::ipc::Channel channel) {
        std::vector<ds::ipc::Fd> fds;
        auto datagram = channel.receive(&fds);
        if (!datagram || !fds.empty()) return;
        bool ok = false;
        ds::sdk::broker_proto::Request request =
            ds::sdk::broker_proto::decode_request(*datagram, &ok);
        ds::sdk::broker_proto::Response response;
        if (!ok) {
            response.code = ds::Code::protocol;
            response.detail = "malformed request";
        } else if (request.op == ds::sdk::broker_proto::Op::lookup_service &&
                   request.name == service_) {
            response.path = path_;
            response.token = token_;
        } else if (request.op == ds::sdk::broker_proto::Op::lookup_service) {
            response.code = ds::Code::not_found;
            response.detail = "no such service";
        } else {
            response.code = ds::Code::unsupported;
            response.detail = "unsupported fake broker op";
        }
        channel.send(ds::sdk::broker_proto::encode_response(response));
    }

    std::string service_;
    std::string path_;
    std::uint64_t token_;
    std::string socket_path_;
    ds::ipc::Channel listener_;
    std::atomic<bool> stopping_{false};
    std::thread thread_;
};

class FakeServer {
public:
    FakeServer(std::string runtime_dir, std::uint64_t expected_token,
               std::vector<std::uint8_t> expected_commit)
        : expected_token_(expected_token),
          expected_commit_(std::move(expected_commit)),
          socket_path_(std::move(runtime_dir) + "/server.sock") {
        auto listener = ds::ipc::Channel::listen(socket_path_);
        if (!listener) {
            throw ds::test::Failure("fake server listen: " +
                                    listener.error().message);
        }
        listener_ = std::move(listener.value());
    }

    ~FakeServer() { stop(); }

    void start() { thread_ = std::thread([this] { run(); }); }

    void stop() {
        if (stopping_.exchange(true)) return;
        if (thread_.joinable()) thread_.join();
        listener_.close();
    }

    const std::string& path() const { return socket_path_; }
    bool commit_verified() const { return commit_verified_.load(); }

    std::string failure() {
        std::lock_guard<std::mutex> lock(failure_mutex_);
        return failure_;
    }

private:
    void record_failure(const std::string& message) {
        std::lock_guard<std::mutex> lock(failure_mutex_);
        if (failure_.empty()) failure_ = message;
    }

    void run() {
        try {
            while (!stopping_.load()) {
                pollfd descriptor{listener_.fd(), POLLIN, 0};
                int rc = ::poll(&descriptor, 1, 25);
                if (rc < 0) {
                    if (errno == EINTR) continue;
                    break;
                }
                if (rc == 0) continue;
                if ((descriptor.revents & POLLIN) == 0) continue;
                auto channel = listener_.accept();
                if (!channel) {
                    if (stopping_.load()) break;
                    record_failure("fake server accept failed");
                    continue;
                }
                serve_client(std::move(channel.value()));
            }
        } catch (const std::exception& error) {
            record_failure(std::string("fake server thread: ") + error.what());
        }
    }

    void serve_client(ds::ipc::Channel channel) {
        auto hello = ds::proto::receive_message(channel, 5000);
        if (!hello) {
            record_failure("fake server did not receive hello");
            return;
        }
        const ds::proto::Message& hello_request = hello.value();
        ds::proto::Reader hello_reader(hello_request.payload);
        std::uint32_t client_version = hello_reader.get_u32();
        std::uint32_t role = hello_reader.get_u32();
        std::string app_name = hello_reader.get_string(4096);
        std::uint64_t token = hello_reader.get_u64();
        if (!hello_reader.ok() || !hello_reader.empty()) {
            record_failure("malformed hello request");
            ds::proto::send_message(
                channel, ds::proto::make_error(hello_request, ds::Code::protocol,
                                               "malformed hello"));
            return;
        }
        if (client_version != 1) {
            record_failure("unexpected client version");
            ds::proto::send_message(
                channel, ds::proto::make_error(hello_request, ds::Code::invalid,
                                               "bad client version"));
            return;
        }
        if (role != static_cast<std::uint32_t>(ds::proto::Role::test)) {
            record_failure("unexpected client role");
        }
        if (app_name != "sdk-test") record_failure("unexpected app name");
        if (token != expected_token_) {
            ds::proto::send_message(
                channel, ds::proto::make_error(hello_request, ds::Code::denied,
                                               "token mismatch"));
            return;
        }

        ds::proto::Writer hello_reply;
        hello_reply.put_u32(1);        // server version
        hello_reply.put_u32(42);       // session id
        hello_reply.put_u64(0x1f);     // capabilities
        hello_reply.put_u32(1u << 20); // max message
        hello_reply.put_u32(128);      // max windows
        auto sent = ds::proto::send_message(
            channel, ds::proto::make_reply(hello_request, hello_reply.take()));
        if (!sent) {
            record_failure("fake server hello reply failed");
            return;
        }

        for (;;) {
            auto message = ds::proto::receive_message(channel, 5000);
            if (!message) return;
            ds::proto::Message reply = dispatch(message.value());
            auto reply_sent = ds::proto::send_message(channel, reply);
            if (!reply_sent) {
                record_failure("fake server reply failed");
                return;
            }
        }
    }

    ds::proto::Message dispatch(const ds::proto::Message& request) {
        using ds::proto::Op;
        switch (request.op()) {
            case Op::create_window: {
                ds::proto::Reader reader(request.payload);
                ds::proto::Rect frame = reader.get_rect();
                std::int32_t level = reader.get_i32();
                std::string title = reader.get_string(4096);
                if (!reader.ok() || !reader.empty()) {
                    return bad_request(request, "create_window payload");
                }
                if (frame.x != 10 || frame.y != 20 || frame.w != 300 ||
                    frame.h != 200 || level != 2 || title != "sdk window") {
                    record_failure("create_window fields were encoded incorrectly");
                }
                ds::proto::Writer reply;
                reply.put_u64(kWindowId);
                return ds::proto::make_reply(request, reply.take());
            }

            case Op::destroy_window: {
                ds::proto::Reader reader(request.payload);
                std::uint64_t window = reader.get_u64();
                if (!reader.ok() || !reader.empty()) {
                    return bad_request(request, "destroy_window payload");
                }
                if (window != kWindowId) record_failure("destroy_window id");
                return ds::proto::make_reply(request);
            }

            case Op::set_window_frame: {
                ds::proto::Reader reader(request.payload);
                std::uint64_t window = reader.get_u64();
                ds::proto::Rect frame = reader.get_rect();
                if (!reader.ok() || !reader.empty()) {
                    return bad_request(request, "set_window_frame payload");
                }
                if (window != kWindowId || frame.x != 1 || frame.y != 2 ||
                    frame.w != 30 || frame.h != 40) {
                    record_failure("set_window_frame fields");
                }
                return ds::proto::make_reply(request);
            }

            case Op::set_window_visible: {
                ds::proto::Reader reader(request.payload);
                std::uint64_t window = reader.get_u64();
                std::uint8_t visible = reader.get_u8();
                if (!reader.ok() || !reader.empty()) {
                    return bad_request(request, "set_window_visible payload");
                }
                if (window != kWindowId || visible != 1) {
                    record_failure("set_window_visible fields");
                }
                return ds::proto::make_reply(request);
            }

            case Op::set_window_opacity: {
                ds::proto::Reader reader(request.payload);
                std::uint64_t window = reader.get_u64();
                std::uint32_t opacity = reader.get_u32();
                if (!reader.ok() || !reader.empty()) {
                    return bad_request(request, "set_window_opacity payload");
                }
                if (window != kWindowId ||
                    opacity != std::bit_cast<std::uint32_t>(0.5f)) {
                    record_failure("set_window_opacity fields");
                }
                return ds::proto::make_reply(request);
            }

            case Op::set_window_title: {
                ds::proto::Reader reader(request.payload);
                std::uint64_t window = reader.get_u64();
                std::string title = reader.get_string(4096);
                if (!reader.ok() || !reader.empty()) {
                    return bad_request(request, "set_window_title payload");
                }
                if (window != kWindowId || title != "renamed") {
                    record_failure("set_window_title fields");
                }
                return ds::proto::make_reply(request);
            }

            case Op::order_window: {
                ds::proto::Reader reader(request.payload);
                std::uint64_t window = reader.get_u64();
                std::uint32_t order = reader.get_u32();
                if (!reader.ok() || !reader.empty()) {
                    return bad_request(request, "order_window payload");
                }
                if (window != kWindowId ||
                    order != static_cast<std::uint32_t>(
                                 ds::proto::WindowOrder::lower)) {
                    record_failure("order_window fields");
                }
                return ds::proto::make_reply(request);
            }

            case Op::commit_surface:
                return handle_commit(request);

            case Op::subscribe_events:
                return handle_subscribe(request);

            case Op::scene_dump: {
                if (!request.payload.empty()) {
                    return bad_request(request, "scene_dump payload");
                }
                ds::proto::Writer reply;
                reply.put_string("{\"windows\":[]}");
                return ds::proto::make_reply(request, reply.take());
            }

            case Op::inject_input: {
                ds::proto::Reader reader(request.payload);
                std::uint32_t type = reader.get_u32();
                std::uint32_t flags = reader.get_u32();
                std::uint64_t timestamp = reader.get_u64();
                std::uint64_t target = reader.get_u64();
                std::int32_t a = reader.get_i32();
                std::int32_t b = reader.get_i32();
                std::int32_t c = reader.get_i32();
                std::int32_t d = reader.get_i32();
                if (!reader.ok() || !reader.empty()) {
                    return bad_request(request, "inject_input payload");
                }
                if (type != static_cast<std::uint32_t>(
                                ds::event::Type::pointer_move) ||
                    flags != 0 || timestamp != 123456789ull || target != 4242 ||
                    a != 33 || b != 44 || c != 0 || d != 0) {
                    record_failure("inject_input fields");
                }
                ds::proto::Writer reply;
                reply.put_u64(target);
                return ds::proto::make_reply(request, reply.take());
            }

            case Op::capture:
                return handle_capture(request);

            case Op::ping: {
                ds::proto::Reader reader(request.payload);
                std::uint64_t token = reader.get_u64();
                if (!reader.ok() || !reader.empty()) {
                    return bad_request(request, "ping payload");
                }
                if (token != 0x44535431ull) record_failure("ping token");
                ds::proto::Writer reply;
                reply.put_u64(token);
                return ds::proto::make_reply(request, reply.take());
            }

            default:
                record_failure("fake server received an unexpected op");
                return ds::proto::make_error(request, ds::Code::unsupported,
                                             "unexpected op");
        }
    }

    ds::proto::Message bad_request(const ds::proto::Message& request,
                                   const char* what) {
        record_failure(std::string("malformed ") + what);
        return ds::proto::make_error(request, ds::Code::protocol, what);
    }

    ds::proto::Message handle_commit(const ds::proto::Message& request) {
        ds::proto::Reader reader(request.payload);
        std::uint64_t window = reader.get_u64();
        std::uint32_t width = reader.get_u32();
        std::uint32_t height = reader.get_u32();
        std::uint32_t stride = reader.get_u32();
        std::uint32_t format = reader.get_u32();
        reader.get_u64();  // surface sequence
        std::uint32_t damage_count = reader.get_u32();
        if (damage_count > 64) return bad_request(request, "commit damage count");
        for (std::uint32_t i = 0; i < damage_count; ++i) reader.get_rect();
        if (!reader.ok() || !reader.empty()) {
            return bad_request(request, "commit_surface payload");
        }
        if (window != kWindowId || width != kCommitWidth ||
            height != kCommitHeight || stride != kCommitStride ||
            format != static_cast<std::uint32_t>(
                          ds::proto::PixelFormat::bgra8888)) {
            record_failure("commit_surface fields");
        }
        if (request.fds.size() != 1) {
            return bad_request(request, "commit descriptor count");
        }
        const std::uint64_t total =
            static_cast<std::uint64_t>(stride) * height;
        if (total > expected_commit_.size()) {
            return bad_request(request, "commit expected size");
        }
        int fd = request.fds.front().get();
        struct stat st {};
        if (::fstat(fd, &st) != 0 ||
            st.st_size != static_cast<off_t>(total)) {
            record_failure("commit memfd size is not stride * height");
        } else {
            std::vector<std::uint8_t> received(static_cast<std::size_t>(total));
            std::size_t offset = 0;
            bool read_ok = true;
            while (offset < received.size()) {
                ssize_t got = ::pread(fd, received.data() + offset,
                                      received.size() - offset,
                                      static_cast<off_t>(offset));
                if (got < 0) {
                    if (errno == EINTR) continue;
                    read_ok = false;
                    break;
                }
                if (got == 0) {
                    read_ok = false;
                    break;
                }
                offset += static_cast<std::size_t>(got);
            }
            if (!read_ok ||
                std::memcmp(received.data(), expected_commit_.data(),
                            received.size()) != 0) {
                record_failure("commit memfd content does not match pixels");
            } else {
                commit_verified_.store(true);
            }
        }

        ds::proto::Writer reply;
        reply.put_u64(kCommitSequence);
        reply.put_u32(damage_count);
        return ds::proto::make_reply(request, reply.take());
    }

    ds::proto::Message handle_subscribe(const ds::proto::Message& request) {
        if (!request.payload.empty()) {
            return bad_request(request, "subscribe_events payload");
        }
        auto ring_fd = ds::event::create_ring_fd(kRingCapacity);
        if (!ring_fd) {
            record_failure("cannot create event ring");
            return ds::proto::make_error(request, ds::Code::io,
                                         "cannot create ring");
        }
        int fd = ring_fd.value();
        ds::event::Event event;
        event.type = ds::event::Type::key_down;
        event.flags = 0;
        event.timestamp_ns = 123456789;
        event.target_window = 7;
        event.a = 65;
        event.b = 1;
        event.c = 2;
        event.d = 3;
        ds::event::RingHeader header{};
        header.magic = ds::event::kRingMagic;
        header.version = ds::event::kRingVersion;
        header.capacity = kRingCapacity;
        header.tail = 1;
        if (::pwrite(fd, &header, sizeof(header), 0) !=
                static_cast<ssize_t>(sizeof(header)) ||
            ::pwrite(fd, &event, sizeof(event),
                     static_cast<off_t>(ds::event::kRingHeaderSize)) !=
                static_cast<ssize_t>(sizeof(event))) {
            ::close(fd);
            record_failure("cannot fill event ring");
            return ds::proto::make_error(request, ds::Code::io,
                                         "cannot fill event ring");
        }

        ds::proto::Writer reply;
        reply.put_u32(kRingCapacity);
        reply.put_u32(static_cast<std::uint32_t>(ds::event::kEventSize));
        std::vector<ds::ipc::Fd> fds;
        fds.emplace_back(fd);
        return ds::proto::make_reply(request, reply.take(), std::move(fds));
    }

    ds::proto::Message handle_capture(const ds::proto::Message& request) {
        ds::proto::Reader reader(request.payload);
        std::uint32_t width = reader.get_u32();
        std::uint32_t height = reader.get_u32();
        if (!reader.ok() || !reader.empty()) {
            return bad_request(request, "capture payload");
        }
        if (width != 2 || height != 2) record_failure("capture dimensions");
        std::vector<std::uint8_t> pixels = make_capture_pixels(width, height);
        int fd = make_test_memfd(pixels);
        if (fd < 0) {
            record_failure("cannot create capture memfd");
            return ds::proto::make_error(request, ds::Code::io,
                                         "cannot create capture");
        }

        ds::proto::Writer reply;
        reply.put_u32(width);
        reply.put_u32(height);
        reply.put_u32(width * 4);
        reply.put_u32(static_cast<std::uint32_t>(
            ds::proto::PixelFormat::bgra8888));
        std::vector<ds::ipc::Fd> fds;
        fds.emplace_back(fd);
        return ds::proto::make_reply(request, reply.take(), std::move(fds));
    }

    std::uint64_t expected_token_;
    std::vector<std::uint8_t> expected_commit_;
    std::string socket_path_;
    ds::ipc::Channel listener_;
    std::atomic<bool> stopping_{false};
    std::atomic<bool> commit_verified_{false};
    std::thread thread_;
    std::mutex failure_mutex_;
    std::string failure_;
};

ds::sdk::Client connect_ok(const std::string& runtime_dir) {
    auto result = ds::sdk::Client::connect(runtime_dir, "sdk-test",
                                           ds::proto::Role::test);
    if (!result) {
        throw ds::test::Failure("Client::connect: " + result.error().message);
    }
    return std::move(result.value());
}

void check_ok(const ds::Result<void>& result, const char* what) {
    if (!result) {
        throw ds::test::Failure(std::string(what) + ": " + result.error().message);
    }
}

}  // namespace

DS_TEST(sdk_fake_server) {
    TempDir temp{make_temp_dir("sdk")};
    std::vector<std::uint8_t> commit_buffer = make_commit_buffer();
    const std::size_t commit_bytes =
        static_cast<std::size_t>(kCommitStride) * kCommitHeight;
    std::vector<std::uint8_t> expected_commit(commit_buffer.begin(),
                                              commit_buffer.begin() + commit_bytes);

    FakeServer server(temp.path, kToken, expected_commit);
    FakeBroker broker(temp.path, "org.daysetta.server", server.path(), kToken);
    server.start();
    broker.start();

    {
        ds::sdk::Client client = connect_ok(temp.path);
        DS_CHECK_EQ(client.hello().server_version, 1u);
        DS_CHECK_EQ(client.hello().session_id, 42u);
        DS_CHECK_EQ(client.hello().capabilities, 0x1full);
        DS_CHECK_EQ(client.hello().max_message, 1u << 20);
        DS_CHECK_EQ(client.hello().max_windows, 128u);
        DS_CHECK_EQ(client.session_id(), 42u);

        auto window = client.create_window(ds::proto::Rect{10, 20, 300, 200}, 2,
                                           "sdk window");
        if (!window) throw ds::test::Failure("create_window: " + window.error().message);
        DS_CHECK_EQ(window.value(), kWindowId);

        check_ok(client.destroy_window(kWindowId), "destroy_window");

        check_ok(client.set_window_frame(kWindowId, ds::proto::Rect{1, 2, 30, 40}),
                 "set_window_frame");
        check_ok(client.set_window_visible(kWindowId, true), "set_window_visible");
        check_ok(client.set_window_opacity(kWindowId, 0.5f), "set_window_opacity");
        check_ok(client.set_window_title(kWindowId, "renamed"), "set_window_title");
        check_ok(client.order_window(kWindowId, ds::proto::WindowOrder::lower),
                 "order_window");

        std::vector<ds::proto::Rect> damage = {
            ds::proto::Rect{0, 0, 2, 2},
            ds::proto::Rect{2, 1, 2, 2},
        };
        auto committed = client.commit_surface(
            kWindowId, kCommitWidth, kCommitHeight, kCommitStride,
            ds::proto::PixelFormat::bgra8888, commit_buffer, damage);
        if (!committed) {
            throw ds::test::Failure("commit_surface: " + committed.error().message);
        }
        DS_CHECK_EQ(committed.value(), kCommitSequence);
        DS_CHECK(server.commit_verified());

        auto ping = client.ping(0x44535431ull);
        if (!ping) throw ds::test::Failure("ping: " + ping.error().message);
        DS_CHECK_EQ(ping.value(), 0x44535431ull);

        auto scene = client.scene_dump();
        if (!scene) throw ds::test::Failure("scene_dump: " + scene.error().message);
        DS_CHECK_EQ(scene.value(), std::string("{\"windows\":[]}"));

        ds::event::Event input;
        input.type = ds::event::Type::pointer_move;
        input.timestamp_ns = 123456789;
        input.target_window = 4242;
        input.a = 33;
        input.b = 44;
        auto injected = client.inject_input(input);
        if (!injected) {
            throw ds::test::Failure("inject_input: " + injected.error().message);
        }
        DS_CHECK_EQ(injected.value(), 4242ull);

        check_ok(client.subscribe_events(), "subscribe_events");
        auto event = client.poll_event(1000);
        if (!event) throw ds::test::Failure("poll_event: " + event.error().message);
        DS_CHECK(event.value().type == ds::event::Type::key_down);
        DS_CHECK_EQ(event.value().timestamp_ns, 123456789ull);
        DS_CHECK_EQ(event.value().target_window, 7ull);
        DS_CHECK_EQ(event.value().a, 65);
        DS_CHECK_EQ(event.value().b, 1);
        DS_CHECK_EQ(event.value().c, 2);
        DS_CHECK_EQ(event.value().d, 3);
        auto timed_out = client.poll_event(50);
        DS_CHECK(!timed_out);
        DS_CHECK(timed_out.code() == ds::Code::timeout);

        auto frame = client.capture(2, 2);
        if (!frame) throw ds::test::Failure("capture: " + frame.error().message);
        DS_CHECK_EQ(frame.value().width, 2u);
        DS_CHECK_EQ(frame.value().height, 2u);
        DS_CHECK_EQ(frame.value().stride, 8u);
        std::vector<std::uint8_t> expected_pixels = make_capture_pixels(2, 2);
        DS_CHECK(frame.value().pixels.size() >= expected_pixels.size());
        DS_CHECK(std::memcmp(frame.value().pixels.data(), expected_pixels.data(),
                             expected_pixels.size()) == 0);
    }

    {
        ds::sdk::Client lazy = connect_ok(temp.path);
        auto event = lazy.poll_event(1000);
        if (!event) {
            throw ds::test::Failure("lazy poll_event: " + event.error().message);
        }
        DS_CHECK(event.value().type == ds::event::Type::key_down);
        DS_CHECK_EQ(event.value().target_window, 7ull);
    }

    const std::string failure = server.failure();
    DS_CHECK(failure.empty());
}

DS_TEST(sdk_hello_denied) {
    TempDir temp{make_temp_dir("sdk-denied")};
    FakeServer server(temp.path, kToken, {});
    FakeBroker broker(temp.path, "org.daysetta.server", server.path(),
                      kToken ^ 0xffffffffffffffffull);
    server.start();
    broker.start();

    auto result = ds::sdk::Client::connect(temp.path, "sdk-test",
                                           ds::proto::Role::test);
    DS_CHECK(!result);
    DS_CHECK(result.code() == ds::Code::denied);
    DS_CHECK(result.error().message.find("token mismatch") != std::string::npos);
    DS_CHECK(server.failure().empty());
}
