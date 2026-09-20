// Darwin arm64 commpage.  Addresses and field offsets are from xnu osfmk/arm/cpu_capabilities.h
// (Darling xnu snapshot, lines 166-167 base/RO addresses, 294-342 fields) and the fill code in
// osfmk/arm/commpage/commpage.c:115-177.  The snapshot is NOT xnu-12377: offsets used by the
// macOS 27 libsystem that are absent here show up as zeroes (verified per run by DS_TRACE).
#pragma once

#include <cstdint>
#include <string>

namespace ds::darwin {

constexpr uint64_t kCommPageBase = 0x0000000FFFFFC000ull;  // _COMM_PAGE64_BASE_ADDRESS
constexpr uint64_t kCommPageRO = 0x0000000FFFFF4000ull;    // _COMM_PAGE64_RO_ADDRESS
constexpr uint64_t kCommPageLen = 0x4000;                  // one 16 KiB Darwin page each

// Host facts the page reports (honest: only what this host really has).
struct HostCaps {
    uint64_t cpu_caps = 0;   // _cpu_capabilities bit mask (cpu_capabilities.h:56-99)
    uint8_t ncpus = 1;
    uint16_t cache_line = 64;
    uint64_t memory_size = 0;
    uint32_t cpufamily = 0;
};
HostCaps detect_host_caps();

// Fill a (writable, zeroed) region as the RW page and RO page.
void fill_commpage(uint8_t* rw, uint8_t* ro, const HostCaps& caps);
// Map both pages at their fixed addresses (read-only afterwards).
bool map_commpage(const HostCaps& caps, std::string* err);

// Field offsets inside the RW page (cpu_capabilities.h:294-342).
enum : uint32_t {
    kOffSignature = 0x000, kOffCaps64 = 0x010, kOffVersion = 0x01E, kOffCaps32 = 0x020, kOffNcpus = 0x022,
    kOffCacheLine = 0x026, kOffClusters = 0x02F, kOffActiveCpus = 0x034, kOffPhysCpus = 0x035,
    kOffLogicalCpus = 0x036, kOffKernelPageShiftLegacy = 0x037, kOffMemSize = 0x038, kOffCpuFamily = 0x080,
    kOffUserTimebase = 0x090, kOffApproxSupported = 0x0C8,
    // RO page (layout version 1): user page shift 64-bit, kernel page shift
    kOffUserPageShift64 = 0x025, kOffKernelPageShift = 0x037,
};

}  // namespace ds::darwin
