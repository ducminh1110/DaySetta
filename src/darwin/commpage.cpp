#include "commpage.hpp"

#include <asm/hwcap.h>
#include <sys/auxv.h>
#include <sys/mman.h>
#include <unistd.h>

#include <cstring>

namespace ds::darwin {

namespace {
// cpu_capabilities.h:56-99 (kHas*), set from what the host really implements.
constexpr uint64_t kFP16 = 0x8, kCache64 = 0x20, kAdvSIMD = 0x100, kHPFPCvt = 0x200, kVfp = 0x400, kFMA = 0x2000,
                   kFHM = 0x4000, kCrypto = 0x01000000, kLSE = 0x02000000, kCrc32 = 0x04000000,
                   kSHA512 = 0x80000000ull, kSHA3 = 0x100000000ull, kFCMA = 0x200000000ull,
                   kDotProd = 0x0000040000000000ull, kSB = 0x0000200000000000ull,
                   kJSCVT = 0x0004000000000000ull, kDPB = 0x0010000000000000ull;
}  // namespace

HostCaps detect_host_caps() {
    HostCaps c;
    const unsigned long h = getauxval(AT_HWCAP);
    uint64_t caps = 0;
    if (h & HWCAP_ASIMD) caps |= kAdvSIMD | kFMA;
    if (h & HWCAP_FP) caps |= kVfp;
    if (h & HWCAP_FPHP) caps |= kHPFPCvt;
    if (h & HWCAP_ASIMDHP) caps |= kFP16;
    if (h & HWCAP_ATOMICS) caps |= kLSE;  // LSE advertised only when the host executes it natively
    if (h & HWCAP_CRC32) caps |= kCrc32;
    if ((h & HWCAP_AES) && (h & HWCAP_SHA1) && (h & HWCAP_SHA2) && (h & HWCAP_PMULL)) caps |= kCrypto;
    if (h & HWCAP_SHA512) caps |= kSHA512;
    if (h & HWCAP_SHA3) caps |= kSHA3;
    if (h & HWCAP_ASIMDDP) caps |= kDotProd;
    if (h & HWCAP_ASIMDFHM) caps |= kFHM;
    if (h & HWCAP_FCMA) caps |= kFCMA;
    if (h & HWCAP_JSCVT) caps |= kJSCVT;
    if (h & HWCAP_SB) caps |= kSB;
    if (h & HWCAP_DCPOP) caps |= kDPB;
    // Deliberately NOT advertised even though the guest can execute them under ds-exec lowering:
    // kHasFeatPAuth (software PAC), kHasFeatLRCPC/LRCPC2 (LDAPR strengthened to LDAR) -- the page
    // describes the hardware, and emulated paths are slower than the non-feature fallbacks.
    caps |= kCache64;
    const long n = sysconf(_SC_NPROCESSORS_ONLN);
    c.ncpus = uint8_t(n < 1 ? 1 : n > 255 ? 255 : n);
    caps |= uint64_t(c.ncpus) << 16;  // kNumCPUs, cpu_capabilities.h:69
    if (c.ncpus == 1) caps |= 0x8000;  // kUP
    c.cpu_caps = caps;
    const long pages = sysconf(_SC_PHYS_PAGES), psz = sysconf(_SC_PAGESIZE);
    c.memory_size = uint64_t(pages) * uint64_t(psz);
    c.cpufamily = 0;  // CPUFAMILY_UNKNOWN (mach/machine.h:424): this host is not an Apple SoC
    return c;
}

void fill_commpage(uint8_t* rw, uint8_t* ro, const HostCaps& c) {
    std::memcpy(rw + kOffSignature, "commpage 64-bit", 15);  // bsd/sys/commpage_private.h:34
    std::memcpy(rw + kOffCaps64, &c.cpu_caps, 8);
    const uint16_t ver = 3;  // _COMM_PAGE_THIS_VERSION, cpu_capabilities.h:299
    std::memcpy(rw + kOffVersion, &ver, 2);
    const uint32_t caps32 = uint32_t(c.cpu_caps);
    std::memcpy(rw + kOffCaps32, &caps32, 4);  // caps32 overlaps ncpus per header; written first, ncpus after
    rw[kOffNcpus] = c.ncpus;
    std::memcpy(rw + kOffCacheLine, &c.cache_line, 2);
    rw[kOffClusters] = 1;
    rw[kOffActiveCpus] = rw[kOffPhysCpus] = rw[kOffLogicalCpus] = c.ncpus;
    rw[kOffKernelPageShiftLegacy] = 14;
    rw[kOffUserPageShift64] = 14;  // SIXTEENK_PAGE_SHIFT, commpage.c:157
    std::memcpy(rw + kOffMemSize, &c.memory_size, 8);
    std::memcpy(rw + kOffCpuFamily, &c.cpufamily, 4);
    rw[kOffUserTimebase] = 0;  // userspace cannot read the timebase: mach_absolute_time uses the trap
    rw[kOffApproxSupported] = 0;
    // RO page, layout version 1 (cpu_capabilities.h:305-333)
    ro[kOffUserPageShift64] = 14;
    ro[kOffKernelPageShift] = 14;
}

bool map_commpage(const HostCaps& caps, std::string* err) {
    uint8_t* pages[2] = {};
    const uint64_t addr[2] = {kCommPageBase, kCommPageRO};
    for (int i = 0; i < 2; ++i) {
        void* p = mmap(reinterpret_cast<void*>(addr[i]), kCommPageLen, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
        if (p == MAP_FAILED || reinterpret_cast<uint64_t>(p) != addr[i]) {
            if (err) *err = "cannot map commpage at its fixed address";
            return false;
        }
        pages[i] = static_cast<uint8_t*>(p);
    }
    fill_commpage(pages[0], pages[1], caps);
    for (int i = 0; i < 2; ++i) mprotect(pages[i], kCommPageLen, PROT_READ);
    return true;
}

}  // namespace ds::darwin
