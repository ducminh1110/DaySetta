#include "macho.hpp"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cstring>

namespace ds::darwin {

namespace {
constexpr uint32_t kFatMagic = 0xCAFEBABE, kMhMagic64 = 0xFEEDFACF;
constexpr uint32_t kLcSegment64 = 0x19, kLcUnixThread = 0x5, kLcMain = 0x80000028;
constexpr uint32_t kCpuTypeArm64 = 0x0100000C;
constexpr uint64_t kPage = 0x4000;  // Darwin arm64 VM page (16 KiB)
constexpr uint32_t kSectInstr = 0x80000400;
constexpr uint32_t kArm64ThreadState = 6;  // ARM_THREAD_STATE64, mach/arm/thread_status.h

bool fail(std::string* e, const std::string& m) {
    if (e) *e = m;
    return false;
}
uint32_t be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }
int prot_of(uint32_t p) { return ((p & 1) ? PROT_READ : 0) | ((p & 2) ? PROT_WRITE : 0) | ((p & 4) ? PROT_EXEC : 0); }
}  // namespace

bool find_arm64e_slice(const uint8_t* buf, size_t len, uint64_t* off, uint64_t* size, std::string* err) {
    if (len < 12) return fail(err, "file too small");
    uint32_t magic;
    std::memcpy(&magic, buf, 4);
    if (magic == kMhMagic64) {  // thin
        uint32_t cpu, sub;
        std::memcpy(&cpu, buf + 4, 4);
        std::memcpy(&sub, buf + 8, 4);
        if (cpu != kCpuTypeArm64 || (sub & 0xFFFFFF) != 2) return fail(err, "thin Mach-O is not arm64e");
        *off = 0;
        *size = len;
        return true;
    }
    if (be32(buf) != kFatMagic) return fail(err, "not a Mach-O/fat file");
    const uint32_t n = be32(buf + 4);
    for (uint32_t i = 0; i < n && 8 + 20 * (i + 1) <= len; ++i) {
        const uint8_t* a = buf + 8 + 20 * i;  // fat_arch: cputype cpusubtype offset size align (big endian)
        if (be32(a) == kCpuTypeArm64 && (be32(a + 4) & 0xFFFFFF) == 2) {
            *off = be32(a + 8);
            *size = be32(a + 12);
            return true;
        }
    }
    return fail(err, "no arm64e slice");
}

bool load_macho(const std::string& path, uint64_t fixed_base, const ds::exec::LowerPolicy& policy, MachoImage* out,
                std::string* err) {
    int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) return fail(err, "cannot open " + path);
    struct Closer { int fd; ~Closer() { close(fd); } } closer{fd};
    uint8_t head[4096];
    const ssize_t hn = pread(fd, head, sizeof head, 0);
    uint64_t soff = 0, ssize = 0;
    if (hn < 12 || !find_arm64e_slice(head, size_t(hn), &soff, &ssize, err)) return fail(err, err && !err->empty() ? *err : "bad file");
    std::vector<uint8_t> hdr(32 + 16384);
    if (pread(fd, hdr.data(), hdr.size(), off_t(soff)) < 32) return fail(err, "short Mach-O header");
    uint32_t w[8];
    std::memcpy(w, hdr.data(), 32);
    if (w[0] != kMhMagic64) return fail(err, "slice is not a 64-bit Mach-O");
    MachoImage im;
    im.path = path;
    im.filetype = w[3];
    const uint32_t ncmds = w[4], sizeofcmds = w[5];
    if (sizeofcmds > 16384) return fail(err, "load commands too large");
    const uint8_t* p = hdr.data() + 32;
    const uint8_t* end = p + sizeofcmds;
    struct Sect { uint64_t addr, size; };
    std::vector<Sect> code_sects;
    uint64_t text_vm = ~0ull, hi = 0, thread_pc = 0;
    bool have_thread = false;
    for (uint32_t i = 0; i < ncmds && p + 8 <= end; ++i) {
        uint32_t cmd, sz;
        std::memcpy(&cmd, p, 4);
        std::memcpy(&sz, p + 4, 4);
        if (sz < 8 || p + sz > end) return fail(err, "bad load command");
        if (cmd == kLcSegment64) {
            Segment s;
            char nm[17] = {};
            std::memcpy(nm, p + 8, 16);
            s.name = nm;
            uint32_t nsects, initprot;
            std::memcpy(&s.vmaddr, p + 24, 8);
            std::memcpy(&s.vmsize, p + 32, 8);
            std::memcpy(&s.fileoff, p + 40, 8);
            std::memcpy(&s.filesize, p + 48, 8);
            std::memcpy(&initprot, p + 60, 4);
            std::memcpy(&nsects, p + 64, 4);
            s.initprot = initprot;
            for (uint32_t k = 0; k < nsects && 72 + 80 * (k + 1) <= sz; ++k) {
                const uint8_t* sec = p + 72 + 80 * k;
                Sect c;
                uint32_t flags;
                std::memcpy(&c.addr, sec + 32, 8);
                std::memcpy(&c.size, sec + 40, 8);
                std::memcpy(&flags, sec + 64, 4);
                if (c.size && (flags & kSectInstr)) code_sects.push_back(c);
            }
            if (s.name == "__TEXT") text_vm = s.vmaddr;
            if (s.name != "__PAGEZERO") hi = std::max(hi, s.vmaddr + s.vmsize);
            im.segments.push_back(s);
        } else if (cmd == kLcUnixThread && sz >= 16 + 33 * 8) {
            uint32_t flavor;
            std::memcpy(&flavor, p + 8, 4);
            if (flavor == kArm64ThreadState) {  // arm_thread_state64: x[29], fp, lr, sp, pc, cpsr
                std::memcpy(&thread_pc, p + 16 + 32 * 8, 8);
                have_thread = true;
            }
        } else if (cmd == kLcMain) {
            std::memcpy(&im.main_entryoff, p + 8, 8);
        }
        p += sz;
    }
    if (text_vm == ~0ull) return fail(err, "no __TEXT segment");
    // Reserve the whole span, then overlay segments (MAP_FIXED over our own reservation).
    const uint64_t span = (hi - text_vm + kPage - 1) & ~(kPage - 1);
    uint64_t base;
    if (fixed_base) {
        void* r = mmap(reinterpret_cast<void*>(fixed_base), span, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
        if (r == MAP_FAILED || reinterpret_cast<uint64_t>(r) != fixed_base) return fail(err, path + ": cannot map at its fixed base");
        base = fixed_base;
    } else {
        void* r = mmap(nullptr, span + kPage, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (r == MAP_FAILED) return fail(err, "cannot reserve address space");
        base = (reinterpret_cast<uint64_t>(r) + kPage - 1) & ~(kPage - 1);
    }
    im.slide = base - text_vm;
    im.mh = base;
    im.vm_end = base + span;
    for (const Segment& s : im.segments) {
        if (s.name == "__PAGEZERO" || s.vmsize == 0) continue;
        const uint64_t addr = s.vmaddr + im.slide, vsz = (s.vmsize + kPage - 1) & ~(kPage - 1);
        void* a = mmap(reinterpret_cast<void*>(addr), vsz, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
        if (a == MAP_FAILED) return fail(err, "segment anon map failed: " + s.name);
        if (s.filesize) {
            const uint64_t flen = (s.filesize + 4095) & ~uint64_t(4095);
            void* f = mmap(reinterpret_cast<void*>(addr), flen, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_FIXED, fd, off_t(soff + s.fileoff));
            if (f == MAP_FAILED) return fail(err, "segment file map failed: " + s.name);
            if (s.filesize & 4095) std::memset(reinterpret_cast<void*>(addr + s.filesize), 0, flen - s.filesize);
        }
    }
    for (const Sect& c : code_sects) im.code.push_back({c.addr + im.slide, c.addr + im.slide + c.size});
    for (const CodeRange& r : im.code) {
        std::string e;
        if (!ds::exec::lower_range(reinterpret_cast<uint32_t*>((r.begin + 3) & ~3ull), reinterpret_cast<uint32_t*>(r.end & ~3ull), policy, im.lowered, &e))
            return fail(err, e);
    }
    for (const Segment& s : im.segments) {
        if (s.name == "__PAGEZERO" || s.vmsize == 0) continue;
        const uint64_t addr = s.vmaddr + im.slide, vsz = (s.vmsize + kPage - 1) & ~(kPage - 1);
        if (s.initprot & 4) ds::exec::flush_icache(reinterpret_cast<void*>(addr), reinterpret_cast<void*>(addr + vsz));
        if (mprotect(reinterpret_cast<void*>(addr), vsz, prot_of(s.initprot)) != 0) return fail(err, "mprotect failed: " + s.name);
    }
    if (have_thread) im.unixthread_pc = thread_pc + im.slide;
    (void)ssize;
    *out = std::move(im);
    return true;
}

}  // namespace ds::darwin
