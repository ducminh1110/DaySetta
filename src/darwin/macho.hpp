// Minimal Mach-O loader for ds-run: picks the arm64e slice of a fat file, maps the segments the
// way xnu's exec does (segments at vmaddr + slide, zero-fill tail, initial protections), lowers
// the executable code in place (see exec/lowering.hpp) and reports the entry point.  Fixups are
// NOT applied: the genuine dyld does that (for itself in __dyld_start/start, for the main
// executable in its own pass), exactly as with the kernel loader.
//
// Layout sources: mach-o/loader.h, mach-o/fat.h (dyld-1378 mach_o + Darling xnu EXTERNAL_HEADERS);
// CPU_SUBTYPE_ARM64E == 2: xnu osfmk/mach/machine.h:388.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "exec/lowering.hpp"

namespace ds::darwin {

struct Segment {
    std::string name;
    uint64_t vmaddr, vmsize, fileoff, filesize;
    uint32_t initprot;
};

struct CodeRange {
    uint64_t begin, end;  // host addresses
};

struct MachoImage {
    std::string path;
    uint32_t filetype = 0;
    uint64_t mh = 0;             // address of the mach_header_64 (== __TEXT vmaddr + slide)
    uint64_t slide = 0;
    uint64_t vm_end = 0;         // end of the highest mapped segment
    uint64_t unixthread_pc = 0;  // LC_UNIXTHREAD pc + slide, 0 if absent
    uint64_t main_entryoff = 0;  // LC_MAIN entryoff, 0 if absent
    std::vector<Segment> segments;
    std::vector<CodeRange> code;
    ds::exec::LowerCounts lowered;
};

// Offset/size of the arm64e (cpusubtype & 0xFFFFFF == 2) slice of `buf` (a fat or thin file).
bool find_arm64e_slice(const uint8_t* buf, size_t len, uint64_t* off, uint64_t* size, std::string* err);

// Map `path`.  `fixed_base` != 0: mh goes exactly there (MAP_FIXED_NOREPLACE) -- for the main
// executable at its link-time address; 0: any free 16 KiB-aligned region (slide chosen by us).
bool load_macho(const std::string& path, uint64_t fixed_base, const ds::exec::LowerPolicy& policy, MachoImage* out,
                std::string* err);

}  // namespace ds::darwin
