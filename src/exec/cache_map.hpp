// Maps the macOS 27 arm64e split dyld shared cache at its preferred addresses, applies
// slide-info v5 (signing authenticated pointers with the software PAC), lowers executable
// pages on demand and resolves exported symbols through the export trie.
//
// Layout sources: dyld-1378 include/mach-o/dyld_cache_format.h (header, mapping_and_slide
// info, slide info v5, subcache entries) and fixup-chains.h (shared-cache v5 pointers);
// fixups logic ported from tools/macos27/fixups.py.
#pragma once

#include <cstdint>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "lowering.hpp"
#include "pac.hpp"

namespace ds::exec {

struct CacheStats {
    size_t files = 0;
    size_t mappings = 0;
    uint64_t mapped_bytes = 0;
    uint64_t rebased = 0;         // plain pointers rewritten
    uint64_t signed_ptrs = 0;     // authenticated pointers signed in software
    uint64_t pages_lowered = 0;
    LowerCounts lowered;
};

struct SymbolInfo {
    uint64_t addr = 0;
    std::string defined_in;  // image path that actually defines it (after re-exports)
};

class CacheMap {
public:
    CacheMap() = default;
    CacheMap(const CacheMap&) = delete;
    CacheMap& operator=(const CacheMap&) = delete;
    ~CacheMap();

    // Map every subcache at its preferred address (MAP_FIXED_NOREPLACE; fails loudly, never
    // rebases), apply slide info and index images.  Executable mappings stay non-executable
    // until lower_page().
    bool map(const std::string& main_path, const Pac& pac, std::string* err);

    size_t image_count() const { return images_.size(); }
    // Address of the mach_header of `path`, or 0.
    uint64_t find_image(const std::string& path);
    // "path+0xoff" for an address inside an image __TEXT, "" otherwise.
    std::string describe(uint64_t addr) const;
    // Resolve an exported symbol of `image_path`, following re-exports.
    bool lookup(const std::string& image_path, const std::string& symbol, SymbolInfo* out, std::string* err);

    // Lower (patch) the host page containing `addr` and make it executable.  Fails when the
    // page overlaps no code section or the side table is full.
    bool lower_page(uint64_t addr, const LowerPolicy& policy, std::string* err);
    uint64_t pages_lowered() const;
    const CacheStats& stats() const { return stats_; }
    LowerPolicy policy;  // used by lazy lowering

private:
    struct Mapping {
        uint64_t addr, size;
        bool exec;
    };
    struct Img {
        uint64_t addr;
        uint64_t text_size;
        uint32_t path_off;
    };
    struct CodeRange {
        uint64_t begin, end;
    };

    bool lookup_in(uint64_t image, const std::string& symbol, int depth, SymbolInfo* out, std::string* err);
    const char* image_path(const Img& i) const { return reinterpret_cast<const char*>(base_ + i.path_off); }
    bool apply_slide(int fd, uint64_t map_addr, uint64_t map_size, uint64_t slide_off, uint64_t slide_size,
                     const Pac& pac, std::string* err);
    void index_images();

    uint64_t base_ = 0;
    std::vector<Mapping> maps_;
    std::vector<Img> images_;                       // sorted by address
    std::vector<CodeRange> code_;                   // sorted, merged
    std::unordered_map<std::string, uint64_t> by_path_;
    std::set<uint64_t> lowered_;
    mutable std::mutex mu_;
    CacheStats stats_;
};

}  // namespace ds::exec
