#include "daysetta/ipc.hpp"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

namespace ds::ipc {
namespace {

Error errno_error(Code code, const char* what) {
    return Error{code, std::string(what) + ": " + std::strerror(errno)};
}

}  // namespace

Fd::~Fd() { reset(); }

Fd::Fd(Fd&& other) noexcept : fd_(other.fd_) { other.fd_ = -1; }

Fd& Fd::operator=(Fd&& other) noexcept {
    if (this != &other) {
        reset(other.fd_);
        other.fd_ = -1;
    }
    return *this;
}

void Fd::reset(int fd) {
    if (fd_ >= 0) ::close(fd_);
    fd_ = fd;
}

Channel::~Channel() { close(); }

Channel::Channel(Channel&& other) noexcept : fd_(other.fd_) { other.fd_ = -1; }

Channel& Channel::operator=(Channel&& other) noexcept {
    if (this != &other) {
        close();
        fd_ = other.fd_;
        other.fd_ = -1;
    }
    return *this;
}

void Channel::close() {
    if (fd_ >= 0) ::close(fd_);
    fd_ = -1;
}

Result<Channel> Channel::connect(const std::string& path) {
    sockaddr_un addr{};
    if (path.size() >= sizeof(addr.sun_path)) {
        return Result<Channel>::fail(Code::invalid, "socket path too long");
    }
    int fd = ::socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
    if (fd < 0) return Result<Channel>::fail(Code::io, std::strerror(errno));

    addr.sun_family = AF_UNIX;
    std::memcpy(addr.sun_path, path.c_str(), path.size() + 1);
    for (;;) {
        if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0) {
            return Result<Channel>::ok(Channel(fd));
        }
        if (errno == EINTR) continue;
        Code code = (errno == ENOENT) ? Code::not_found : Code::io;
        Error error = errno_error(code, "connect");
        ::close(fd);
        return Result<Channel>::fail(error.code, error.message);
    }
}

Result<Channel> Channel::listen(const std::string& path, int backlog) {
    sockaddr_un addr{};
    if (path.size() >= sizeof(addr.sun_path)) {
        return Result<Channel>::fail(Code::invalid, "socket path too long");
    }
    int fd = ::socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
    if (fd < 0) return Result<Channel>::fail(Code::io, std::strerror(errno));

    addr.sun_family = AF_UNIX;
    std::memcpy(addr.sun_path, path.c_str(), path.size() + 1);
    ::unlink(path.c_str());
    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        Error error = errno_error(Code::io, "bind");
        ::close(fd);
        return Result<Channel>::fail(error.code, error.message);
    }
    if (::chmod(path.c_str(), 0600) != 0) {
        Error error = errno_error(Code::io, "chmod");
        ::close(fd);
        return Result<Channel>::fail(error.code, error.message);
    }
    if (::listen(fd, backlog) != 0) {
        Error error = errno_error(Code::io, "listen");
        ::close(fd);
        return Result<Channel>::fail(error.code, error.message);
    }
    return Result<Channel>::ok(Channel(fd));
}

Result<Channel> Channel::accept(PeerCredentials* peer) {
    int fd = ::accept4(fd_, nullptr, nullptr, SOCK_CLOEXEC);
    if (fd < 0) {
        if (errno == EINTR) return accept(peer);
        return Result<Channel>::fail(Code::io, std::strerror(errno));
    }
    if (peer != nullptr) {
        ucred cred{};
        socklen_t len = sizeof(cred);
        if (::getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &cred, &len) == 0) {
            peer->pid = static_cast<std::uint32_t>(cred.pid);
            peer->uid = cred.uid;
            peer->gid = cred.gid;
        } else {
            *peer = PeerCredentials{};
        }
    }
    return Result<Channel>::ok(Channel(fd));
}

Result<void> Channel::send(std::span<const std::uint8_t> bytes,
                           std::span<const int> fds) {
    if (fd_ < 0) return Result<void>::fail(Code::state, "channel closed");
    if (bytes.size() > kMaxMessage) {
        return Result<void>::fail(Code::too_large, "message exceeds kMaxMessage");
    }
    if (fds.size() > kMaxFds) {
        return Result<void>::fail(Code::invalid, "too many descriptors");
    }

    iovec iov{};
    iov.iov_base = const_cast<std::uint8_t*>(bytes.data());
    iov.iov_len = bytes.size();

    msghdr msg{};
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;

    std::vector<std::uint8_t> control;
    if (!fds.empty()) {
        control.resize(CMSG_SPACE(fds.size() * sizeof(int)));
        msg.msg_control = control.data();
        msg.msg_controllen = control.size();
        cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);
        cmsg->cmsg_level = SOL_SOCKET;
        cmsg->cmsg_type = SCM_RIGHTS;
        cmsg->cmsg_len = CMSG_LEN(fds.size() * sizeof(int));
        std::memcpy(CMSG_DATA(cmsg), fds.data(), fds.size() * sizeof(int));
    }

    ssize_t sent;
    do {
        sent = ::sendmsg(fd_, &msg, MSG_NOSIGNAL);
    } while (sent < 0 && errno == EINTR);
    if (sent < 0) return Result<void>::fail(Code::io, std::strerror(errno));
    if (static_cast<std::size_t>(sent) != bytes.size()) {
        return Result<void>::fail(Code::protocol, "short datagram send");
    }
    return Result<void>::ok();
}

Result<std::vector<std::uint8_t>> Channel::receive(std::vector<Fd>* fds) {
    if (fd_ < 0) return Result<std::vector<std::uint8_t>>::fail(Code::state, "channel closed");

    std::vector<std::uint8_t> bytes(kMaxMessage + 1);
    std::vector<std::uint8_t> control(CMSG_SPACE(kMaxFds * sizeof(int)));

    iovec iov{};
    iov.iov_base = bytes.data();
    iov.iov_len = bytes.size();

    msghdr msg{};
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = control.data();
    msg.msg_controllen = control.size();

    ssize_t got;
    do {
        got = ::recvmsg(fd_, &msg, MSG_CMSG_CLOEXEC);
    } while (got < 0 && errno == EINTR);
    if (got < 0) return Result<std::vector<std::uint8_t>>::fail(Code::io, std::strerror(errno));

    std::vector<Fd> received;
    for (cmsghdr* cmsg = CMSG_FIRSTHDR(&msg); cmsg != nullptr;
         cmsg = CMSG_NXTHDR(&msg, cmsg)) {
        if (cmsg->cmsg_level != SOL_SOCKET || cmsg->cmsg_type != SCM_RIGHTS) continue;
        std::size_t count = (cmsg->cmsg_len - CMSG_LEN(0)) / sizeof(int);
        const int* descriptors = reinterpret_cast<const int*>(CMSG_DATA(cmsg));
        for (std::size_t i = 0; i < count; ++i) received.emplace_back(descriptors[i]);
    }

    if ((msg.msg_flags & MSG_TRUNC) != 0 || static_cast<std::size_t>(got) > kMaxMessage) {
        return Result<std::vector<std::uint8_t>>::fail(Code::too_large,
                                                       "datagram exceeds kMaxMessage");
    }
    if (received.size() > kMaxFds) {
        return Result<std::vector<std::uint8_t>>::fail(Code::too_large,
                                                       "too many descriptors");
    }
    if (fds != nullptr) {
        *fds = std::move(received);
    }
    bytes.resize(static_cast<std::size_t>(got));
    return Result<std::vector<std::uint8_t>>::ok(std::move(bytes));
}

Result<void> make_private_dir(const std::string& path) {
    std::error_code ec;
    std::filesystem::create_directories(path, ec);
    if (ec) {
        return Result<void>::fail(Code::io, "create_directories: " + ec.message());
    }
    struct stat st{};
    if (::lstat(path.c_str(), &st) != 0) {
        return Result<void>::fail(Code::io, std::strerror(errno));
    }
    if (!S_ISDIR(st.st_mode)) {
        return Result<void>::fail(Code::invalid, path + " is not a directory");
    }
    if (::chmod(path.c_str(), 0700) != 0) {
        return Result<void>::fail(Code::io, std::strerror(errno));
    }
    return Result<void>::ok();
}

Result<void> unlink_if_exists(const std::string& path) {
    if (::unlink(path.c_str()) != 0 && errno != ENOENT) {
        return Result<void>::fail(Code::io, std::strerror(errno));
    }
    return Result<void>::ok();
}

Result<void> set_nonblocking(int fd, bool enabled) {
    int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags < 0) return Result<void>::fail(Code::io, std::strerror(errno));
    flags = enabled ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK);
    if (::fcntl(fd, F_SETFL, flags) != 0) {
        return Result<void>::fail(Code::io, std::strerror(errno));
    }
    return Result<void>::ok();
}

Result<void> set_cloexec(int fd, bool enabled) {
    int flags = ::fcntl(fd, F_GETFD, 0);
    if (flags < 0) return Result<void>::fail(Code::io, std::strerror(errno));
    flags = enabled ? (flags | FD_CLOEXEC) : (flags & ~FD_CLOEXEC);
    if (::fcntl(fd, F_SETFD, flags) != 0) {
        return Result<void>::fail(Code::io, std::strerror(errno));
    }
    return Result<void>::ok();
}

Result<void> wait_readable(int fd, int timeout_ms) {
    pollfd pfd{};
    pfd.fd = fd;
    pfd.events = POLLIN;
    for (;;) {
        int rc = ::poll(&pfd, 1, timeout_ms);
        if (rc < 0) {
            if (errno == EINTR) continue;
            return Result<void>::fail(Code::io, std::strerror(errno));
        }
        if (rc == 0) return Result<void>::fail(Code::timeout, "poll timed out");
        if ((pfd.revents & (POLLERR | POLLNVAL)) != 0) {
            return Result<void>::fail(Code::state, "socket error");
        }
        return Result<void>::ok();
    }
}

std::string peer_credentials_string(const PeerCredentials& peer) {
    return "pid=" + std::to_string(peer.pid) + " uid=" + std::to_string(peer.uid) +
           " gid=" + std::to_string(peer.gid);
}

}  // namespace ds::ipc
