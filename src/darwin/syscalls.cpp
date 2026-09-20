#include "syscalls.hpp"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/random.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <csignal>
#include <pthread.h>
#include <sys/uio.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <unordered_map>
#include <vector>

#include "abi.hpp"
#include "commpage.hpp"

namespace ds::darwin {

namespace {
struct NameEntry { int n; const char* name; };
#include "sysnames.inc"

constexpr uint64_t kDarwinPage = 0x4000;
thread_local uint64_t g_tsd = 0;

// Darwin kern_return_t / mach_msg values: osfmk/mach/kern_return.h, message.h (Darling xnu).
constexpr int64_t KERN_SUCCESS_ = 0, KERN_INVALID_ADDRESS_ = 1, KERN_NO_SPACE_ = 3, KERN_INVALID_ARGUMENT_ = 4,
                  KERN_FAILURE_ = 5, KERN_NOT_SUPPORTED_ = 46;
constexpr int64_t MACH_SEND_INVALID_DEST_ = 0x10000003, MACH_RCV_INVALID_NAME_ = 0x10004002;
constexpr uint32_t VM_FLAGS_ANYWHERE_ = 1, MACH_SEND_MSG_ = 1;

// copyout with EFAULT semantics (process_vm_writev on ourselves never faults the host).
bool copyout(uint64_t dst, const void* src, size_t n) {
    struct iovec l{const_cast<void*>(src), n}, r{reinterpret_cast<void*>(dst), n};
    return process_vm_writev(getpid(), &l, 1, &r, 1, 0) == ssize_t(n);
}
SysRet err(int e) { SysRet r; r.err = e; return r; }
SysRet ok(int64_t v = 0) { SysRet r; r.v = v; return r; }
SysRet from_host(long rv) { return rv < 0 ? err(errno_to_darwin(errno)) : ok(rv); }
template <typename T> T* P(uint64_t a) { return reinterpret_cast<T*>(a); }
uint64_t round_up(uint64_t v) { return (v + kDarwinPage - 1) & ~(kDarwinPage - 1); }

SysRet stub(SysContext& c, const char* name, const char* why, int e) {
    std::lock_guard<std::mutex> lk(c.mu);
    c.stubbed.emplace(name, why);
    return err(e);
}

// ---- process / identity ----
SysRet s_exit(SysContext& c, const uint64_t a[8]) {
    if (c.trace) print_summary(c, stderr);
    std::fflush(stdout);
    _exit(int(a[0]));
}
SysRet s_abort_with_payload(SysContext& c, const uint64_t a[8]) {
    std::fprintf(stderr, "ds-run: abort_with_payload namespace=%u code=0x%llx reason=%s\n", uint32_t(a[0]),
                 (unsigned long long)a[1], a[4] ? P<const char>(a[4]) : "(none)");
    if (c.trace) print_summary(c, stderr);
    _exit(134);
}
SysRet s_getpid(SysContext&, const uint64_t*) { return ok(getpid()); }
SysRet s_getuid(SysContext&, const uint64_t*) { return ok(getuid()); }
SysRet s_geteuid(SysContext&, const uint64_t*) { return ok(geteuid()); }
SysRet s_getgid(SysContext&, const uint64_t*) { return ok(getgid()); }
SysRet s_getegid(SysContext&, const uint64_t*) { return ok(getegid()); }
SysRet s_issetugid(SysContext&, const uint64_t*) { return ok(getuid() != geteuid() || getgid() != getegid()); }
SysRet s_thread_selfid(SysContext&, const uint64_t*) { return ok(syscall(SYS_gettid)); }
// csops: same answers as Darling's emulation (csops.c): STATUS -> CS_VALID, blobs zeroed, rest ENOSYS.
constexpr uint32_t CS_OPS_STATUS_ = 0, CS_VALID_ = 0x1;  // bsd/sys/codesign.h
SysRet s_csops(SysContext&, const uint64_t a[8]) {
    switch (uint32_t(a[1])) {
        case CS_OPS_STATUS_:
            if (!a[2] || a[3] < 4) return err(D_EINVAL);
            *P<uint32_t>(a[2]) = CS_VALID_;
            return ok();
        case 1 /*CS_OPS_MARKINVALID*/: return err(D_EPERM);
        default: return err(D_ENOSYS);
    }
}
SysRet s_getentropy(SysContext&, const uint64_t a[8]) {
    if (a[1] > 256) return err(D_EIO);  // xnu getentropy: size limit 256 (EIO)
    ssize_t n = getrandom(P<void>(a[0]), a[1], 0);
    return n == ssize_t(a[1]) ? ok() : err(D_EIO);
}
SysRet s_shared_region_check_np(SysContext& c, const uint64_t a[8]) {
    // xnu: writes the shared region start and returns 0, ENOMEM when no region is mapped.
    if (!c.shared_cache_base) return err(D_ENOMEM);
    if (a[0] && !copyout(a[0], &c.shared_cache_base, 8)) return err(D_EFAULT);
    return ok();
}
SysRet s_bsdthread_register(SysContext&, const uint64_t a[8]) {
    // Single-threaded for now: accept and report no optional features (Darling returns 0 too).
    (void)a;
    return ok(0);
}
SysRet s_mac_syscall(SysContext& c, const uint64_t*) {
    return stub(c, "__mac_syscall", "no MAC policy loaded: ENOSYS like a kernel without that policy", D_ENOSYS);
}
SysRet s_workq_kernreturn(SysContext& c, const uint64_t*) {
    return stub(c, "workq_kernreturn", "no kernel workqueue: ENOTSUP", D_ENOTSUP);
}

// csrctl: xnu bsd/kern/kern_csr.c:345-405 -- op 0 CSR_CHECK(mask) / op 1 GET_ACTIVE_CONFIG (bsd/sys/csr.h:93-96).
// We report SIP fully enabled (config 0), the retail default: CHECK(mask) fails with EPERM for any non-empty mask.
SysRet s_csrctl(SysContext&, const uint64_t a[8]) {
    if (!a[1] || a[2] != 4) return err(D_EINVAL);
    if (uint32_t(a[0]) == 0) return *P<uint32_t>(a[1]) == 0 ? ok() : err(D_EPERM);
    if (uint32_t(a[0]) == 1) { *P<uint32_t>(a[1]) = 0; return ok(); }
    return err(D_ENOSYS);
}

// ---- sysctl ----
// Static OIDs from xnu bsd/sys/sysctl.h (CTL_KERN 1, CTL_HW 6, KERN_* / HW_* as cited per row);
// CTL_UNSPEC (0) is the meta node: {0,3} name2oid (bsd/kern/kern_newsysctl.c).  Dynamic OIDs
// (xnu allocates them from OID_AUTO_START=100, sysctl.h:202) are ours, opaque to the guest.
struct SysctlEnt {
    const char* name;
    std::vector<int> oid;
    int kind;  // 0 string, 1 int32, 2 int64
    std::string s;
    int64_t i;
};
std::vector<SysctlEnt>& sysctls() {
    static std::vector<SysctlEnt> t = {
        {"kern.bootargs", {1, 100}, 0, "", 0},  // no boot-args: retail Apple silicon default
    };
    return t;
}
SysRet sysctl_copy(const SysctlEnt& e, void* old, uint64_t* oldlenp) {
    uint8_t buf[sizeof(int64_t)];
    const void* src; size_t n;
    if (e.kind == 0) { src = e.s.c_str(); n = e.s.size() + 1; }
    else if (e.kind == 1) { int32_t v = int32_t(e.i); std::memcpy(buf, &v, 4); src = buf; n = 4; }
    else { std::memcpy(buf, &e.i, 8); src = buf; n = 8; }
    if (!oldlenp) return ok();
    if (!old) { *oldlenp = n; return ok(); }
    const size_t cap = *oldlenp;
    std::memcpy(old, src, cap < n ? cap : n);
    *oldlenp = cap < n ? cap : n;
    return cap < n ? err(D_ENOMEM) : ok();
}
SysRet sysctl_unknown(SysContext& c, const std::string& what) {
    std::lock_guard<std::mutex> lk(c.mu);
    ++c.unimplemented["sysctl " + what];
    std::fprintf(stderr, "UNIMPLEMENTED sysctl %s\n", what.c_str());
    return err(D_ENOENT);
}
SysRet s_sysctl(SysContext& c, const uint64_t a[8]) {  // (name, namelen, old, oldlenp, new, newlen)
    const int* mib = P<const int>(a[0]);
    const unsigned n = unsigned(a[1]);
    if (n >= 2 && mib[0] == 0 && mib[1] == 3) {  // name2oid
        if (!a[4]) return err(D_EINVAL);
        std::string name(P<const char>(a[4]), a[5]);
        for (auto& e : sysctls())
            if (name == e.name) {
                uint64_t* lp = P<uint64_t>(a[3]);
                const size_t bytes = e.oid.size() * 4;
                if (a[2]) { if (*lp < bytes) return err(D_ENOMEM); std::memcpy(P<void>(a[2]), e.oid.data(), bytes); }
                *lp = bytes;
                return ok();
            }
        return sysctl_unknown(c, "name2oid(" + name + ")");
    }
    for (auto& e : sysctls())
        if (e.oid.size() == n && std::equal(e.oid.begin(), e.oid.end(), mib)) return sysctl_copy(e, P<void>(a[2]), P<uint64_t>(a[3]));
    std::string m;
    for (unsigned i = 0; i < n; ++i) m += (i ? "." : "") + std::to_string(mib[i]);
    return sysctl_unknown(c, "mib " + m);
}
SysRet s_sysctlbyname(SysContext& c, const uint64_t a[8]) {  // (name, namelen, old, oldlenp, new, newlen)
    std::string name(P<const char>(a[0]), a[1]);
    for (auto& e : sysctls())
        if (name == e.name) return sysctl_copy(e, P<void>(a[2]), P<uint64_t>(a[3]));
    return sysctl_unknown(c, name);
}

// fsgetpath(buf, bufsize, fsid_t*, objid): path of a (fsid, objid) pair; returns the length including NUL
// (xnu bsd/vfs/vfs_syscalls.c fsgetpath_internal -> build_path).  Only ids we registered resolve.
SysRet s_fsgetpath(SysContext& c, const uint64_t a[8]) {
    if (!a[0] || !a[2]) return err(D_EINVAL);
    const int32_t dev = P<const int32_t>(a[2])[0];
    auto it = c.fileids.find({dev, a[3]});
    if (it == c.fileids.end()) return err(D_ENOENT);
    if (it->second.size() + 1 > a[1]) return err(D_ERANGE);
    std::memcpy(P<void>(a[0]), it->second.c_str(), it->second.size() + 1);
    return ok(int64_t(it->second.size() + 1));
}

// ---- file I/O ----
SysRet s_write(SysContext&, const uint64_t a[8]) { return from_host(write(int(a[0]), P<void>(a[1]), a[2])); }
SysRet s_read(SysContext&, const uint64_t a[8]) { return from_host(read(int(a[0]), P<void>(a[1]), a[2])); }
SysRet s_pread(SysContext&, const uint64_t a[8]) { return from_host(pread(int(a[0]), P<void>(a[1]), a[2], off_t(a[3]))); }
SysRet s_writev(SysContext&, const uint64_t a[8]) {
    // struct iovec is {void*, size_t} on both ABIs
    return from_host(writev(int(a[0]), P<const struct iovec>(a[1]), int(a[2])));
}
SysRet s_close(SysContext&, const uint64_t a[8]) { return from_host(close(int(a[0]))); }
SysRet s_lseek(SysContext&, const uint64_t a[8]) { return from_host(lseek(int(a[0]), off_t(a[1]), int(a[2]))); }
SysRet do_openat(int dfd, const char* path, int dflags, int mode) {
    int bad = 0;
    bool noany = false;
    const int fl = open_flags_to_linux(dflags, &bad, &noany);
    if (fl < 0) return err(D_EINVAL);
    if (noany) {  // O_NOFOLLOW_ANY: no symlink anywhere in the path -> openat2 RESOLVE_NO_SYMLINKS
        struct { uint64_t flags, mode, resolve; } how = {uint64_t(fl), uint64_t(mode), 0x04 /*RESOLVE_NO_SYMLINKS*/};
        return from_host(syscall(SYS_openat2, dfd, path, &how, sizeof how));
    }
    return from_host(openat(dfd, path, fl, mode_t(mode)));
}
constexpr int D_AT_FDCWD_ = -2;  // bsd/sys/fcntl.h AT_FDCWD
SysRet s_open(SysContext&, const uint64_t a[8]) { return do_openat(AT_FDCWD, P<const char>(a[0]), int(a[1]), int(a[2])); }
SysRet s_openat(SysContext&, const uint64_t a[8]) {
    return do_openat(int(a[0]) == D_AT_FDCWD_ ? AT_FDCWD : int(a[0]), P<const char>(a[1]), int(a[2]), int(a[3]));
}
SysRet s_sigprocmask(SysContext&, const uint64_t a[8]) {  // (how, mask*, omask*) -- sigset_t is uint32 here
    sigset_t cur, ns;
    sigemptyset(&ns);
    pthread_sigmask(SIG_BLOCK, nullptr, &cur);
    uint32_t dold = 0;
    for (int s = 1; s <= 31; ++s) { int l = signal_to_linux(s); if (l && sigismember(&cur, l)) dold |= 1u << (s - 1); }
    if (a[1]) {
        const uint64_t lm = sigset_to_linux(*P<uint32_t>(a[1]));
        for (int l = 1; l <= 64; ++l) if (lm >> (l - 1) & 1) sigaddset(&ns, l);
        int how;
        switch (int(a[0])) {
            case D_SIG_BLOCK: how = SIG_BLOCK; break;
            case D_SIG_UNBLOCK: how = SIG_UNBLOCK; break;
            case D_SIG_SETMASK: how = SIG_SETMASK; break;
            default: return err(D_EINVAL);
        }
        pthread_sigmask(how, &ns, nullptr);
    }
    if (a[2]) *P<uint32_t>(a[2]) = dold;
    return ok();
}
SysRet put_stat(int rc, const struct stat& ls, uint64_t ub) {
    if (rc < 0) return err(errno_to_darwin(errno));
    DarwinStat64 d;
    stat_to_darwin(ls, &d);
    std::memcpy(P<void>(ub), &d, sizeof d);
    return ok();
}
SysRet s_stat64(SysContext&, const uint64_t a[8]) { struct stat s; int rc = stat(P<const char>(a[0]), &s); return put_stat(rc, s, a[1]); }
SysRet s_lstat64(SysContext&, const uint64_t a[8]) { struct stat s; int rc = lstat(P<const char>(a[0]), &s); return put_stat(rc, s, a[1]); }
SysRet s_fstat64(SysContext&, const uint64_t a[8]) { struct stat s; int rc = fstat(int(a[0]), &s); return put_stat(rc, s, a[1]); }
SysRet s_fstatat64(SysContext&, const uint64_t a[8]) {
    constexpr int D_AT_FDCWD = -2, D_AT_SYMLINK_NOFOLLOW = 0x20;  // bsd/sys/fcntl.h
    struct stat s;
    int fl = (int(a[3]) & D_AT_SYMLINK_NOFOLLOW) ? AT_SYMLINK_NOFOLLOW : 0;
    int rc = fstatat(int(a[0]) == D_AT_FDCWD ? AT_FDCWD : int(a[0]), P<const char>(a[1]), &s, fl);
    return put_stat(rc, s, a[2]);
}
SysRet s_access(SysContext&, const uint64_t a[8]) { return from_host(access(P<const char>(a[0]), int(a[1]))); }  // R_OK/W_OK/X_OK equal

// fcntl: bsd/sys/fcntl.h:279-402 command numbers.
SysRet s_fcntl(SysContext& c, const uint64_t a[8]) {
    const int fd = int(a[0]), cmd = int(a[1]);
    switch (cmd) {
        case 1: case 2: return from_host(fcntl(fd, cmd, int(a[2])));        // F_GETFD / F_SETFD
        case 0: return from_host(fcntl(fd, F_DUPFD, int(a[2])));
        case 67: return from_host(fcntl(fd, F_DUPFD_CLOEXEC, int(a[2])));   // F_DUPFD_CLOEXEC
        case 3: {                                                            // F_GETFL: translate back
            int f = fcntl(fd, F_GETFL);
            if (f < 0) return err(errno_to_darwin(errno));
            int d = f & O_ACCMODE;
            if (f & O_NONBLOCK) d |= 0x4;
            if (f & O_APPEND) d |= 0x8;
            return ok(d);
        }
        case 4: {                                                            // F_SETFL
            int bad, f = open_flags_to_linux(int(a[2]) & (0x4 | 0x8), &bad);
            return from_host(fcntl(fd, F_SETFL, f));
        }
        case 50: {                                                           // F_GETPATH: buf of MAXPATHLEN
            char l[64];
            std::snprintf(l, sizeof l, "/proc/self/fd/%d", fd);
            ssize_t n = readlink(l, P<char>(a[2]), 1023);
            if (n < 0) return err(errno_to_darwin(errno));
            P<char>(a[2])[n] = 0;
            return ok();
        }
        case 98:  // F_CHECK_LV: library validation -- accepted (no code-signature enforcement here)
            { std::lock_guard<std::mutex> lk(c.mu); c.stubbed.emplace("fcntl(F_CHECK_LV)", "accepted: no library validation"); }
            return ok();
        case 61: case 97: case 83: case 78: case 103:  // F_ADDFILESIGS*, F_FINDSIGS: no signature store
            return stub(c, "fcntl(F_ADDFILESIGS*)", "no code-signature store: EINVAL (as a file without LC_CODE_SIGNATURE)", D_EINVAL);
        case 48: case 45: case 40: case 51: case 85:  // F_NOCACHE F_RDAHEAD F_FLUSH_DATA F_FULLFSYNC F_BARRIERFSYNC: hints
            return ok();
        default:
            return err(D_EINVAL);
    }
}

// ---- memory ----
SysRet s_mmap(SysContext&, const uint64_t a[8]) {
    int lf, prot = prot_to_linux(int(a[2]));
    const char* note = nullptr;
    if (!mmap_flags_to_linux(int(a[3]), &lf, &note)) return err(D_EINVAL);
    const uint64_t len = round_up(a[1]);
    if (a[1] == 0) return err(D_EINVAL);
    int fd = int(a[4]);
    if (lf & MAP_ANONYMOUS) fd = -1;
    void* r;
    if (lf & MAP_FIXED) {
        r = mmap(P<void>(a[0]), len, prot, lf, fd, off_t(a[5]));
    } else {
        // Darwin mappings are 16 KiB aligned; Linux only guarantees 4 KiB.  Over-reserve, trim.
        void* res = mmap(nullptr, len + kDarwinPage, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (res == MAP_FAILED) return err(errno_to_darwin(errno));
        const uint64_t base = round_up(reinterpret_cast<uint64_t>(res));
        r = mmap(P<void>(base), len, prot, lf | MAP_FIXED, fd, off_t(a[5]));
        const uint64_t rs = reinterpret_cast<uint64_t>(res);
        if (base > rs) munmap(res, base - rs);
        if (rs + len + kDarwinPage > base + len) munmap(P<void>(base + len), rs + len + kDarwinPage - (base + len));
    }
    if (r == MAP_FAILED) return err(errno_to_darwin(errno));
    return ok(int64_t(reinterpret_cast<uint64_t>(r)));
}
SysRet s_munmap(SysContext&, const uint64_t a[8]) { return from_host(munmap(P<void>(a[0]), round_up(a[1]))); }
SysRet s_mprotect(SysContext&, const uint64_t a[8]) { return from_host(mprotect(P<void>(a[0]), round_up(a[1]), prot_to_linux(int(a[2])))); }
SysRet s_madvise(SysContext&, const uint64_t*) { return ok(); }  // advice only (MADV_* are hints)

// ---- Mach traps ----
int64_t m_task_self(SysContext&, const uint64_t*) { return 0x103; }    // opaque port names (ours)
int64_t m_thread_self(SysContext&, const uint64_t*) { return 0x203; }
int64_t m_host_self(SysContext&, const uint64_t*) { return 0x303; }
int64_t m_reply_port(SysContext& c, const uint64_t*) {
    std::lock_guard<std::mutex> lk(c.mu);
    const uint32_t n = c.next_port;
    c.next_port += 0x100;
    return n;
}
int64_t m_msg(SysContext& c, const uint64_t a[8]) {
    // No port namespace beyond self/reply ports yet: sends cannot reach a server, receives find no port.
    { std::lock_guard<std::mutex> lk(c.mu); c.stubbed.emplace("mach_msg", "no IPC: send -> MACH_SEND_INVALID_DEST, receive -> MACH_RCV_INVALID_NAME"); }
    return (uint32_t(a[1]) & MACH_SEND_MSG_) ? MACH_SEND_INVALID_DEST_ : MACH_RCV_INVALID_NAME_;
}
int64_t m_vm_allocate(SysContext&, const uint64_t a[8]) {  // (target, *addr, size, flags)
    const uint64_t size = round_up(a[2]);
    void* want = P<void>(*P<uint64_t>(a[1]));
    const bool anywhere = uint32_t(a[3]) & VM_FLAGS_ANYWHERE_;
    if (!size) return KERN_INVALID_ARGUMENT_;
    void* r = mmap(anywhere ? nullptr : want, size + (anywhere ? kDarwinPage : 0), PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | (anywhere ? 0 : MAP_FIXED_NOREPLACE), -1, 0);
    if (r == MAP_FAILED) return anywhere ? KERN_NO_SPACE_ : KERN_NO_SPACE_;
    if (anywhere) {
        const uint64_t rs = reinterpret_cast<uint64_t>(r), base = round_up(rs);
        if (base > rs) munmap(r, base - rs);
        if (rs + size + kDarwinPage > base + size) munmap(P<void>(base + size), rs + size + kDarwinPage - (base + size));
        r = P<void>(base);
    }
    *P<uint64_t>(a[1]) = reinterpret_cast<uint64_t>(r);
    return KERN_SUCCESS_;
}
int64_t m_vm_deallocate(SysContext&, const uint64_t a[8]) { return munmap(P<void>(a[1]), round_up(a[2])) == 0 ? KERN_SUCCESS_ : KERN_INVALID_ADDRESS_; }
int64_t m_vm_protect(SysContext&, const uint64_t a[8]) {  // (target, addr, size, set_max, prot)
    return mprotect(P<void>(a[1]), round_up(a[2]), prot_to_linux(int(a[4]))) == 0 ? KERN_SUCCESS_ : KERN_INVALID_ADDRESS_;
}
int64_t m_timebase_info(SysContext&, const uint64_t a[8]) {
    // Our mach_absolute_time is CLOCK_MONOTONIC in nanoseconds: numer == denom == 1.
    uint32_t* t = P<uint32_t>(a[0]);
    t[0] = 1;
    t[1] = 1;
    return KERN_SUCCESS_;
}

uint64_t mono_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return uint64_t(ts.tv_sec) * 1000000000ull + uint64_t(ts.tv_nsec);
}

struct Tables {
    std::unordered_map<std::string, int> bsd_num, mach_num;
    std::unordered_map<int, const char*> bsd_nm, mach_nm;
    std::unordered_map<int, BsdHandler> bsd;
    std::unordered_map<int, MachHandler> mach;
    Tables() {
        for (auto& e : kBsdNames) { bsd_num.emplace(e.name, e.n); bsd_nm.emplace(e.n, e.name); }
        for (auto& e : kMachNames) { mach_num.emplace(e.name, e.n); mach_nm.emplace(e.n, e.name); }
        struct B { const char* n; BsdHandler h; } bs[] = {
            {"exit", s_exit}, {"abort_with_payload", s_abort_with_payload}, {"read", s_read}, {"read_nocancel", s_read},
            {"write", s_write}, {"write_nocancel", s_write}, {"writev", s_writev}, {"writev_nocancel", s_writev},
            {"open", s_open}, {"open_nocancel", s_open}, {"openat", s_openat}, {"openat_nocancel", s_openat},
            {"sys_close", s_close}, {"sys_close_nocancel", s_close}, {"lseek", s_lseek}, {"pread", s_pread},
            {"pread_nocancel", s_pread}, {"access", s_access},
            {"stat64", s_stat64}, {"lstat64", s_lstat64}, {"sys_fstat64", s_fstat64}, {"fstatat64", s_fstatat64},
            {"getpid", s_getpid}, {"getuid", s_getuid}, {"geteuid", s_geteuid}, {"getgid", s_getgid}, {"getegid", s_getegid},
            {"issetugid", s_issetugid}, {"thread_selfid", s_thread_selfid}, {"csops", s_csops}, {"getentropy", s_getentropy},
            {"shared_region_check_np", s_shared_region_check_np}, {"bsdthread_register", s_bsdthread_register},
            {"__mac_syscall", s_mac_syscall}, {"csrctl", s_csrctl}, {"fsgetpath", s_fsgetpath}, {"sigprocmask", s_sigprocmask}, {"__pthread_sigmask", s_sigprocmask}, {"sysctl", s_sysctl}, {"sys_sysctlbyname", s_sysctlbyname}, {"workq_kernreturn", s_workq_kernreturn},
            {"sys_fcntl", s_fcntl}, {"sys_fcntl_nocancel", s_fcntl},
            {"mmap", s_mmap}, {"munmap", s_munmap}, {"mprotect", s_mprotect}, {"madvise", s_madvise}};
        for (auto& b : bs) {
            auto it = bsd_num.find(b.n);
            if (it == bsd_num.end()) { std::fprintf(stderr, "ds-run: internal error: unknown BSD syscall name %s\n", b.n); std::abort(); }
            bsd[it->second] = b.h;
        }
        struct M { const char* n; MachHandler h; } ms[] = {
            {"task_self_trap", m_task_self}, {"thread_self_trap", m_thread_self}, {"host_self_trap", m_host_self},
            {"mach_reply_port", m_reply_port}, {"mach_msg_trap", m_msg}, {"mach_msg_overwrite_trap", m_msg}, {"mach_msg2_trap", m_msg},
            {"_kernelrpc_mach_vm_allocate_trap", m_vm_allocate}, {"_kernelrpc_mach_vm_deallocate_trap", m_vm_deallocate},
            {"_kernelrpc_mach_vm_protect_trap", m_vm_protect}, {"mach_timebase_info_trap", m_timebase_info}};
        for (auto& m : ms) {
            auto it = mach_num.find(m.n);
            if (it == mach_num.end()) { std::fprintf(stderr, "ds-run: internal error: unknown Mach trap name %s\n", m.n); std::abort(); }
            mach[it->second] = m.h;
        }
    }
};
Tables& T() { static Tables t; return t; }

bool readable(uint64_t a) {  // probe one byte without faulting (trace aid)
    char c;
    struct iovec l{&c, 1}, r{reinterpret_cast<void*>(a), 1};
    return process_vm_readv(getpid(), &l, 1, &r, 1, 0) == 1;
}
std::string fmt_args(const uint64_t a[8], int n) {
    char b[200];
    int o = 0;
    for (int i = 0; i < n; ++i) o += std::snprintf(b + o, sizeof b - size_t(o), "%s0x%llx", i ? "," : "", (unsigned long long)a[i]);
    return b;
}
}  // namespace

const char* bsd_name(int n) { auto& t = T().bsd_nm; auto it = t.find(n); return it == t.end() ? "?" : it->second; }
const char* mach_name(int n) { auto& t = T().mach_nm; auto it = t.find(n); return it == t.end() ? "?" : it->second; }
BsdHandler find_bsd(const std::string& name) { auto& t = T(); auto it = t.bsd_num.find(name); if (it == t.bsd_num.end()) return nullptr; auto h = t.bsd.find(it->second); return h == t.bsd.end() ? nullptr : h->second; }
int bsd_number(const std::string& name) { auto& t = T().bsd_num; auto it = t.find(name); return it == t.end() ? -1 : it->second; }
int mach_number(const std::string& name) { auto& t = T().mach_num; auto it = t.find(name); return it == t.end() ? -1 : it->second; }
void set_tsd_base(uint64_t v) { g_tsd = v; }
uint64_t tsd_base() { return g_tsd; }

void print_summary(SysContext& c, FILE* out) {
    std::lock_guard<std::mutex> lk(c.mu);
    std::fprintf(out, "ds-run: syscall histogram:\n");
    for (auto& [k, v] : c.hist) std::fprintf(out, "  %-40s %llu\n", k.c_str(), (unsigned long long)v);
    for (auto& [k, v] : c.stubbed) std::fprintf(out, "ds-run: STUBBED %s: %s\n", k.c_str(), v.c_str());
    for (auto& [k, v] : c.unimplemented) std::fprintf(out, "ds-run: UNIMPLEMENTED %s x%llu\n", k.c_str(), (unsigned long long)v);
}

void dispatch_svc(SysContext& c, ds::exec::CpuState& cpu) {
    using ds::exec::kPstateC;
    const int n = int(int32_t(cpu.x[16]));
    uint64_t a[8];
    for (int i = 0; i < 8; ++i) a[i] = cpu.x[i];
    const uint64_t pc = cpu.pc - 4;  // emulate() already advanced past the svc
    c.current_pc = pc;
    if (c.trace) std::fprintf(stderr, "[ds] svc x16=%d x0=0x%llx x1=0x%llx at %s\n", n, (unsigned long long)a[0], (unsigned long long)a[1], c.describe ? c.describe(pc).c_str() : "?");
    auto where = [&] { return c.describe ? c.describe(pc) : std::string("?"); };
    auto count = [&](const std::string& k) { std::lock_guard<std::mutex> lk(c.mu); ++c.hist[k]; };

    if (n == int(0x80000000u)) {  // machdep (platform) call, code in x3
        const uint32_t code = uint32_t(cpu.x[3]);
        if (code == 2) { g_tsd = a[0]; count("machdep set_tsd_base"); }
        else if (code == 3) { cpu.x[0] = g_tsd; count("machdep get_tsd_base"); }
        else {
            std::lock_guard<std::mutex> lk(c.mu);
            ++c.unimplemented["machdep " + std::to_string(code)];
            std::fprintf(stderr, "UNIMPLEMENTED machdep %u at %s\n", code, where().c_str());
        }
        if (c.trace) std::fprintf(stderr, "[ds] machdep %u(%s)\n", code, fmt_args(a, 1).c_str());
        return;
    }
    if (n < 0) {
        const int t = -n;
        if (t == 3 || t == 4) {  // MACH_ARM_TRAP_ABSTIME / CONTTIME (osfmk/mach/arm/traps.h:36-37)
            cpu.x[0] = mono_ns();
            count(t == 3 ? "mach_absolute_time" : "mach_continuous_time");
            return;
        }
        auto it = T().mach.find(t);
        if (it == T().mach.end()) {
            const std::string nm = std::string("mach ") + std::to_string(t) + " " + mach_name(t);
            { std::lock_guard<std::mutex> lk(c.mu); ++c.unimplemented[nm]; }
            std::fprintf(stderr, "UNIMPLEMENTED mach %d %s at %s\n", t, mach_name(t), where().c_str());
            cpu.x[0] = uint64_t(KERN_FAILURE_);
            return;
        }
        const int64_t r = it->second(c, a);
        cpu.x[0] = uint64_t(r);
        count(std::string("mach:") + mach_name(t));
        if (c.trace) std::fprintf(stderr, "[ds] mach %s(%s) = 0x%llx\n", mach_name(t), fmt_args(a, 4).c_str(), (unsigned long long)r);
        return;
    }
    auto it = T().bsd.find(n);
    if (it == T().bsd.end()) {
        const std::string nm = std::string("bsd ") + std::to_string(n) + " " + bsd_name(n);
        { std::lock_guard<std::mutex> lk(c.mu); ++c.unimplemented[nm]; }
        std::fprintf(stderr, "UNIMPLEMENTED bsd %d %s at %s\n", n, bsd_name(n), where().c_str());
        cpu.x[0] = uint64_t(D_ENOSYS);
        cpu.pstate |= kPstateC;
        return;
    }
    const SysRet r = it->second(c, a);
    if (r.err) { cpu.x[0] = uint64_t(r.err); cpu.pstate |= kPstateC; }
    else { cpu.x[0] = uint64_t(r.v); cpu.x[1] = uint64_t(r.v1); cpu.pstate &= ~kPstateC; }
    count(std::string("bsd:") + bsd_name(n));
    std::string pth;  // trace aid: show the path argument of path-taking calls
    {
        const std::string nm = bsd_name(n);
        const uint64_t pa = (nm == "open" || nm == "stat64" || nm == "lstat64" || nm == "access" || nm == "open_nocancel") ? a[0]
                            : (nm == "openat" || nm == "fstatat64" || nm == "openat_nocancel") ? a[1] : 0;
        if (c.trace && pa && readable(pa)) pth = std::string(" path=\"") + P<const char>(pa) + "\"";
    }
    if (c.trace) std::fprintf(stderr, "[ds] bsd %s(%s)%s = %s%lld at %s\n", bsd_name(n), fmt_args(a, 4).c_str(), pth.c_str(), r.err ? "errno " : "", (long long)(r.err ? r.err : r.v), where().c_str());
}

}  // namespace ds::darwin
