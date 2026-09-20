#include "abi.hpp"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/sysmacros.h>

#include <csignal>
#include <cerrno>
#include <cstring>

namespace ds::darwin {

int errno_to_darwin(int e) {
    if (e == 0) return 0;
    if (e >= 1 && e <= 34 && e != EAGAIN) return e;  // EPERM..ERANGE share values (errno.h 1-34); EAGAIN is 11 on Linux
    switch (e) {
        case EAGAIN: return 35;
        case EINPROGRESS: return 36;
        case EALREADY: return 37;
        case ENOTSOCK: return 38;
        case EMSGSIZE: return 40;
        case EPROTOTYPE: return 41;
        case ENOPROTOOPT: return 42;
        case EPROTONOSUPPORT: return 43;
        case EOPNOTSUPP: return D_ENOTSUP;  // Linux ENOTSUP == EOPNOTSUPP
        case EAFNOSUPPORT: return 47;
        case EADDRINUSE: return 48;
        case EADDRNOTAVAIL: return 49;
        case ENETDOWN: return 50;
        case ENETUNREACH: return 51;
        case ECONNABORTED: return 53;
        case ECONNRESET: return 54;
        case ENOBUFS: return 55;
        case EISCONN: return 56;
        case ENOTCONN: return 57;
        case ETIMEDOUT: return D_ETIMEDOUT;
        case ECONNREFUSED: return 61;
        case ELOOP: return 62;
        case ENAMETOOLONG: return 63;
        case EHOSTUNREACH: return 65;
        case ENOTEMPTY: return 66;
        case EDQUOT: return 69;
        case ESTALE: return 70;
        case ENOLCK: return 77;
        case ENOSYS: return D_ENOSYS;
        case EOVERFLOW: return D_EOVERFLOW;
        case ECANCELED: return 89;
        case EIDRM: return 90;
        case ENOMSG: return 91;
        case EILSEQ: return 92;
        case EBADMSG: return 94;
        case EPROTO: return 100;
        case EDEADLK: return 11;  // Linux 35 == EDEADLK; Darwin EDEADLK 11
        default: return D_EIO;
    }
}

int open_flags_to_linux(int f, int* bad, bool* nofollow_any) {
    int out = 0, known = 0x3;
    if (nofollow_any) *nofollow_any = false;
    switch (f & 3) {
        case 0: out = O_RDONLY; break;
        case 1: out = O_WRONLY; break;
        case 2: out = O_RDWR; break;
        default: if (bad) *bad = 3; return -1;
    }
    struct M { int d, l; } map[] = {
        {0x4, O_NONBLOCK}, {0x8, O_APPEND}, {0x40, O_ASYNC}, {0x100, O_NOFOLLOW}, {0x200, O_CREAT},
        {0x400, O_TRUNC}, {0x800, O_EXCL}, {0x20000, O_NOCTTY}, {0x100000, O_DIRECTORY}, {0x1000000, O_CLOEXEC}};
    for (const M& m : map) {
        known |= m.d;
        if (f & m.d) out |= m.l;
    }
    known |= 0x20000000;  // O_NOFOLLOW_ANY: caller must use openat2(RESOLVE_NO_SYMLINKS)
    if (nofollow_any) *nofollow_any = f & 0x20000000;
    if (!nofollow_any && (f & 0x20000000)) known &= ~0x20000000;
    if (bad) *bad = f & ~known;
    return (f & ~known) ? -1 : out;
}

int prot_to_linux(int p) {
    return ((p & D_PROT_READ) ? PROT_READ : 0) | ((p & D_PROT_WRITE) ? PROT_WRITE : 0) | ((p & D_PROT_EXEC) ? PROT_EXEC : 0);
}

bool mmap_flags_to_linux(int f, int* out, const char** note) {
    int l = 0;
    if (note) *note = nullptr;
    if (f & D_MAP_SHARED) l |= MAP_SHARED;
    if (f & D_MAP_PRIVATE) l |= MAP_PRIVATE;
    if (f & D_MAP_FIXED) l |= MAP_FIXED;
    if (f & D_MAP_ANON) l |= MAP_ANONYMOUS;
    if (f & D_MAP_NORESERVE) l |= MAP_NORESERVE;
    if (f & D_MAP_32BIT) return false;  // Darwin <4G placement is not what Linux MAP_32BIT means
    if (f & D_MAP_JIT) { if (note) *note = "MAP_JIT accepted, no W^X toggling semantics"; }
    if (f & D_MAP_TPRO) { if (note) *note = "MAP_TPRO accepted, no TPRO protection"; }
    const int known = D_MAP_SHARED | D_MAP_PRIVATE | D_MAP_FIXED | D_MAP_ANON | D_MAP_NORESERVE | D_MAP_32BIT |
                      D_MAP_JIT | D_MAP_TPRO | D_MAP_NOCACHE | D_MAP_RESILIENT_CODESIGN | D_MAP_RESILIENT_MEDIA |
                      D_MAP_HASSEMAPHORE | D_MAP_NOEXTEND | D_MAP_UNIX03 | D_MAP_TRANSLATED_ALLOW_EXECUTE;
    if (f & ~known) return false;
    *out = l;
    return true;
}

void stat_to_darwin(const struct stat& s, DarwinStat64* d) {
    std::memset(d, 0, sizeof *d);
    d->st_dev = int32_t(s.st_dev);
    d->st_mode = uint16_t(s.st_mode);
    d->st_nlink = uint16_t(s.st_nlink);
    d->st_ino = s.st_ino;
    d->st_uid = s.st_uid;
    d->st_gid = s.st_gid;
    d->st_rdev = int32_t(s.st_rdev);
    d->st_atime_sec = s.st_atim.tv_sec;  d->st_atime_nsec = s.st_atim.tv_nsec;
    d->st_mtime_sec = s.st_mtim.tv_sec;  d->st_mtime_nsec = s.st_mtim.tv_nsec;
    d->st_ctime_sec = s.st_ctim.tv_sec;  d->st_ctime_nsec = s.st_ctim.tv_nsec;
    d->st_birthtime_sec = s.st_ctim.tv_sec;  d->st_birthtime_nsec = s.st_ctim.tv_nsec;
    d->st_size = s.st_size;
    d->st_blocks = s.st_blocks;
    d->st_blksize = int32_t(s.st_blksize);
}

}  // namespace ds::darwin

namespace ds::darwin {
namespace {
// index = Darwin signal number (signal.h), value = Linux number
const int kSig[32] = {0, SIGHUP, SIGINT, SIGQUIT, SIGILL, SIGTRAP, SIGABRT, 0, SIGFPE, SIGKILL, SIGBUS, SIGSEGV, SIGSYS,
                      SIGPIPE, SIGALRM, SIGTERM, SIGURG, SIGSTOP, SIGTSTP, SIGCONT, SIGCHLD, SIGTTIN, SIGTTOU, SIGIO,
                      SIGXCPU, SIGXFSZ, SIGVTALRM, SIGPROF, SIGWINCH, 0, SIGUSR1, SIGUSR2};
}  // namespace
int signal_to_linux(int s) { return s >= 1 && s <= 31 ? kSig[s] : 0; }
uint64_t sigset_to_linux(uint32_t d) {
    uint64_t l = 0;
    for (int s = 1; s <= 31; ++s)
        if ((d >> (s - 1) & 1) && kSig[s] && kSig[s] != SIGTRAP && kSig[s] != SIGSEGV && kSig[s] != SIGILL && kSig[s] != SIGBUS)
            l |= uint64_t(1) << (kSig[s] - 1);
    return l;
}
uint32_t sigset_to_darwin(uint64_t l) {
    uint32_t d = 0;
    for (int s = 1; s <= 31; ++s)
        if (kSig[s] && (l >> (kSig[s] - 1) & 1)) d |= 1u << (s - 1);
    return d;
}
}  // namespace ds::darwin
