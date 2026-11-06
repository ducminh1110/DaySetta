#if !defined(__aarch64__)
// Runs guest arm64 code natively (trap-and-lower): aarch64 hosts only.  Exit 77 = skipped.
#include <cstdio>
int main() {
    std::fprintf(stderr, "ds-run: requires an aarch64 host (x86_64 build: skipped)\n");
    return 77;
}
#else
// ds-run: run a genuine Apple macOS arm64e executable under the genuine /usr/lib/dyld and the
// genuine shared cache on a Linux aarch64 host.  Milestone R3.
//   ds-run [--cache PATH] [--root DIR] EXECUTABLE [ARGS...]
// DS_TRACE=1 logs every Darwin syscall/trap to stderr and prints a histogram at exit.
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/random.h>
#include <unistd.h>

#include <asm/hwcap.h>
#include <sys/auxv.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "darwin/commpage.hpp"
#include "darwin/macho.hpp"
#include "darwin/sharedregion.hpp"
#include "darwin/stack.hpp"
#include "darwin/syscalls.hpp"
#include "exec/cache_map.hpp"
#include "exec/trap.hpp"

using namespace ds;

extern char** environ;

namespace {
exec::CacheMap g_cache;
darwin::MachoImage g_exe, g_dyld;
darwin::SysContext g_ctx;

std::string describe(uint64_t pc) {
    std::string s = g_cache.describe(pc);
    if (!s.empty()) return s;
    char b[64];
    for (const darwin::MachoImage* im : {&g_exe, &g_dyld})
        if (pc >= im->mh && pc < im->vm_end) {
            std::snprintf(b, sizeof b, "+0x%llx", (unsigned long long)(pc - im->mh));
            return (im == &g_exe ? "exe" : "dyld") + std::string(b);
        }
    return "";
}
std::string describe_cb(uint64_t pc, void*) { return describe(pc); }
void svc_cb(exec::CpuState& cpu, uint32_t, void*) { darwin::dispatch_svc(g_ctx, cpu); }
uint64_t tsd_cb(void*) { return darwin::tsd_base(); }

[[noreturn]] void die(const std::string& m) {
    std::fprintf(stderr, "ds-run: %s\n", m.c_str());
    std::exit(1);
}
}  // namespace

int main(int argc, char** argv) {
    std::string cache = "/home/ubuntu/macos-re/cache-arm64e/dyld_shared_cache_arm64e";
    std::string root = "/home/ubuntu/macos-re/store-27/root";
    int i = 1;
    for (; i < argc && argv[i][0] == '-'; ++i) {
        if (!std::strcmp(argv[i], "--cache") && i + 1 < argc) cache = argv[++i];
        else if (!std::strcmp(argv[i], "--root") && i + 1 < argc) root = argv[++i];
        else if (!std::strcmp(argv[i], "--")) { ++i; break; }
        else { std::fprintf(stderr, "usage: ds-run [--cache PATH] [--root DIR] EXECUTABLE [ARGS...]\n"); return 2; }
    }
    if (i >= argc) { std::fprintf(stderr, "usage: ds-run [--cache PATH] [--root DIR] EXECUTABLE [ARGS...]\n"); return 2; }
    const std::string exe_path = argv[i];
    std::vector<std::string> gargv(argv + i, argv + argc);
    const char* tr = std::getenv("DS_TRACE");
    g_ctx.trace = tr && *tr && std::strcmp(tr, "0") != 0;
    g_ctx.describe = describe;

    const bool host_lse = getauxval(AT_HWCAP) & HWCAP_ATOMICS;
    exec::Pac pac(48);
    g_cache.policy.lse = !host_lse;
    std::string err;
    if (!g_cache.map(cache, pac, &err)) die("cache map failed: " + err);
    g_ctx.shared_cache_base = g_cache.base();
    {
        struct stat st;
        if (stat(cache.c_str(), &st) != 0) die("cannot stat cache");
        if (!darwin::map_dynamic_region(g_cache.base(), cache, int32_t(st.st_dev), st.st_ino, &err)) die(err);
        g_ctx.fileids[{int32_t(st.st_dev), uint64_t(st.st_ino)}] = cache;
    }
    if (!darwin::map_commpage(darwin::detect_host_caps(), &err)) die(err);

    exec::LowerPolicy pol;
    pol.lse = !host_lse;
    if (!darwin::load_macho(exe_path, 0x100000000ull, pol, &g_exe, &err)) die("exe: " + err);
    if (!darwin::load_macho(root + "/usr/lib/dyld", 0, pol, &g_dyld, &err)) die("dyld: " + err);
    if (!g_dyld.unixthread_pc) die("dyld has no LC_UNIXTHREAD");
    g_ctx.main_mh = g_exe.mh;

    if (g_ctx.trace)
        for (const darwin::MachoImage* im : {&g_exe, &g_dyld}) {
            const auto& l = im->lowered;
            std::fprintf(stderr, "ds-run: lowered %s: words=%llu", im->path.c_str(), (unsigned long long)l.words);
            for (int k = 1; k < static_cast<int>(exec::Op::kCount); ++k)
                if (l.patched[k]) std::fprintf(stderr, " %s=%llu", exec::op_name(static_cast<exec::Op>(k)), (unsigned long long)l.patched[k]);
            std::fprintf(stderr, " ldapr->ldar=%llu\n", (unsigned long long)l.ldapr_rewritten);
        }
    // Main thread stack (xnu: 8 MiB default, MAXSSIZ policy not modelled).
    const size_t kStack = 8u << 20;
    void* stk = mmap(nullptr, kStack, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (stk == MAP_FAILED) die("stack allocation failed");
    darwin::StackSpec spec;
    spec.mh = g_exe.mh;
    spec.argv = gargv;
    for (char** e = environ; *e; ++e) spec.envp.emplace_back(*e);
    char* rp = realpath(exe_path.c_str(), nullptr);
    spec.executable_path = rp ? rp : exe_path;
    free(rp);
    if (getrandom(spec.entropy, sizeof spec.entropy, 0) != ssize_t(sizeof spec.entropy)) die("getrandom failed");
    const uint64_t top = reinterpret_cast<uint64_t>(stk) + kStack;
    spec.main_stack[0] = top;
    spec.main_stack[1] = kStack - 0x4000;
    spec.main_stack[2] = reinterpret_cast<uint64_t>(stk);
    spec.main_stack[3] = kStack;
    const uint64_t sp = darwin::build_initial_stack(top - 0x1000, kStack / 2, spec);
    if (!sp) die("initial stack does not fit");

    exec::TrapRuntime rt;
    rt.pac = &pac;
    rt.describe = describe_cb;
    rt.lazy_lower = [](uint64_t pc, void*, std::string* e) { return g_cache.lower_page(pc, g_cache.policy, e); };
    rt.svc = svc_cb;
    rt.tsd = tsd_cb;
    if (!exec::install_trap_handlers(&rt)) die("sigaction failed");

    if (g_ctx.trace)
        std::fprintf(stderr, "ds-run: exe mh=0x%llx dyld mh=0x%llx entry=0x%llx sp=0x%llx\n", (unsigned long long)g_exe.mh,
                     (unsigned long long)g_dyld.mh, (unsigned long long)g_dyld.unixthread_pc, (unsigned long long)sp);
    std::fflush(nullptr);
    // Enter __dyld_start like the kernel: sp and pc set, everything else zero (x17 carries the entry).
    register uint64_t entry asm("x17") = g_dyld.unixthread_pc;
    asm volatile(
        "mov sp, %0\n"
        "mov x1, xzr\n mov x2, xzr\n mov x3, xzr\n mov x4, xzr\n mov x5, xzr\n mov x6, xzr\n mov x7, xzr\n"
        "mov x8, xzr\n mov x9, xzr\n mov x10, xzr\n mov x11, xzr\n mov x12, xzr\n mov x13, xzr\n mov x14, xzr\n"
        "mov x15, xzr\n mov x16, xzr\n mov x19, xzr\n mov x20, xzr\n mov x21, xzr\n mov x22, xzr\n"
        "mov x23, xzr\n mov x24, xzr\n mov x25, xzr\n mov x26, xzr\n mov x27, xzr\n mov x28, xzr\n mov x29, xzr\n"
        "mov x30, xzr\n mov x0, xzr\n br x17\n"
        :
        : "r"(sp), "r"(entry)
        : "memory");
    __builtin_unreachable();
}

#endif  // __aarch64__
