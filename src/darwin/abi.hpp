// Darwin <-> Linux ABI translation helpers (pure functions, unit-tested).
//
// Every constant is from the Darwin headers of the Darling xnu snapshot
// (/home/ubuntu/darling/src/external/xnu/bsd/sys/{errno,fcntl,mman,stat}.h); the Linux side uses
// the host headers.  Evidence class SOURCE for the numbers, tests in tests/test_darwin.cpp.
#pragma once

#include <sys/stat.h>

#include <cstdint>

namespace ds::darwin {

// ---- errno (bsd/sys/errno.h) ----
constexpr int D_EPERM = 1, D_ENOENT = 2, D_EBADF = 9, D_ENOMEM = 12, D_EACCES = 13, D_EFAULT = 14, D_EINVAL = 22,
              D_ENOTTY = 25, D_EAGAIN = 35, D_ENOTSUP = 45, D_ENOSYS = 78, D_EIO = 5, D_ESRCH = 3, D_ERANGE = 34,
              D_EOVERFLOW = 84, D_ENOATTR = 93, D_ETIMEDOUT = 60, D_EEXIST = 17, D_ENOTDIR = 20;
// Linux errno -> Darwin errno.  Unmapped values return D_EIO (reported by ds-run's trace).
int errno_to_darwin(int linux_errno);

// ---- open(2) flags (bsd/sys/fcntl.h:98-186) ----
// Returns -1 (and sets *bad to the offending Darwin bits) when a flag has no faithful Linux
// equivalent (O_SHLOCK, O_EXLOCK, O_EVTONLY, O_SYMLINK, O_CLOFORK, ...).
int open_flags_to_linux(int darwin_flags, int* bad, bool* nofollow_any = nullptr);

// ---- signals (bsd/sys/signal.h) ----
// Darwin -> Linux signal number, 0 when Linux has no equivalent (SIGEMT, SIGINFO).
int signal_to_linux(int darwin_sig);
// Darwin sigset_t is a uint32 bitmask (bit n-1 = signal n).  Signals the execution engine owns
// (SIGTRAP, SIGSEGV, SIGILL, SIGBUS) are removed from a *blocked* set: the guest cannot mask them.
uint64_t sigset_to_linux(uint32_t darwin_set);
uint32_t sigset_to_darwin(uint64_t linux_set);
constexpr int D_SIG_BLOCK = 1, D_SIG_UNBLOCK = 2, D_SIG_SETMASK = 3;

// ---- mmap (bsd/sys/mman.h:101-165) ----
constexpr int D_PROT_READ = 1, D_PROT_WRITE = 2, D_PROT_EXEC = 4;
constexpr int D_MAP_SHARED = 0x1, D_MAP_PRIVATE = 0x2, D_MAP_FIXED = 0x10, D_MAP_NORESERVE = 0x40,
              D_MAP_NOCACHE = 0x400, D_MAP_JIT = 0x800, D_MAP_ANON = 0x1000, D_MAP_RESILIENT_CODESIGN = 0x2000,
              D_MAP_RESILIENT_MEDIA = 0x4000, D_MAP_32BIT = 0x8000, D_MAP_TRANSLATED_ALLOW_EXECUTE = 0x20000,
              D_MAP_UNIX03 = 0x40000, D_MAP_TPRO = 0x80000, D_MAP_HASSEMAPHORE = 0x200, D_MAP_NOEXTEND = 0x100;
int prot_to_linux(int darwin_prot);
// Returns false when a flag cannot be honoured.  MAP_JIT/MAP_TPRO are accepted as documented
// no-ops on mapping semantics (flagged in *note); hints (NOCACHE, RESILIENT_*, HASSEMAPHORE,
// NOEXTEND, UNIX03, NORESERVE-as-is) are dropped.
bool mmap_flags_to_linux(int darwin_flags, int* linux_flags, const char** note);

// ---- stat (bsd/sys/stat.h:156-176, __DARWIN_STRUCT_STAT64) ----
struct DarwinStat64 {
    int32_t st_dev;
    uint16_t st_mode;
    uint16_t st_nlink;
    uint64_t st_ino;
    uint32_t st_uid;
    uint32_t st_gid;
    int32_t st_rdev;
    int64_t st_atime_sec, st_atime_nsec;
    int64_t st_mtime_sec, st_mtime_nsec;
    int64_t st_ctime_sec, st_ctime_nsec;
    int64_t st_birthtime_sec, st_birthtime_nsec;
    int64_t st_size;
    int64_t st_blocks;
    int32_t st_blksize;
    uint32_t st_flags;
    uint32_t st_gen;
    int32_t st_lspare;
    int64_t st_qspare[2];
};
static_assert(sizeof(DarwinStat64) == 144, "Darwin struct stat64 is 144 bytes");

// Linux gives no birth time here: st_birthtime is reported equal to st_ctime (documented).
void stat_to_darwin(const struct stat& ls, DarwinStat64* ds);
// Darwin st_mode type/permission bits are the same octal values as Linux (S_IF* in sys/stat.h).

}  // namespace ds::darwin
