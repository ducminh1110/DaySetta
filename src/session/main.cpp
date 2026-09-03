#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <string>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

#include "daysetta/ipc.hpp"
#include "daysetta/result.hpp"

namespace {

volatile std::sig_atomic_t g_stop = 0;
void on_signal(int) { g_stop = 1; }

std::string default_runtime_dir() {
    if (const char* xdg = std::getenv("XDG_RUNTIME_DIR"); xdg != nullptr && *xdg != '\0') {
        return std::string(xdg) + "/daysetta";
    }
    return "/tmp/daysetta-" + std::to_string(::geteuid());
}

std::string sibling_binary(const char* name) {
    std::error_code ec;
    auto self = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (ec) return name;
    auto path = self.parent_path() / name;
    if (std::filesystem::exists(path)) return path.string();
    return name;
}

pid_t spawn(const std::string& binary, const std::vector<std::string>& args) {
    pid_t pid = ::fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        std::vector<char*> argv;
        argv.push_back(const_cast<char*>(binary.c_str()));
        for (const auto& arg : args) argv.push_back(const_cast<char*>(arg.c_str()));
        argv.push_back(nullptr);
        ::execv(binary.c_str(), argv.data());
        ::execlp(binary.c_str(), binary.c_str(), nullptr);
        std::fprintf(stderr, "daysetta-session: exec %s: %s\n", binary.c_str(),
                     std::strerror(errno));
        _exit(127);
    }
    return pid;
}

bool wait_for_socket(const std::string& path, int timeout_ms) {
    for (int elapsed = 0; elapsed < timeout_ms; elapsed += 25) {
        struct stat st{};
        if (::stat(path.c_str(), &st) == 0 && S_ISSOCK(st.st_mode)) return true;
        ::usleep(25 * 1000);
    }
    return false;
}

void terminate(pid_t pid) {
    if (pid <= 0) return;
    ::kill(pid, SIGTERM);
}

void write_pidfile(const std::string& path, pid_t broker, pid_t server) {
    FILE* f = std::fopen(path.c_str(), "w");
    if (f == nullptr) return;
    std::fprintf(f, "%ld %ld\n", static_cast<long>(broker), static_cast<long>(server));
    std::fclose(f);
}

std::vector<pid_t> read_pidfile(const std::string& path) {
    std::vector<pid_t> pids;
    FILE* f = std::fopen(path.c_str(), "r");
    if (f == nullptr) return pids;
    long a = 0, b = 0;
    if (std::fscanf(f, "%ld %ld", &a, &b) >= 1) {
        if (a > 0) pids.push_back(static_cast<pid_t>(a));
        if (b > 0) pids.push_back(static_cast<pid_t>(b));
    }
    std::fclose(f);
    return pids;
}

void usage(const char* argv0) {
    std::fprintf(stderr,
                 "usage: %s [options]\n"
                 "  --runtime-dir DIR   session directory\n"
                 "  --width N           output width for the server\n"
                 "  --height N          output height for the server\n"
                 "  --frame-dir DIR     write frames\n"
                 "  --broker PATH       broker binary (default: sibling daysetta-broker)\n"
                 "  --server PATH       server binary (default: sibling daysetta-server)\n"
                 "  --stop              stop the session recorded in DIR/session.pid\n"
                 "  --verbose           pass --verbose to both processes\n",
                 argv0);
}

}  // namespace

int main(int argc, char** argv) {
    std::string runtime_dir = default_runtime_dir();
    std::string broker_bin = sibling_binary("daysetta-broker");
    std::string server_bin = sibling_binary("daysetta-server");
    std::string frame_dir;
    std::uint32_t width = 1280;
    std::uint32_t height = 720;
    bool stop = false;
    bool verbose = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        auto next = [&](const char* name) -> const char* {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "%s needs a value\n", name);
                std::exit(2);
            }
            return argv[++i];
        };
        if (arg == "--runtime-dir") runtime_dir = next("--runtime-dir");
        else if (arg == "--width") width = static_cast<std::uint32_t>(std::strtoul(next("--width"), nullptr, 0));
        else if (arg == "--height") height = static_cast<std::uint32_t>(std::strtoul(next("--height"), nullptr, 0));
        else if (arg == "--frame-dir") frame_dir = next("--frame-dir");
        else if (arg == "--broker") broker_bin = next("--broker");
        else if (arg == "--server") server_bin = next("--server");
        else if (arg == "--stop") stop = true;
        else if (arg == "--verbose") verbose = true;
        else if (arg == "--help" || arg == "-h") { usage(argv[0]); return 0; }
        else {
            std::fprintf(stderr, "unknown option: %s\n", arg.c_str());
            usage(argv[0]);
            return 2;
        }
    }

    std::string broker_socket = runtime_dir + "/broker.sock";
    std::string pidfile = runtime_dir + "/session.pid";

    if (stop) {
        auto pids = read_pidfile(pidfile);
        if (pids.empty()) {
            std::fprintf(stderr, "daysetta-session: no session in %s\n", runtime_dir.c_str());
            return 1;
        }
        for (pid_t pid : pids) terminate(pid);
        ::unlink(pidfile.c_str());
        return 0;
    }

    if (width == 0 || height == 0 || width > 16384 || height > 16384) {
        std::fprintf(stderr, "daysetta-session: invalid size\n");
        return 2;
    }

    auto dir = ds::ipc::make_private_dir(runtime_dir);
    if (!dir) {
        std::fprintf(stderr, "daysetta-session: %s\n", dir.error().message.c_str());
        return 1;
    }

    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    pid_t broker_pid = -1;
    bool own_broker = false;
    auto probe = ds::ipc::Channel::connect(broker_socket);
    if (!probe) {
        std::vector<std::string> args{"--runtime-dir", runtime_dir};
        if (verbose) args.push_back("--verbose");
        broker_pid = spawn(broker_bin, args);
        if (broker_pid < 0) {
            std::fprintf(stderr, "daysetta-session: cannot start broker: %s\n",
                         std::strerror(errno));
            return 1;
        }
        own_broker = true;
        if (!wait_for_socket(broker_socket, 5000)) {
            std::fprintf(stderr, "daysetta-session: broker did not publish %s\n",
                         broker_socket.c_str());
            terminate(broker_pid);
            return 1;
        }
    }

    std::vector<std::string> server_args{"--runtime-dir", runtime_dir,
                                         "--width", std::to_string(width),
                                         "--height", std::to_string(height)};
    if (!frame_dir.empty()) {
        server_args.push_back("--frame-dir");
        server_args.push_back(frame_dir);
    }
    if (verbose) server_args.push_back("--verbose");

    pid_t server_pid = spawn(server_bin, server_args);
    if (server_pid < 0) {
        std::fprintf(stderr, "daysetta-session: cannot start server: %s\n",
                     std::strerror(errno));
        if (own_broker) terminate(broker_pid);
        return 1;
    }
    write_pidfile(pidfile, broker_pid, server_pid);

    int status = 0;
    while (!g_stop) {
        int child_status = 0;
        pid_t done = ::waitpid(-1, &child_status, 0);
        if (done < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (done == server_pid) {
            status = WIFEXITED(child_status) ? WEXITSTATUS(child_status) : 1;
            break;
        }
        if (done == broker_pid) {
            broker_pid = -1;
            if (own_broker) {
                std::fprintf(stderr, "daysetta-session: broker exited\n");
                status = 1;
                break;
            }
        }
    }

    terminate(server_pid);
    if (own_broker) terminate(broker_pid);
    for (int i = 0; i < 40; ++i) {
        pid_t done = ::waitpid(-1, nullptr, WNOHANG);
        if (done <= 0) break;
        ::usleep(25 * 1000);
    }
    ::unlink(pidfile.c_str());
    return status;
}
