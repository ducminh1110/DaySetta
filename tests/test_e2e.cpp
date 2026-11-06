#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <sys/stat.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

#include "daysetta/render.hpp"
#include "daysetta/sdk.hpp"
#include "test.hpp"

namespace {

struct Child {
    pid_t pid = -1;
    ~Child() {
        if (pid > 0) {
            ::kill(pid, SIGTERM);
            int status = 0;
            for (int i = 0; i < 100; ++i) {
                pid_t done = ::waitpid(pid, &status, WNOHANG);
                if (done == pid) {
                    pid = -1;
                    break;
                }
                ::usleep(10 * 1000);
            }
            if (pid > 0) {
                ::kill(pid, SIGKILL);
                ::waitpid(pid, &status, 0);
                pid = -1;
            }
        }
    }
    Child() = default;
    Child(const Child&) = delete;
    Child& operator=(const Child&) = delete;
};

pid_t spawn(const std::string& binary, const std::vector<std::string>& args) {
    pid_t pid = ::fork();
    if (pid != 0) return pid;
    std::vector<char*> argv;
    argv.push_back(const_cast<char*>(binary.c_str()));
    for (const auto& arg : args) argv.push_back(const_cast<char*>(arg.c_str()));
    argv.push_back(nullptr);
    ::execv(binary.c_str(), argv.data());
    std::fprintf(stderr, "e2e: exec %s: %s\n", binary.c_str(), std::strerror(errno));
    _exit(127);
}

bool wait_socket(const std::string& path, int timeout_ms) {
    for (int elapsed = 0; elapsed < timeout_ms; elapsed += 20) {
        struct stat st{};
        if (::stat(path.c_str(), &st) == 0 && S_ISSOCK(st.st_mode)) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return false;
}

std::vector<std::uint8_t> solid(std::uint32_t width, std::uint32_t height,
                                std::uint8_t b, std::uint8_t g, std::uint8_t r,
                                std::uint8_t a) {
    std::uint32_t stride = width * 4;
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(stride) * height);
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            std::uint8_t* p = pixels.data() + static_cast<std::size_t>(y) * stride + x * 4;
            p[0] = b;
            p[1] = g;
            p[2] = r;
            p[3] = a;
        }
    }
    return pixels;
}

void pixel(const ds::render::Frame& frame, std::uint32_t x, std::uint32_t y,
           std::uint8_t expected_b, std::uint8_t expected_g, std::uint8_t expected_r) {
    const std::uint8_t* p = frame.pixels.data() + static_cast<std::size_t>(y) * frame.stride +
                            x * 4;
    DS_CHECK_EQ(static_cast<int>(p[0]), static_cast<int>(expected_b));
    DS_CHECK_EQ(static_cast<int>(p[1]), static_cast<int>(expected_g));
    DS_CHECK_EQ(static_cast<int>(p[2]), static_cast<int>(expected_r));
}

}  // namespace

DS_TEST(e2e_two_windows_capture_and_input) {
    const std::string runtime = "/tmp/daysetta-e2e-" + std::to_string(::getpid());
    const std::string frames = runtime + "/frames";
    std::filesystem::remove_all(runtime);
    std::filesystem::create_directories(runtime);
    std::filesystem::permissions(runtime, std::filesystem::perms::owner_all);

    Child broker;
    broker.pid = spawn(DS_BROKER_BIN, {"--runtime-dir", runtime});
    DS_CHECK(broker.pid > 0);
    DS_CHECK(wait_socket(runtime + "/broker.sock", 5000));

    Child server;
    server.pid = spawn(DS_SERVER_BIN,
                       {"--runtime-dir", runtime, "--width", "320", "--height", "240",
                        "--tick-ms", "5", "--frame-dir", frames, "--verbose"});
    DS_CHECK(server.pid > 0);
    DS_CHECK(wait_socket(runtime + "/server.sock", 5000));

    auto client = ds::sdk::Client::connect(runtime, "e2e-test", ds::proto::Role::test);
    if (!client) {
        throw ds::test::Failure(std::string("connect failed: ") + client.error().message);
    }
    const auto hello = client->hello();
    DS_CHECK_EQ(hello.server_version, 1u);
    DS_CHECK(hello.session_id != 0);
    DS_CHECK((hello.capabilities & 0x10) != 0);

    auto w1 = client->create_window({0, 0, 100, 100}, 0, "blue");
    DS_CHECK(w1.has_value());
    auto w2 = client->create_window({50, 50, 100, 100}, 10, "red");
    DS_CHECK(w2.has_value());
    DS_CHECK(w2.value() != w1.value());

    auto blue = solid(100, 100, 255, 0, 0, 255);
    auto red = solid(100, 100, 0, 0, 255, 255);
    DS_CHECK(client->commit_surface(w1.value(), 100, 100, 400,
                                    ds::proto::PixelFormat::bgra8888, blue).has_value());
    DS_CHECK(client->commit_surface(w2.value(), 100, 100, 400,
                                    ds::proto::PixelFormat::bgra8888, red).has_value());
    DS_CHECK(client->set_window_visible(w1.value(), true).has_value());
    DS_CHECK(client->set_window_visible(w2.value(), true).has_value());

    auto captured = client->capture(320, 240);
    if (!captured) {
        throw ds::test::Failure(std::string("capture failed: ") + captured.error().message);
    }
    DS_CHECK_EQ(captured.value().width, 320u);
    DS_CHECK_EQ(captured.value().height, 240u);
    DS_CHECK(captured.value().valid());
    pixel(captured.value(), 10, 10, 255, 0, 0);
    pixel(captured.value(), 60, 60, 0, 0, 255);
    pixel(captured.value(), 200, 200, 0, 0, 0);

    auto again = client->capture(320, 240);
    DS_CHECK(again.has_value());
    DS_CHECK_EQ(ds::render::sha256_hex(captured.value().pixels),
                ds::render::sha256_hex(again.value().pixels));

    DS_CHECK(client->subscribe_events().has_value());
    ds::event::Event input;
    input.type = ds::event::Type::button_down;
    input.timestamp_ns = 123456;
    input.a = 60;
    input.b = 60;
    input.c = 1;
    auto target = client->inject_input(input);
    DS_CHECK(target.has_value());
    DS_CHECK_EQ(target.value(), w2.value());

    auto event = client->poll_event(1000);
    if (!event) {
        throw ds::test::Failure(std::string("poll_event failed: ") + event.error().message);
    }
    DS_CHECK_EQ(static_cast<std::uint32_t>(event.value().type),
                static_cast<std::uint32_t>(ds::event::Type::button_down));
    DS_CHECK_EQ(event.value().target_window, w2.value());

    auto dump = client->scene_dump();
    DS_CHECK(dump.has_value());
    DS_CHECK(dump.value().find("\"blue\"") != std::string::npos);
    DS_CHECK(dump.value().find("\"red\"") != std::string::npos);

    auto ping = client->ping(0x44535431);
    DS_CHECK(ping.has_value());
    DS_CHECK_EQ(ping.value(), 0x44535431ull);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    DS_CHECK(std::filesystem::exists(frames + "/latest.ppm"));
    std::uintmax_t size = std::filesystem::file_size(frames + "/latest.ppm");
    DS_CHECK(size >= 15 + 320 * 240 * 3);

    DS_CHECK(client->destroy_window(w1.value()).has_value());
    DS_CHECK(!client->destroy_window(w2.value() + 999).has_value());

    std::filesystem::remove_all(runtime);
}
