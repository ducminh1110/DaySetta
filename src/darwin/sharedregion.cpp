#include "sharedregion.hpp"

#include <sys/mman.h>

#include <cstring>

namespace ds::darwin {

void fill_dynamic_region(uint8_t* page, const std::string& cache_path, int32_t dev, uint64_t ino) {
    std::memcpy(page, "dyld_data    v3", 16);
    std::memcpy(page + 16, &dev, 4);  // FileIdTuple: fsid.val[0]; val[1] stays 0; then fsobjid
    std::memcpy(page + 24, &ino, 8);
    const uint32_t path_off = kDynRegionHeaderSize;  // osCryptexPathOffset stays 0 (no cryptex)
    std::memcpy(page + 36, &path_off, 4);
    std::strncpy(reinterpret_cast<char*>(page) + path_off, cache_path.c_str(), kDynRegionSize - path_off - 1);
    // Function-variant flag words stay 0: baseline variants only (host features are not advertised).
}

bool map_dynamic_region(uint64_t cache_base, const std::string& cache_path, int32_t dev, uint64_t ino, std::string* err) {
    uint64_t off, maxsz;
    std::memcpy(&off, reinterpret_cast<const void*>(cache_base + 0x1f0), 8);
    std::memcpy(&maxsz, reinterpret_cast<const void*>(cache_base + 0x1f8), 8);
    if (!off || maxsz < kDynRegionSize) { if (err) *err = "cache has no dynamic data region"; return false; }
    void* p = mmap(reinterpret_cast<void*>(cache_base + off), kDynRegionSize, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (p == MAP_FAILED || reinterpret_cast<uint64_t>(p) != cache_base + off) { if (err) *err = "cannot map dynamic region"; return false; }
    fill_dynamic_region(static_cast<uint8_t*>(p), cache_path, dev, ino);
    mprotect(p, kDynRegionSize, PROT_READ);
    return true;
}

}  // namespace ds::darwin
