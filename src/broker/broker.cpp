#include "daysetta/daemon.hpp"

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fcntl.h>
#include <map>
#include <poll.h>
#include <string>
#include <string_view>
#include <sys/random.h>
#include <sys/signalfd.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>
#include <vector>

#include "daysetta/ipc.hpp"
#include "daysetta/sdk.hpp"

namespace ds {
namespace {

constexpr std::size_t kMaxBrokerDatagram = 64 * 1024;

struct Service {
    std::string path;
    std::uint64_t token = 0;
    std::uint32_t pid = 0;
    std::uint32_t uid = 0;
};

using Registry = std::map<std::string, Service>;

sdk::broker_proto::Response error_response(Code code, std::string detail) {
    sdk::broker_proto::Response response;
    response.code = code;
    response.detail = std::move(detail);
    return response;
}

bool valid_service_name(std::string_view name) {
    if (name.empty() || name.size() > 128) return false;
    auto alpha = [](char c) {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
    };
    auto rest = [&](char c) {
        return alpha(c) || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
    };
    if (!alpha(name.front())) return false;
    return std::all_of(name.begin() + 1, name.end(), rest);
}

bool path_within(const std::filesystem::path& root,
                 const std::filesystem::path& path) {
    std::filesystem::path relative = path.lexically_relative(root);
    if (relative.empty() || relative == ".") return false;
    if (relative.is_absolute()) return false;
    auto first = relative.begin();
    return first != relative.end() && *first != "..";
}

std::uint64_t random_token() {
    std::uint64_t token = 0;
    ssize_t got;
    do {
        got = ::getrandom(&token, sizeof(token), 0);
    } while (got < 0 && errno == EINTR);
    if (got == static_cast<ssize_t>(sizeof(token)) && token != 0) return token;

    int fd = ::open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (fd >= 0) {
        std::size_t offset = 0;
        while (offset < sizeof(token)) {
            ssize_t n = ::read(fd, reinterpret_cast<char*>(&token) + offset,
                               sizeof(token) - offset);
            if (n < 0) {
                if (errno == EINTR) continue;
                break;
            }
            if (n == 0) break;
            offset += static_cast<std::size_t>(n);
        }
        ::close(fd);
        if (offset == sizeof(token) && token != 0) return token;
    }
    return 1;  // the contract only requires a nonzero token
}

std::string json_escape(std::string_view value) {
    static constexpr char hex[] = "0123456789abcdef";
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
                if (c < 0x20 || c == 0x7f) {
                    out += "\\u00";
                    out.push_back(hex[c >> 4]);
                    out.push_back(hex[c & 0x0f]);
                } else {
                    out.push_back(static_cast<char>(c));
                }
                break;
        }
    }
    return out;
}

std::string registry_json(const Registry& registry) {
    std::string json = "[";
    bool first = true;
    for (const auto& [name, service] : registry) {
        if (!first) json += ',';
        first = false;
        json += "{\"name\":\"";
        json += json_escape(name);
        json += "\",\"path\":\"";
        json += json_escape(service.path);
        json += "\",\"pid\":";
        json += std::to_string(service.pid);
        json += ",\"uid\":";
        json += std::to_string(service.uid);
        json += '}';
    }
    json += ']';
    return json;
}

sdk::broker_proto::Response register_service(
    Registry* registry, const std::filesystem::path& root,
    const sdk::broker_proto::Request& request, const ipc::PeerCredentials& peer,
    bool verbose) {
    if (!valid_service_name(request.name)) {
        return error_response(Code::invalid,
                              "service name must match [A-Za-z][A-Za-z0-9._-]{0,127}");
    }
    if (registry->find(request.name) != registry->end()) {
        return error_response(Code::busy,
                              "service already registered: " + request.name);
    }

    std::filesystem::path raw(request.path);
    if (request.path.empty() || !raw.is_absolute()) {
        return error_response(Code::invalid, "service path must be absolute");
    }
    std::error_code ec;
    std::filesystem::path canonical = std::filesystem::weakly_canonical(raw, ec);
    if (ec) {
        return error_response(Code::invalid,
                              "cannot canonicalize service path: " + ec.message());
    }
    if (!path_within(root, canonical)) {
        return error_response(Code::invalid,
                              "service path must be inside the runtime directory");
    }

    struct stat st{};
    if (::lstat(canonical.c_str(), &st) != 0) {
        Code code = errno == ENOENT ? Code::not_found : Code::io;
        return error_response(code, std::string("cannot stat service path: ") +
                                        std::strerror(errno));
    }
    if (!S_ISSOCK(st.st_mode)) {
        return error_response(Code::invalid, "service path is not a Unix socket");
    }
    if (st.st_uid != peer.uid) {
        return error_response(Code::denied,
                              "service socket is not owned by the caller");
    }

    Service service;
    service.path = canonical.string();
    service.token = random_token();
    service.pid = peer.pid;
    service.uid = peer.uid;
    const std::string name = request.name;
    registry->emplace(name, service);

    sdk::broker_proto::Response response;
    response.name = name;
    response.path = service.path;
    response.token = service.token;
    response.detail = "registered";
    if (verbose) {
        std::fprintf(stderr, "daysetta-broker: registered %s at %s\n", name.c_str(),
                     service.path.c_str());
    }
    return response;
}

sdk::broker_proto::Response handle_request(
    Registry* registry, const std::filesystem::path& root,
    const sdk::broker_proto::Request& request, const ipc::PeerCredentials& peer,
    bool verbose) {
    using Ops = sdk::broker_proto::Op;
    switch (request.op) {
        case Ops::register_service:
            return register_service(registry, root, request, peer, verbose);

        case Ops::lookup_service: {
            auto found = registry->find(request.name);
            if (found == registry->end()) {
                return error_response(Code::not_found,
                                      "service not registered: " + request.name);
            }
            if (found->second.uid != peer.uid) {
                return error_response(Code::denied,
                                      "service belongs to a different user");
            }
            sdk::broker_proto::Response response;
            response.name = found->first;
            response.path = found->second.path;
            response.token = found->second.token;
            return response;
        }

        case Ops::unregister_service: {
            auto found = registry->find(request.name);
            if (found == registry->end()) {
                return error_response(Code::not_found,
                                      "service not registered: " + request.name);
            }
            if (found->second.uid != peer.uid) {
                return error_response(Code::denied,
                                      "only the registering user may unregister");
            }
            registry->erase(found);
            sdk::broker_proto::Response response;
            response.name = request.name;
            response.detail = "unregistered";
            if (verbose) {
                std::fprintf(stderr, "daysetta-broker: unregistered %s\n",
                             request.name.c_str());
            }
            return response;
        }

        case Ops::list_services: {
            sdk::broker_proto::Response response;
            response.json = registry_json(*registry);
            return response;
        }

        case Ops::ping: {
            sdk::broker_proto::Response response;
            response.detail = "pong";
            return response;
        }
    }

    return error_response(Code::unsupported,
                          "unsupported broker op " +
                              std::to_string(static_cast<std::uint32_t>(request.op)));
}

void serve_connection(ipc::Channel channel, const ipc::PeerCredentials& peer,
                      Registry* registry, const std::filesystem::path& root,
                      bool verbose) {
    // One request per connection.  The wait keeps an idle peer from stalling the
    // single-threaded accept loop.
    auto readable = ipc::wait_readable(channel.fd(), 5000);
    if (!readable) {
        if (verbose) {
            std::fprintf(stderr, "daysetta-broker: peer %s did not send a request\n",
                         ipc::peer_credentials_string(peer).c_str());
        }
        return;
    }

    std::vector<ipc::Fd> fds;
    auto datagram = channel.receive(&fds);
    if (!datagram) {
        if (verbose) {
            std::fprintf(stderr, "daysetta-broker: receive failed for peer %s: %s\n",
                         ipc::peer_credentials_string(peer).c_str(),
                         datagram.error().message.c_str());
        }
        return;
    }

    sdk::broker_proto::Response response;
    if (!fds.empty()) {
        response = error_response(Code::protocol,
                                  "broker request carried unexpected descriptors");
    } else if (datagram->size() > kMaxBrokerDatagram) {
        response = error_response(Code::too_large,
                                  "broker request exceeds 64 KiB");
    } else {
        bool ok = false;
        sdk::broker_proto::Request request =
            sdk::broker_proto::decode_request(*datagram, &ok);
        if (!ok) {
            response = error_response(Code::protocol, "malformed broker request");
        } else {
            response = handle_request(registry, root, request, peer, verbose);
        }
    }

    std::vector<std::uint8_t> encoded = sdk::broker_proto::encode_response(response);
    if (encoded.size() > kMaxBrokerDatagram) {
        encoded = sdk::broker_proto::encode_response(
            error_response(Code::too_large, "broker response exceeds 64 KiB"));
    }
    auto sent = channel.send(encoded);
    if (!sent && verbose) {
        std::fprintf(stderr, "daysetta-broker: reply failed for peer %s: %s\n",
                     ipc::peer_credentials_string(peer).c_str(),
                     sent.error().message.c_str());
    }
}

struct SocketCleanup {
    std::string path;
    ~SocketCleanup() {
        if (!path.empty()) ::unlink(path.c_str());
    }
};

struct SignalMaskRestore {
    sigset_t previous{};
    bool active = false;
    ~SignalMaskRestore() {
        if (active) ::sigprocmask(SIG_SETMASK, &previous, nullptr);
    }
};

}  // namespace

Result<void> run_broker(const BrokerConfig& config) {
    if (config.runtime_dir.empty()) {
        return Result<void>::fail(Code::invalid, "runtime directory is empty");
    }

    auto made = ipc::make_private_dir(config.runtime_dir);
    if (!made) return Result<void>::fail(made.error().code, made.error().message);

    std::error_code ec;
    std::filesystem::path root =
        std::filesystem::weakly_canonical(config.runtime_dir, ec);
    if (ec || root.empty()) {
        return Result<void>::fail(Code::invalid,
                                  "cannot canonicalize runtime directory: " +
                                      ec.message());
    }

    const std::string socket_path =
        (std::filesystem::path(config.runtime_dir) / "broker.sock").string();
    auto stale = ipc::unlink_if_exists(socket_path);
    if (!stale) {
        return Result<void>::fail(stale.error().code,
                                  "cannot remove stale broker socket: " +
                                      stale.error().message);
    }

    auto listener = ipc::Channel::listen(socket_path);
    if (!listener) {
        return Result<void>::fail(listener.error().code,
                                  "cannot listen on " + socket_path + ": " +
                                      listener.error().message);
    }
    SocketCleanup cleanup{socket_path};

    std::printf("%s\n", socket_path.c_str());
    std::fflush(stdout);

    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGTERM);
    SignalMaskRestore mask_restore;
    if (::sigprocmask(SIG_BLOCK, &mask, &mask_restore.previous) != 0) {
        return Result<void>::fail(Code::io,
                                  std::string("cannot block signals: ") +
                                      std::strerror(errno));
    }
    mask_restore.active = true;

    int signal_fd = ::signalfd(-1, &mask, SFD_CLOEXEC | SFD_NONBLOCK);
    if (signal_fd < 0) {
        return Result<void>::fail(Code::io,
                                  std::string("cannot create signalfd: ") +
                                      std::strerror(errno));
    }

    Registry registry;
    bool stopping = false;
    while (!stopping) {
        pollfd descriptors[2] = {
            {listener.value().fd(), POLLIN, 0},
            {signal_fd, POLLIN, 0},
        };
        int rc = ::poll(descriptors, 2, -1);
        if (rc < 0) {
            if (errno == EINTR) continue;
            ::close(signal_fd);
            return Result<void>::fail(Code::io,
                                      std::string("broker poll failed: ") +
                                          std::strerror(errno));
        }

        if ((descriptors[1].revents & POLLIN) != 0) {
            signalfd_siginfo info{};
            ssize_t got = ::read(signal_fd, &info, sizeof(info));
            if (got == static_cast<ssize_t>(sizeof(info)) && config.verbose) {
                std::fprintf(stderr, "daysetta-broker: signal %u, shutting down\n",
                             info.ssi_signo);
            }
            stopping = true;
            continue;
        }

        if ((descriptors[0].revents & POLLIN) != 0) {
            ipc::PeerCredentials peer;
            auto accepted = listener.value().accept(&peer);
            if (!accepted) {
                if (config.verbose) {
                    std::fprintf(stderr, "daysetta-broker: accept failed: %s\n",
                                 accepted.error().message.c_str());
                }
                continue;
            }
            if (peer.uid != static_cast<std::uint32_t>(::geteuid())) {
                if (config.verbose) {
                    std::fprintf(stderr,
                                 "daysetta-broker: rejecting peer %s (uid mismatch)\n",
                                 ipc::peer_credentials_string(peer).c_str());
                }
                continue;
            }
            if (config.verbose) {
                std::fprintf(stderr, "daysetta-broker: connection from %s\n",
                             ipc::peer_credentials_string(peer).c_str());
            }
            serve_connection(std::move(accepted.value()), peer, &registry, root,
                             config.verbose);
        }
    }

    ::close(signal_fd);
    if (config.verbose) {
        std::fprintf(stderr, "daysetta-broker: stopped, removing %s\n",
                     socket_path.c_str());
    }
    return Result<void>::ok();
}

}  // namespace ds
