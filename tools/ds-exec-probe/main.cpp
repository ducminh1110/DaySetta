#if !defined(__aarch64__)
// Runs guest arm64 code natively (trap-and-lower): aarch64 hosts only.  Exit 77 = skipped.
#include <cstdio>
int main() {
    std::fprintf(stderr, "ds-exec-probe: requires an aarch64 host (x86_64 build: skipped)\n");
    return 77;
}
#else
// ds-exec-probe: map the genuine macOS 27 arm64e dyld shared cache, lower the pages that
// run, call a few pure Apple functions and compare with host references.
#include <asm/hwcap.h>
#include <sys/auxv.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "exec/cache_map.hpp"
#include "exec/trap.hpp"

using namespace ds::exec;

namespace {
CacheMap* g_cache = nullptr;

std::string describe_cb(uint64_t pc, void*) { return g_cache->describe(pc); }
bool lazy_cb(uint64_t pc, void*, std::string* err) { return g_cache->lower_page(pc, g_cache->policy, err); }

uint32_t ref_adler32(const unsigned char* d, size_t n) {
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < n; ++i) { a = (a + d[i]) % 65521; b = (b + a) % 65521; }
    return (b << 16) | a;
}
uint32_t ref_crc32(const unsigned char* d, size_t n) {
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; ++i) {
        c ^= d[i];
        for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1)));
    }
    return ~c;
}

int g_fail = 0, g_pass = 0;
void report(const char* name, const std::string& sym_in, const std::string& got, const std::string& want) {
    const bool ok = got == want;
    std::printf("%-22s %-40s result=%s reference=%s %s\n", name, sym_in.c_str(), got.c_str(), want.c_str(), ok ? "MATCH" : "MISMATCH");
    ok ? ++g_pass : ++g_fail;
}
std::string S(long long v) { return std::to_string(v); }

bool resolve(CacheMap& c, const char* image, const char* sym, uint64_t* addr, std::string* via) {
    SymbolInfo si;
    std::string err;
    if (!c.lookup(image, sym, &si, &err)) {
        std::printf("lookup %s in %s: FAILED (%s)\n", sym, image, err.c_str());
        return false;
    }
    *addr = si.addr;
    *via = si.defined_in;
    return true;
}
}  // namespace

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IOLBF, 0);  // results must survive a crash
    const char* cache_path = "/home/ubuntu/macos-re/cache-arm64e/dyld_shared_cache_arm64e";
    const char* lookup_image = nullptr;
    const char* lookup_sym = nullptr;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--cache") && i + 1 < argc) cache_path = argv[++i];
        else if (!std::strcmp(argv[i], "--lookup") && i + 2 < argc) { lookup_image = argv[i + 1]; lookup_sym = argv[i + 2]; i += 2; }
        else { std::fprintf(stderr, "usage: ds-exec-probe [--cache PATH] [--lookup IMAGE SYMBOL]\n"); return 2; }
    }
    const unsigned long hwcap = getauxval(AT_HWCAP);
    const bool host_lse = hwcap & HWCAP_ATOMICS, host_pac = hwcap & HWCAP_PACA;
    std::printf("host: AT_HWCAP=0x%lx atomics(LSE)=%s paca=%s\n", hwcap, host_lse ? "yes" : "no", host_pac ? "yes" : "no");

    Pac pac(48);
    CacheMap cache;
    g_cache = &cache;
    cache.policy.lse = !host_lse;  // native LSE when the host has it; PAC is always software (cache is signed with software keys)
    std::string err;
    const auto t0 = std::chrono::steady_clock::now();
    if (!cache.map(cache_path, pac, &err)) { std::fprintf(stderr, "cache map failed: %s\n", err.c_str()); return 1; }
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    const CacheStats& st = cache.stats();
    std::printf("cache: %zu files, %zu mappings, %llu MiB, %zu images; slide v5: %llu plain, %llu signed (software PAC, VA %u bits) in %.2fs\n",
                st.files, st.mappings, (unsigned long long)(st.mapped_bytes >> 20), cache.image_count(),
                (unsigned long long)st.rebased, (unsigned long long)st.signed_ptrs, pac.va_bits(), secs);

    if (lookup_image) {
        uint64_t a; std::string via;
        if (!resolve(cache, lookup_image, lookup_sym, &a, &via)) return 1;
        std::printf("%s -> 0x%llx (%s)\n", lookup_sym, (unsigned long long)a, via.c_str());
        return 0;
    }

    TrapRuntime rt;
    rt.pac = &pac;
    rt.describe = describe_cb;
    rt.lazy_lower = lazy_cb;
    if (!install_trap_handlers(&rt)) { std::perror("sigaction"); return 1; }

    // ---- genuine Apple calls ---------------------------------------------------------------
    const char* kSysc = "/usr/lib/system/libsystem_c.dylib";
    const char* kZ = "/usr/lib/libz.1.dylib";
    uint64_t a;
    std::string via;
    std::vector<unsigned char> buf(4096);
    for (size_t i = 0; i < buf.size(); ++i) buf[i] = (unsigned char)(i * 131 + 7);

    if (resolve(cache, kSysc, "_strlen", &a, &via)) {
        auto f = reinterpret_cast<size_t (*)(const char*)>(a);
        const char* s = "genuine apple strlen";
        report("strlen", std::string("_strlen @") + via, S((long long)f(s)), S((long long)std::strlen(s)));
        std::string longs(1000, 'x');
        report("strlen(1000)", "_strlen", S((long long)f(longs.c_str())), "1000");
    } else ++g_fail;
    if (resolve(cache, kSysc, "_memcmp", &a, &via)) {
        auto f = reinterpret_cast<int (*)(const void*, const void*, size_t)>(a);
        unsigned char x[64], y[64];
        for (int i = 0; i < 64; ++i) x[i] = y[i] = (unsigned char)i;
        auto sign = [](int v) { return v < 0 ? -1 : v > 0 ? 1 : 0; };
        report("memcmp(eq)", std::string("_memcmp @") + via, S(sign(f(x, y, 64))), S(sign(std::memcmp(x, y, 64))));
        y[37] = 99;
        report("memcmp(lt)", "_memcmp", S(sign(f(x, y, 64))), S(sign(std::memcmp(x, y, 64))));
        report("memcmp(gt)", "_memcmp", S(sign(f(y, x, 64))), S(sign(std::memcmp(y, x, 64))));
    } else ++g_fail;
    // Plain _strtol is NOT pure: it reads the locale from Darwin TSD (mrs x9, tpidrro_el0;
    // ldr x8,[x9,x8,lsl #3] at _strtol+0xc, OBSERVED by disassembling the cache), and
    // TPIDRRO_EL0 is 0 on Linux.  _strtol_l takes the locale explicitly, so call it with the
    // exported C locale object __c_locale.
    std::printf("note: plain _strtol not called (reads Darwin TSD via mrs tpidrro_el0, 0 on Linux; observed by disassembly); using _strtol_l\n");
    uint64_t loc;
    std::string via2;
    if (resolve(cache, kSysc, "_strtol_l", &a, &via) && resolve(cache, kSysc, "__c_locale", &loc, &via2)) {
        auto f = reinterpret_cast<long (*)(const char*, char**, int, void*)>(a);
        char* e1; char* e2;
        const char* in = "  -12345xyz";
        long g = f(in, &e1, 10, reinterpret_cast<void*>(loc)), r = std::strtol(in, &e2, 10);
        report("strtol_l(C)", std::string("_strtol_l @") + via, S(g) + " end=" + S(e1 - in), S(r) + " end=" + S(e2 - in));
        const char* hex = "0x7fffffff;";
        g = f(hex, &e1, 0, reinterpret_cast<void*>(loc));
        r = std::strtol(hex, &e2, 0);
        report("strtol_l(base0)", "_strtol_l", S(g) + " end=" + S(e1 - hex), S(r) + " end=" + S(e2 - hex));
    } else ++g_fail;
    if (resolve(cache, kZ, "_adler32", &a, &via)) {
        auto f = reinterpret_cast<unsigned long (*)(unsigned long, const unsigned char*, unsigned)>(a);
        report("adler32", std::string("_adler32 @") + via, S((long long)f(1, buf.data(), (unsigned)buf.size())), S(ref_adler32(buf.data(), buf.size())));
    } else ++g_fail;
    if (resolve(cache, kZ, "_crc32", &a, &via)) {
        auto f = reinterpret_cast<unsigned long (*)(unsigned long, const unsigned char*, unsigned)>(a);
        report("crc32", std::string("_crc32 @") + via, S((long long)f(0, buf.data(), (unsigned)buf.size())), S(ref_crc32(buf.data(), buf.size())));
    } else ++g_fail;

    // ---- histogram --------------------------------------------------------------------------
    std::printf("pages lowered (lazy, on first fetch): %llu; words scanned %llu; patched:", (unsigned long long)cache.pages_lowered(),
                (unsigned long long)st.lowered.words);
    for (int i = 1; i < static_cast<int>(Op::kCount); ++i) std::printf(" %s=%llu", op_name(static_cast<Op>(i)), (unsigned long long)st.lowered.patched[i]);
    std::printf("\nunsupported (LDAPR) words seen in lowered pages: %llu\n", (unsigned long long)st.lowered.unsupported_seen);
    std::printf("traps taken:");
    for (int i = 1; i < static_cast<int>(Op::kCount); ++i) std::printf(" %s=%llu", op_name(static_cast<Op>(i)), (unsigned long long)rt.stats.by_op[i].load());
    std::printf(" | total=%llu auth_fail=%llu lazy_page_faults=%llu\n", (unsigned long long)rt.stats.total(),
                (unsigned long long)rt.stats.auth_fail.load(), (unsigned long long)rt.stats.lazy_pages.load());
    std::printf("RESULT: %d match, %d mismatch/failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}

#endif  // __aarch64__
