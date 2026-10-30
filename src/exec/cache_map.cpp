#include <thread>
#include <atomic>
#include "cache_map.hpp"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <cstring>

namespace ds::exec {

namespace {

// struct dyld_cache_header up to cacheSubType (dyld-1378 dyld_cache_format.h); field order and
// sizes copied from the header, offsets cross-checked below against
// /home/ubuntu/daylight/tools/apicatalog/machoinfo.py (which parses the same cache).
struct CacheHeader {
    char magic[16];
    uint32_t mappingOffset, mappingCount, imagesOffsetOld, imagesCountOld;
    uint64_t dyldBaseAddress, codeSignatureOffset, codeSignatureSize, slideInfoOffsetUnused,
        slideInfoSizeUnused, localSymbolsOffset, localSymbolsSize;
    uint8_t uuid[16];
    uint64_t cacheType;
    uint32_t branchPoolsOffset, branchPoolsCount;
    uint64_t dyldInCacheMH, dyldInCacheEntry, imagesTextOffset, imagesTextCount, patchInfoAddr,
        patchInfoSize, otherImageGroupAddrUnused, otherImageGroupSizeUnused, progClosuresAddr,
        progClosuresSize, progClosuresTrieAddr, progClosuresTrieSize;
    uint32_t platform, formatVersionBits;
    uint64_t sharedRegionStart, sharedRegionSize, maxSlide, dylibsImageArrayAddr, dylibsImageArraySize,
        dylibsTrieAddr, dylibsTrieSize, otherImageArrayAddr, otherImageArraySize, otherTrieAddr,
        otherTrieSize;
    uint32_t mappingWithSlideOffset, mappingWithSlideCount;
    uint64_t dylibsPBLStateArrayAddrUnused, dylibsPBLSetAddr, programsPBLSetPoolAddr,
        programsPBLSetPoolSize, programTrieAddr;
    uint32_t programTrieSize, osVersion, altPlatform, altOsVersion;
    uint64_t swiftOptsOffset, swiftOptsSize;
    uint32_t subCacheArrayOffset, subCacheArrayCount;
    uint8_t symbolFileUUID[16];
    uint64_t rosettaReadOnlyAddr, rosettaReadOnlySize, rosettaReadWriteAddr, rosettaReadWriteSize;
    uint32_t imagesOffset, imagesCount, cacheSubType;
};
static_assert(offsetof(CacheHeader, platform) == 216);
static_assert(offsetof(CacheHeader, sharedRegionStart) == 224);
static_assert(offsetof(CacheHeader, mappingWithSlideOffset) == 312);
static_assert(offsetof(CacheHeader, subCacheArrayOffset) == 392);
static_assert(offsetof(CacheHeader, imagesOffset) == 448);

struct MappingAndSlide {  // dyld_cache_mapping_and_slide_info
    uint64_t address, size, fileOffset, slideInfoFileOffset, slideInfoFileSize, flags;
    uint32_t maxProt, initProt;
};
static_assert(sizeof(MappingAndSlide) == 56);

struct SubCacheEntry {  // dyld_subcache_entry
    uint8_t uuid[16];
    uint64_t cacheVMOffset;
    char fileSuffix[32];
};
static_assert(sizeof(SubCacheEntry) == 56);

struct ImageInfo {  // dyld_cache_image_info
    uint64_t address, modTime, inode;
    uint32_t pathFileOffset, pad;
};
static_assert(sizeof(ImageInfo) == 32);

struct SlideInfo5 {  // dyld_cache_slide_info5 (page_starts follow)
    uint32_t version, page_size, page_starts_count;
    uint32_t pad;  // natural alignment of value_add
    uint64_t value_add;
};
static_assert(sizeof(SlideInfo5) == 24);

constexpr uint64_t kPageNoRebase = 0xFFFF;  // DYLD_CACHE_SLIDE_V5_PAGE_ATTR_NO_REBASE
constexpr uint64_t kHostPage = 4096;

constexpr uint32_t kMhMagic64 = 0xFEEDFACF, kLcSegment64 = 0x19, kLcDyldExportsTrie = 0x80000033;
constexpr uint32_t kLcLoadDylib = 0xC, kLcLoadWeak = 0x80000018, kLcReexport = 0x8000001F,
                   kLcLoadUpward = 0x80000023, kLcLazyLoad = 0x20;
constexpr uint32_t kSectInstr = 0x80000400;  // S_ATTR_PURE_INSTRUCTIONS | S_ATTR_SOME_INSTRUCTIONS
constexpr uint64_t kExportReexport = 0x08, kExportResolver = 0x10, kExportKindMask = 0x03;

bool fail(std::string* err, const std::string& m) {
    if (err) *err = m;
    return false;
}

int to_prot(uint32_t mach) {
    return ((mach & 1) ? PROT_READ : 0) | ((mach & 2) ? PROT_WRITE : 0) | ((mach & 4) ? PROT_EXEC : 0);
}

uint64_t uleb(const uint8_t*& p, const uint8_t* end) {
    uint64_t v = 0;
    int sh = 0;
    while (p < end) {
        const uint8_t b = *p++;
        v |= uint64_t(b & 0x7F) << sh;
        if (!(b & 0x80)) break;
        sh += 7;
        if (sh > 63) break;
    }
    return v;
}

struct Seg {
    char name[17];
    uint64_t vmaddr, vmsize, fileoff;
    const uint8_t* sections;  // first section_64
    uint32_t nsects;
};

// Walk the load commands of the (mapped) Mach-O at `mh`.
template <typename F>
void each_cmd(uint64_t mh, F&& f) {
    const auto* h = reinterpret_cast<const uint32_t*>(mh);
    if (h[0] != kMhMagic64) return;
    const uint32_t ncmds = h[4], sizeofcmds = h[5];
    const uint8_t* p = reinterpret_cast<const uint8_t*>(mh) + 32;
    const uint8_t* end = p + sizeofcmds;
    for (uint32_t i = 0; i < ncmds && p + 8 <= end; ++i) {
        uint32_t cmd, size;
        std::memcpy(&cmd, p, 4);
        std::memcpy(&size, p + 4, 4);
        if (size < 8 || p + size > end) return;
        f(cmd, p, size);
        p += size;
    }
}

Seg read_seg(const uint8_t* c) {
    Seg s{};
    std::memcpy(s.name, c + 8, 16);
    s.name[16] = 0;
    std::memcpy(&s.vmaddr, c + 24, 8);
    std::memcpy(&s.vmsize, c + 32, 8);
    std::memcpy(&s.fileoff, c + 40, 8);
    std::memcpy(&s.nsects, c + 64, 4);
    s.sections = c + 72;
    return s;
}

bool is_code_section(const char* sect, uint32_t flags) {
    static const char* names[] = {"__text", "__stubs", "__auth_stubs", "__resolver_help", "__stub_helper"};
    for (const char* n : names)
        if (std::strncmp(sect, n, 16) == 0) return true;
    return (flags & kSectInstr) != 0;
}

}  // namespace

CacheMap::~CacheMap() {
    for (const auto& m : maps_) munmap(reinterpret_cast<void*>(m.addr), m.size);
}

bool CacheMap::apply_slide(int fd, uint64_t map_addr, uint64_t map_size, uint64_t slide_off, uint64_t slide_size,
                           const Pac& pac, std::string* err) {
    std::vector<uint8_t> buf(slide_size);
    if (pread(fd, buf.data(), buf.size(), off_t(slide_off)) != ssize_t(buf.size()))
        return fail(err, "slide info read failed");
    SlideInfo5 si;
    if (slide_size < sizeof si) return fail(err, "slide info too small");
    std::memcpy(&si, buf.data(), sizeof si);
    if (si.version != 5) return fail(err, "unsupported slide info version " + std::to_string(si.version));
    if (si.page_size == 0 || sizeof si + 2ull * si.page_starts_count > slide_size ||
        uint64_t(si.page_starts_count) * si.page_size > map_size)
        return fail(err, "slide info v5 header inconsistent with mapping");
    return apply_slide_pages(buf, map_addr, pac, 0, si.page_starts_count, err, nullptr, nullptr);
}

// Slide pages [pg0, pg1) of a mapping whose slide info is `buf` (header already validated).  Thread-safe: pages are disjoint.
bool CacheMap::apply_slide_pages(const std::vector<uint8_t>& buf, uint64_t map_addr, const Pac& pac, uint32_t pg0, uint32_t pg1,
                                 std::string* err, uint64_t* signed_out, uint64_t* rebased_out) {
    SlideInfo5 si;
    std::memcpy(&si, buf.data(), sizeof si);
    const uint16_t* starts = reinterpret_cast<const uint16_t*>(buf.data() + sizeof si);
    uint64_t nsigned = 0, nrebased = 0;
    for (uint32_t pg = pg0; pg < pg1; ++pg) {
        uint64_t off = starts[pg];
        if (off == kPageNoRebase) continue;
        const uint64_t page_addr = map_addr + uint64_t(pg) * si.page_size;
        for (;;) {
            if (off + 8 > si.page_size) return fail(err, "slide chain leaves its page");
            auto* loc = reinterpret_cast<uint64_t*>(page_addr + off);
            const uint64_t raw = *loc;
            const uint64_t next = ((raw >> 52) & 0x7FF) * 8;
            const uint64_t target = (raw & ((uint64_t(1) << 34) - 1)) + si.value_add;  // slide == 0
            if (raw >> 63) {  // dyld_chained_ptr_arm64e_shared_cache_auth_rebase
                const uint16_t div = (raw >> 34) & 0xFFFF;
                const bool addr_div = (raw >> 50) & 1, key_data = (raw >> 51) & 1;
                const uint64_t mod = addr_div ? Pac::blend(page_addr + off, div) : div;
                *loc = pac.sign(key_data ? PacKey::DA : PacKey::IA, target, mod);
                ++nsigned;
            } else {
                *loc = target | (((raw >> 34) & 0xFF) << 56);
                ++nrebased;
            }
            if (next == 0) break;
            off += next;
        }
    }
    if (signed_out) *signed_out += nsigned; else stats_.signed_ptrs += nsigned;
    if (rebased_out) *rebased_out += nrebased; else stats_.rebased += nrebased;
    return true;
}

// Slide every pending mapping, spread over the host's cores (the cache has ~30M pointers; each page is independent).
bool CacheMap::slide_pending(std::vector<PendingSlide>& pending, const Pac& pac, std::string* err) {
    struct Task { size_t idx; uint32_t pg0, pg1; };
    std::vector<Task> tasks;
    constexpr uint32_t kChunkPages = 512;
    for (size_t i = 0; i < pending.size(); ++i)
        for (uint32_t pg = 0; pg < pending[i].pages; pg += kChunkPages)
            tasks.push_back({i, pg, std::min<uint32_t>(pending[i].pages, pg + kChunkPages)});
    std::atomic<size_t> next{0};
    std::atomic<bool> failed{false};
    std::mutex emu;
    std::string first_err;
    std::atomic<uint64_t> tot_signed{0}, tot_rebased{0};
    auto worker = [&] {
        uint64_t ns = 0, nr = 0;
        for (;;) {
            const size_t t = next.fetch_add(1);
            if (t >= tasks.size() || failed.load()) break;
            std::string e;
            if (!apply_slide_pages(pending[tasks[t].idx].buf, pending[tasks[t].idx].addr, pac, tasks[t].pg0, tasks[t].pg1, &e, &ns, &nr)) {
                std::lock_guard<std::mutex> lk(emu);
                if (!failed.exchange(true)) first_err = e;
            }
        }
        tot_signed += ns;
        tot_rebased += nr;
    };
    unsigned n = std::thread::hardware_concurrency();
    if (const char* e = std::getenv("DS_EXEC_SLIDE_THREADS")) n = unsigned(std::atoi(e));
    n = std::max(1u, std::min(n, 16u));
    std::vector<std::thread> ts;
    for (unsigned i = 1; i < n; ++i) ts.emplace_back(worker);
    worker();
    for (auto& t : ts) t.join();
    if (failed) return fail(err, first_err);
    stats_.signed_ptrs += tot_signed;
    stats_.rebased += tot_rebased;
    for (auto& p : pending)
        if (mprotect(reinterpret_cast<void*>(p.addr), p.size, p.final_prot) != 0) return fail(err, "mprotect after slide failed");
    return true;
}

bool CacheMap::map(const std::string& main_path, const Pac& pac, std::string* err) {
    int mfd = open(main_path.c_str(), O_RDONLY | O_CLOEXEC);
    if (mfd < 0) return fail(err, "cannot open " + main_path);
    CacheHeader hdr;
    if (pread(mfd, &hdr, sizeof hdr, 0) != ssize_t(sizeof hdr) || std::strncmp(hdr.magic, "dyld_v1", 7) != 0) {
        close(mfd);
        return fail(err, main_path + ": not a dyld cache");
    }
    std::vector<SubCacheEntry> subs(hdr.subCacheArrayCount);
    if (hdr.subCacheArrayCount &&
        pread(mfd, subs.data(), subs.size() * sizeof(SubCacheEntry), hdr.subCacheArrayOffset) !=
            ssize_t(subs.size() * sizeof(SubCacheEntry))) {
        close(mfd);
        return fail(err, "subcache array read failed");
    }
    std::vector<PendingSlide> pending;
    std::vector<std::string> files{main_path};
    for (auto& s : subs) files.push_back(main_path + std::string(s.fileSuffix, strnlen(s.fileSuffix, sizeof s.fileSuffix)));
    close(mfd);

    base_ = hdr.sharedRegionStart;
    for (const std::string& path : files) {
        int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
        if (fd < 0) return fail(err, "cannot open subcache " + path);
        struct stat st;
        CacheHeader h;
        if (fstat(fd, &st) != 0 || pread(fd, &h, sizeof h, 0) != ssize_t(sizeof h) || !h.mappingWithSlideOffset) {
            close(fd);
            return fail(err, path + ": unreadable header or no mapping_and_slide_info");
        }
        std::vector<MappingAndSlide> ms(h.mappingWithSlideCount);
        if (pread(fd, ms.data(), ms.size() * sizeof(MappingAndSlide), h.mappingWithSlideOffset) !=
            ssize_t(ms.size() * sizeof(MappingAndSlide))) {
            close(fd);
            return fail(err, path + ": mapping table read failed");
        }
        ++stats_.files;
        for (const auto& m : ms) {
            if (m.fileOffset + m.size > uint64_t(st.st_size)) {
                close(fd);
                return fail(err, path + ": mapping extends past end of file");
            }
            const bool exec = m.initProt & 4;
            const bool slid = m.slideInfoFileSize != 0;
            const int final_prot = exec ? PROT_READ : to_prot(m.initProt);  // exec pages stay unmapped-for-exec until lowered
            void* want = reinterpret_cast<void*>(m.address);
            void* got = mmap(want, m.size, slid ? (PROT_READ | PROT_WRITE) : final_prot,
                             MAP_PRIVATE | MAP_FIXED_NOREPLACE, fd, off_t(m.fileOffset));
            if (got == MAP_FAILED || got != want) {
                if (got != MAP_FAILED) munmap(got, m.size);
                char msg[256];
                std::snprintf(msg, sizeof msg, "%s: cannot map 0x%llx+0x%llx at its preferred address (MAP_FIXED_NOREPLACE): %s",
                              path.c_str(), (unsigned long long)m.address, (unsigned long long)m.size,
                              got == MAP_FAILED ? std::strerror(errno) : "kernel ignored the address");
                close(fd);
                return fail(err, msg);
            }
            maps_.push_back({m.address, m.size, exec});
            ++stats_.mappings;
            stats_.mapped_bytes += m.size;
            if (slid) {
                PendingSlide ps;
                ps.addr = m.address;
                ps.size = m.size;
                ps.final_prot = final_prot;
                ps.buf.resize(m.slideInfoFileSize);
                if (pread(fd, ps.buf.data(), ps.buf.size(), off_t(m.slideInfoFileOffset)) != ssize_t(ps.buf.size())) {
                    close(fd);
                    return fail(err, "slide info read failed");
                }
                SlideInfo5 si;
                if (ps.buf.size() < sizeof si) { close(fd); return fail(err, "slide info too small"); }
                std::memcpy(&si, ps.buf.data(), sizeof si);
                if (si.version != 5) { close(fd); return fail(err, "unsupported slide info version " + std::to_string(si.version)); }
                if (si.page_size == 0 || sizeof si + 2ull * si.page_starts_count > ps.buf.size() ||
                    uint64_t(si.page_starts_count) * si.page_size > m.size) {
                    close(fd);
                    return fail(err, "slide info v5 header inconsistent with mapping");
                }
                ps.pages = si.page_starts_count;
                pending.push_back(std::move(ps));
            }
        }
        close(fd);
    }
    if (!slide_pending(pending, pac, err)) return false;
    if (hdr.imagesCount) {
        images_.reserve(hdr.imagesCount);
        for (uint32_t i = 0; i < hdr.imagesCount; ++i) {
            const auto* ii = reinterpret_cast<const ImageInfo*>(base_ + hdr.imagesOffset) + i;
            images_.push_back({ii->address, 0, ii->pathFileOffset});
        }
    }
    index_images();
    return true;
}

void CacheMap::index_images() {
    std::sort(images_.begin(), images_.end(), [](const Img& a, const Img& b) { return a.addr < b.addr; });
    for (auto& im : images_) {
        by_path_.emplace(image_path(im), im.addr);
        each_cmd(im.addr, [&](uint32_t cmd, const uint8_t* c, uint32_t) {
            if (cmd != kLcSegment64) return;
            const Seg s = read_seg(c);
            if (std::strcmp(s.name, "__TEXT") == 0) {
                im.text_size = s.vmsize;
                for (uint32_t i = 0; i < s.nsects; ++i) {
                    const uint8_t* sec = s.sections + size_t(i) * 80;
                    uint64_t addr, size;
                    uint32_t flags;
                    std::memcpy(&addr, sec + 32, 8);
                    std::memcpy(&size, sec + 40, 8);
                    std::memcpy(&flags, sec + 64, 4);
                    if (size && is_code_section(reinterpret_cast<const char*>(sec), flags))
                        code_.push_back({addr, addr + size});
                }
            }
        });
    }
    std::sort(code_.begin(), code_.end(), [](const CodeRange& a, const CodeRange& b) { return a.begin < b.begin; });
}

uint64_t CacheMap::find_image(const std::string& path) {
    auto it = by_path_.find(path);
    return it == by_path_.end() ? 0 : it->second;
}

std::string CacheMap::describe(uint64_t addr) const {
    auto it = std::upper_bound(images_.begin(), images_.end(), addr,
                               [](uint64_t a, const Img& i) { return a < i.addr; });
    if (it == images_.begin()) return "";
    --it;
    if (addr >= it->addr + it->text_size) return "";
    char buf[64];
    std::snprintf(buf, sizeof buf, "+0x%llx", (unsigned long long)(addr - it->addr));
    return std::string(image_path(*it)) + buf;
}

bool CacheMap::lower_page(uint64_t addr, const LowerPolicy& pol, std::string* err) {
    const uint64_t page = addr & ~(kHostPage - 1);
    std::lock_guard<std::mutex> lk(mu_);
    if (lowered_.count(page)) return true;
    bool exec = false;
    for (const auto& m : maps_)
        if (m.exec && page >= m.addr && page + kHostPage <= m.addr + m.size) exec = true;
    if (!exec) return fail(err, "address is not in an executable cache mapping");
    // Collect the code-section parts of this page.
    std::vector<std::pair<uint64_t, uint64_t>> parts;
    auto it = std::upper_bound(code_.begin(), code_.end(), page, [](uint64_t a, const CodeRange& r) { return a < r.begin; });
    if (it != code_.begin()) --it;  // a range starting before the page may reach into it
    for (; it != code_.end() && it->begin < page + kHostPage; ++it) {
        const uint64_t b = std::max(it->begin, page), e = std::min(it->end, page + kHostPage);
        if (b < e) parts.emplace_back((b + 3) & ~uint64_t(3), e & ~uint64_t(3));
    }
    // Executable cache pages outside every image __TEXT section hold linker-generated branch
    // islands/stubs (cache header branchPools): they are code too, lower the whole page.
    if (parts.empty()) parts.emplace_back(page, page + kHostPage);
    if (mprotect(reinterpret_cast<void*>(page), kHostPage, PROT_READ | PROT_WRITE) != 0)
        return fail(err, "mprotect RW failed");
    LowerCounts c;
    bool ok = true;
    for (auto& [b, e] : parts)
        if (b < e && !lower_range(reinterpret_cast<uint32_t*>(b), reinterpret_cast<uint32_t*>(e), pol, c, err)) {
            ok = false;
            break;
        }
    if (!ok) {  // leave the page non-executable: partially patched code must never run
        mprotect(reinterpret_cast<void*>(page), kHostPage, PROT_READ);
        return false;
    }
    flush_icache(reinterpret_cast<void*>(page), reinterpret_cast<void*>(page + kHostPage));
    if (mprotect(reinterpret_cast<void*>(page), kHostPage, PROT_READ | PROT_EXEC) != 0)
        return fail(err, "mprotect RX failed");
    lowered_.insert(page);
    ++stats_.pages_lowered;
    stats_.lowered.words += c.words;
    stats_.lowered.unsupported_seen += c.unsupported_seen;
    stats_.lowered.ldapr_rewritten += c.ldapr_rewritten;
    for (int i = 0; i < static_cast<int>(Op::kCount); ++i) stats_.lowered.patched[i] += c.patched[i];
    return true;
}

uint64_t CacheMap::pages_lowered() const {
    std::lock_guard<std::mutex> lk(mu_);
    return lowered_.size();
}

// ---- export trie ------------------------------------------------------------------------

bool CacheMap::lookup(const std::string& image_path, const std::string& symbol, SymbolInfo* out, std::string* err) {
    const uint64_t mh = find_image(image_path);
    if (!mh) return fail(err, "image not in cache: " + image_path);
    return lookup_in(mh, symbol, 0, out, err);
}

bool CacheMap::lookup_in(uint64_t mh, const std::string& symbol, int depth, SymbolInfo* out, std::string* err) {
    if (depth > 8) return fail(err, "re-export chain too deep for " + symbol);
    uint32_t trie_off = 0, trie_size = 0;
    uint64_t le_vm = 0, le_fileoff = 0;
    std::vector<std::string> deps;
    std::string self;
    each_cmd(mh, [&](uint32_t cmd, const uint8_t* c, uint32_t size) {
        if (cmd == kLcSegment64) {
            const Seg s = read_seg(c);
            if (std::strcmp(s.name, "__LINKEDIT") == 0) { le_vm = s.vmaddr; le_fileoff = s.fileoff; }
        } else if (cmd == kLcDyldExportsTrie) {
            std::memcpy(&trie_off, c + 8, 4);
            std::memcpy(&trie_size, c + 12, 4);
        } else if (cmd == kLcLoadDylib || cmd == kLcLoadWeak || cmd == kLcReexport || cmd == kLcLoadUpward ||
                   cmd == kLcLazyLoad) {
            uint32_t no;
            std::memcpy(&no, c + 8, 4);
            if (no < size) deps.emplace_back(reinterpret_cast<const char*>(c + no), strnlen(reinterpret_cast<const char*>(c + no), size - no));
        }
    });
    self = describe(mh);
    if (!trie_size || !le_vm) return fail(err, "image has no export trie: " + self);
    // dyld convention for caches: linkedit vm address + (dataoff - linkedit fileoff).
    const uint8_t* trie = reinterpret_cast<const uint8_t*>(le_vm + (trie_off - le_fileoff));
    const uint8_t* tend = trie + trie_size;
    bool in_map = false;
    for (const auto& m : maps_)
        if (reinterpret_cast<uint64_t>(trie) >= m.addr && reinterpret_cast<uint64_t>(tend) <= m.addr + m.size) in_map = true;
    if (!in_map) return fail(err, "export trie outside mapped cache for " + self);

    const uint8_t* node = trie;
    size_t pos = 0;  // consumed characters of `symbol`
    for (;;) {
        const uint8_t* p = node;
        const uint64_t term_size = uleb(p, tend);
        if (pos == symbol.size()) {
            if (!term_size) return fail(err, "symbol not exported: " + symbol + " (" + self + ")");
            const uint8_t* t = p;
            const uint64_t flags = uleb(t, tend);
            if (flags & kExportReexport) {
                const uint64_t ord = uleb(t, tend);
                std::string name(reinterpret_cast<const char*>(t), strnlen(reinterpret_cast<const char*>(t), size_t(tend - t)));
                if (name.empty()) name = symbol;
                if (ord == 0 || ord > deps.size()) return fail(err, "bad re-export ordinal for " + symbol);
                const uint64_t dep = find_image(deps[ord - 1]);
                if (!dep) return fail(err, "re-exported from image not in cache: " + deps[ord - 1]);
                return lookup_in(dep, name, depth + 1, out, err);
            }
            if (flags & kExportResolver) return fail(err, "symbol uses a resolver (not callable directly): " + symbol);
            if ((flags & kExportKindMask) != 0) return fail(err, "symbol is not a regular export: " + symbol);
            out->addr = mh + uleb(t, tend);
            out->defined_in = self;
            return true;
        }
        const uint8_t* ch = p + term_size;
        if (ch >= tend) return fail(err, "corrupt export trie");
        const unsigned count = *ch++;
        bool descended = false;
        for (unsigned i = 0; i < count && ch < tend; ++i) {
            const char* label = reinterpret_cast<const char*>(ch);
            const size_t len = strnlen(label, size_t(tend - ch));
            ch += len + 1;
            const uint64_t child = uleb(ch, tend);
            if (symbol.compare(pos, len, label, len) == 0 && len > 0 && pos + len <= symbol.size()) {
                if (child >= trie_size) return fail(err, "corrupt export trie edge");
                node = trie + child;
                pos += len;
                descended = true;
                break;
            }
        }
        if (!descended) return fail(err, "symbol not exported: " + symbol + " (" + self + ")");
    }
}

}  // namespace ds::exec
