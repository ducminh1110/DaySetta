#include "trampoline.hpp"

#include <sys/mman.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <vector>

namespace ds::exec {

namespace {

constexpr uint64_t kReach = 127ull << 20;      // B imm26 reaches +-128 MB; keep a margin
constexpr size_t kChunk = 512 * 1024;
constexpr unsigned kQOff = 288;                                         // v0-v7, v16-v31 (caller-saved vector registers) follow the GPR frame
constexpr unsigned kFrame = kQOff + 24 * 16, kRedZone = 128, kTotal = kFrame + kRedZone;  // 800, 16-aligned

struct Chunk {
    uint64_t base = 0, used = 0, common = 0;
};

std::mutex g_mu;
std::vector<Chunk> g_chunks;
std::deque<SiteInfo> g_sites;
TrampHandler g_handler = nullptr;
TrampStats g_stats;
long g_mrs_tls = -1;

uint32_t enc_stp(unsigned a, unsigned b, unsigned off) { return 0xA9000000u | ((off / 8) << 15) | (b << 10) | (31u << 5) | a; }
uint32_t enc_ldp(unsigned a, unsigned b, unsigned off) { return 0xA9400000u | ((off / 8) << 15) | (b << 10) | (31u << 5) | a; }
uint32_t enc_stpq(unsigned a, unsigned b, unsigned off) { return 0xAD000000u | ((off / 16) << 15) | (b << 10) | (31u << 5) | a; }
uint32_t enc_ldpq(unsigned a, unsigned b, unsigned off) { return 0xAD400000u | ((off / 16) << 15) | (b << 10) | (31u << 5) | a; }
uint32_t enc_str(unsigned t, unsigned off) { return 0xF9000000u | ((off / 8) << 10) | (31u << 5) | t; }
uint32_t enc_ldr(unsigned t, unsigned off) { return 0xF9400000u | ((off / 8) << 10) | (31u << 5) | t; }

// Common block: see trampoline.hpp.  `handler_lit_off` is the byte offset of the 8-byte handler literal from the block start.
std::vector<uint32_t> build_common(uint64_t handler, size_t* lit_off_out) {
    std::vector<uint32_t> c;
    c.push_back(0xD10003FFu | (kTotal << 10));                       // sub sp, sp, #416
    for (unsigned r = 0; r < 30; r += 2) c.push_back(enc_stp(r, r + 1, r * 8));
    c.push_back(enc_str(30, 240));
    {   // vector registers the host handler may clobber
        unsigned off = kQOff;
        for (unsigned r : {0u, 2u, 4u, 6u, 16u, 18u, 20u, 22u, 24u, 26u, 28u, 30u}) { c.push_back(enc_stpq(r, r + 1, off)); off += 32; }
    }
    c.push_back(0xD53B4200u);                                         // mrs x0, nzcv
    c.push_back(enc_str(0, 248));
    c.push_back(0x910003E0u | (kTotal << 10));                        // add x0, sp, #416 (guest sp)
    c.push_back(enc_str(0, 256));
    c.push_back(0x910003E0u);                                         // mov x0, sp
    c.push_back(0xAA1203E1u);                                         // mov x1, x18 (SiteInfo*)
    const size_t ldr_idx = c.size();
    c.push_back(0);                                                   // ldr x16, handler literal (patched below)
    c.push_back(0xD63F0200u);                                         // blr x16
    c.push_back(enc_ldr(1, 248));
    c.push_back(0xD51B4201u);                                         // msr nzcv, x1
    for (unsigned r = 0; r < 30; r += 2) c.push_back(enc_ldp(r, r + 1, r * 8));
    {
        unsigned off = kQOff;
        for (unsigned r : {0u, 2u, 4u, 6u, 16u, 18u, 20u, 22u, 24u, 26u, 28u, 30u}) { c.push_back(enc_ldpq(r, r + 1, off)); off += 32; }
    }
    c.push_back(enc_ldr(30, 240));
    c.push_back(enc_ldr(18, 264));                                    // next pc (after the x18 restore: x18 is the branch register)
    c.push_back(0x910003FFu | (kTotal << 10));                        // add sp, sp, #416
    c.push_back(0xD61F0240u);                                         // br x18
    if (c.size() & 1) c.push_back(0xD503201Fu);                       // nop: 8-byte align the literal
    const size_t lit_off = c.size() * 4;
    c[ldr_idx] = 0x58000000u | (uint32_t((lit_off - ldr_idx * 4) / 4) << 5) | 16u;
    c.push_back(uint32_t(handler));
    c.push_back(uint32_t(handler >> 32));
    *lit_off_out = lit_off;
    return c;
}

bool in_reach(uint64_t a, uint64_t b) { return (a > b ? a - b : b - a) < kReach; }

Chunk* make_chunk(uint64_t near, uint64_t skip) {
    // look for free address space within reach of `near`: above, then below, in 256 KB steps (MAP_FIXED_NOREPLACE never overwrites).
    const uint64_t up0 = (near + skip + 0xFFFFF) & ~0xFFFFFull;
    for (int pass = 0; pass < 2; ++pass) {
        for (uint64_t d = 0; d + skip < kReach - kChunk - 0x100000; d += 0x40000) {
            uint64_t cand = pass == 0 ? up0 + d : ((near - skip - kChunk - d) & ~0xFFFFull);
            if (pass == 1 && near < skip + kChunk + d + 0x10000) break;
            void* p = mmap(reinterpret_cast<void*>(cand), kChunk, PROT_READ | PROT_WRITE | PROT_EXEC,
                           MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
            if (p == MAP_FAILED) continue;
            if (reinterpret_cast<uint64_t>(p) != cand) { munmap(p, kChunk); continue; }
            Chunk ch;
            ch.base = cand;
            size_t lit;
            auto code = build_common(reinterpret_cast<uint64_t>(g_handler), &lit);
            std::memcpy(p, code.data(), code.size() * 4);
            ch.common = cand;
            ch.used = (code.size() * 4 + 15) & ~15ull;
            __builtin___clear_cache(reinterpret_cast<char*>(p), reinterpret_cast<char*>(p) + ch.used);
            g_chunks.push_back(ch);
            ++g_stats.islands;
            return &g_chunks.back();
        }
    }
    return nullptr;
}

}  // namespace

void trampoline_set_handler(TrampHandler h) { g_handler = h; }
void trampoline_set_mrs_tls(long off) { g_mrs_tls = (off >= 0 && off < 32760 && (off & 7) == 0) ? off : -1; }

bool trampoline_wants(const Insn& in) {
#if defined(__aarch64__)
    if (!g_handler) return false;
    switch (in.op) {
        case Op::PacSign: case Op::PacAuth: case Op::PacGa: case Op::BrAuth: case Op::RetAuth: case Op::LdrAuth:
        case Op::Cas: case Op::Casp: case Op::Rmw: case Op::Ldapur: case Op::Cpa:
            return true;
        case Op::Mrs:  // TSD base from the thread-local cache (see trampoline_set_mrs_tls); MrsTpidr is rewritten in place by lower_range; Svc keeps BRK
            return g_mrs_tls >= 0 && in.rd != 18 && in.rd != 31;
            return !(in.op == Op::LdrAuth && in.writeback && in.rn == 31);  // SP writeback would need the frame SP written back
        default: return false;
    }
#else
    (void)in;
    return false;
#endif
}

uint32_t trampoline_make(uint32_t* site, uint32_t word, const Insn& in, uint64_t skip) {
#if defined(__aarch64__)
    if (!g_handler) return 0;
    const uint64_t pc = reinterpret_cast<uint64_t>(site);
    std::lock_guard<std::mutex> lk(g_mu);
    Chunk* ch = nullptr;
    for (auto& c : g_chunks)
        if (c.used + 16 <= kChunk && in_reach(c.base, pc) && in_reach(c.base + kChunk, pc)) { ch = &c; break; }
    if (!ch) ch = make_chunk(pc, skip);
    if (!ch) { if (!g_stats.no_island++ && getenv("DS_TRAMP_DEBUG")) fprintf(stderr, "tramp: no island near %p\n", (void*)site); return 0; }
    if (in.op == Op::Mrs) {  // mrs x18, tpidr_el0; ldr xd, [x18, #off]; b site+4
        uint32_t* stub = reinterpret_cast<uint32_t*>(ch->base + ch->used);
        ch->used += 16;
        stub[0] = 0xD53BD052u;
        stub[1] = 0xF9400000u | ((uint32_t(g_mrs_tls) / 8) << 10) | (18u << 5) | in.rd;
        stub[2] = 0x14000000u | (uint32_t((int64_t(pc + 4) - int64_t(reinterpret_cast<uint64_t>(stub + 2))) / 4) & 0x03FFFFFFu);
        stub[3] = 0xD503201Fu;
        __builtin___clear_cache(reinterpret_cast<char*>(stub), reinterpret_cast<char*>(stub) + 16);
        ++g_stats.sites;
        const int64_t to_stub = (int64_t(reinterpret_cast<uint64_t>(stub)) - int64_t(pc)) / 4;
        return 0x14000000u | (uint32_t(to_stub) & 0x03FFFFFFu);
    }
    g_sites.push_back(SiteInfo{in, pc, word});
    const SiteInfo* si = &g_sites.back();
    uint32_t* stub = reinterpret_cast<uint32_t*>(ch->base + ch->used);
    ch->used += 16;
    const int64_t to_common = (int64_t(ch->common) - int64_t(reinterpret_cast<uint64_t>(stub) + 4)) / 4;
    stub[0] = 0x58000052u;                                  // ldr x18, #8
    stub[1] = 0x14000000u | (uint32_t(to_common) & 0x03FFFFFFu);  // b common
    const uint64_t sip = reinterpret_cast<uint64_t>(si);
    std::memcpy(stub + 2, &sip, 8);
    __builtin___clear_cache(reinterpret_cast<char*>(stub), reinterpret_cast<char*>(stub) + 16);
    ++g_stats.sites;
    if (getenv("DS_TRAMP_DEBUG")) fprintf(stderr, "tramp: site=%p stub=%p common=%p chunk=%p\n", (void*)site, (void*)stub, (void*)ch->common, (void*)ch->base);
    const int64_t to_stub = (int64_t(reinterpret_cast<uint64_t>(stub)) - int64_t(pc)) / 4;
    return 0x14000000u | (uint32_t(to_stub) & 0x03FFFFFFu);  // b stub
#else
    (void)site; (void)word; (void)in; (void)skip;
    return 0;
#endif
}

TrampStats trampoline_stats() {
    std::lock_guard<std::mutex> lk(g_mu);
    return g_stats;
}

uint32_t encode_and_imm64(unsigned rd, uint64_t mask) {
    // one cyclic run of ones in 64 bits, N=1: imms = ones-1, immr = rotate-right amount of the run start
    if (mask == 0 || mask == ~0ull) return 0;
    for (unsigned immr = 0; immr < 64; ++immr)
        for (unsigned ones = 1; ones < 64; ++ones) {
            uint64_t base = (ones == 64) ? ~0ull : ((1ull << ones) - 1);
            uint64_t rot = immr ? ((base >> immr) | (base << (64 - immr))) : base;
            if (rot == mask) return 0x92400000u | (immr << 16) | ((ones - 1) << 10) | (rd << 5) | rd;
        }
    return 0;
}

}  // namespace ds::exec
