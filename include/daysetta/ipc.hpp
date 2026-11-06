// ds-ipc: framed Unix-socket transport with descriptor passing and peer identity.
//
// The transport is deliberately below window semantics.  It knows a byte string and
// a set of file descriptors, nothing else.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "daysetta/result.hpp"

namespace ds::ipc {

constexpr std::size_t kMaxMessage = 1u << 20;  // 1 MiB
constexpr std::size_t kMaxFds = 64;

class Fd {
public:
    Fd() = default;
    explicit Fd(int fd) : fd_(fd) {}
    ~Fd();

    Fd(const Fd&) = delete;
    Fd& operator=(const Fd&) = delete;
    Fd(Fd&& other) noexcept;
    Fd& operator=(Fd&& other) noexcept;

    int get() const { return fd_; }
    bool valid() const { return fd_ >= 0; }
    int release() {
        int fd = fd_;
        fd_ = -1;
        return fd;
    }
    void reset(int fd = -1);

private:
    int fd_ = -1;
};

struct PeerCredentials {
    std::uint32_t pid = 0;
    std::uint32_t uid = 0;
    std::uint32_t gid = 0;
};

// A connected Unix SOCK_SEQPACKET channel.  One send() is one datagram, which makes
// the framing header and the descriptor list arrive together and atomically.
class Channel {
public:
    Channel() = default;
    explicit Channel(int fd) : fd_(fd) {}
    ~Channel();

    Channel(const Channel&) = delete;
    Channel& operator=(const Channel&) = delete;
    Channel(Channel&& other) noexcept;
    Channel& operator=(Channel&& other) noexcept;

    static Result<Channel> connect(const std::string& path);
    static Result<Channel> listen(const std::string& path, int backlog = 32);
    Result<Channel> accept(PeerCredentials* peer = nullptr);

    Result<void> send(std::span<const std::uint8_t> bytes,
                      std::span<const int> fds = {});
    // Returns one datagram.  The buffer limit is kMaxMessage; oversized datagrams are
    // reported as Code::too_large and consumed.
    Result<std::vector<std::uint8_t>> receive(std::vector<Fd>* fds = nullptr);

    int fd() const { return fd_; }
    bool valid() const { return fd_ >= 0; }
    void close();

private:
    int fd_ = -1;
};

// Filesystem helpers used by the broker and session.
Result<void> make_private_dir(const std::string& path);
Result<void> unlink_if_exists(const std::string& path);
Result<void> set_nonblocking(int fd, bool enabled);
Result<void> set_cloexec(int fd, bool enabled);

// Wait for readability.  timeout_ms < 0 means block.
Result<void> wait_readable(int fd, int timeout_ms);

std::string peer_credentials_string(const PeerCredentials& peer);

}  // namespace ds::ipc
