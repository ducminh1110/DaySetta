// Shared-region "dynamic config" page.  On macOS the first dyld that maps a cache creates this
// 16 KiB page inside the shared region (dyld-1378 common/DyldSharedCache.{h,cpp}
// DynamicRegion::make/setCachePath, layout "dyld_data    v3"); later processes (and dyld's
// DyldSharedCache::dynamicRegion()) find it at header.dynamicDataOffset.  Because ds-run maps the
// cache itself, it plays the kernel/first-dyld role and writes the page.
#pragma once

#include <cstdint>
#include <string>

namespace ds::darwin {

// Layout of DynamicRegion (dyld-1378 DyldSharedCache.h:222-241): magic[16], FileIdTuple
// (fsid_t 8 + fsobj_id_t 8), osCryptexPathOffset u32, cachePathOffset u32, pad u64,
// two __uint128_t function-variant flag words; sizeof == 80, strings follow.
constexpr uint32_t kDynRegionHeaderSize = 80;
constexpr uint64_t kDynRegionSize = 0x4000;
// (dev, ino) identify the main cache file the way FileIdTuple::init does (DyldSharedCache.cpp:2270):
// fsid.val[0] = st_dev, fsid.val[1] = 0, fsobjid = st_ino.
void fill_dynamic_region(uint8_t* page, const std::string& cache_path, int32_t dev, uint64_t ino);

// Maps the page at cache_base + header.dynamicDataOffset (header offsets 0x1f0/0x1f8, verified
// against the 27 cache header and DyldSharedCache::dynamicRegion's disassembly).
bool map_dynamic_region(uint64_t cache_base, const std::string& cache_path, int32_t dev, uint64_t ino, std::string* err);

}  // namespace ds::darwin
