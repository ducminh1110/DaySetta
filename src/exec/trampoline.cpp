#include "trampoline.hpp"

#include <sys/mman.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace ds::exec {

namespace {

constexpr uint64_t kReach = 127ull << 20;      // B imm26 reaches +-128 MB; keep a margin
constexpr size_t kChunk = 1024 * 1024;
constexpr unsigned kQOff = 288;                                         // v0-v15, v16-v31 (caller-saved vector registers) follow the GPR frame
constexpr unsigned kFrame = kQOff + 32 * 16, kRedZone = 128, kTotal = kFrame + kRedZone;  // 928, 16-aligned

struct Chunk {
    uint64_t base = 0, used = 0, common = 0, consts = 0;
};

const Pac* g_pac = nullptr;

std::mutex g_mu;
std::vector<Chunk> g_chunks;
std::deque<SiteInfo> g_sites;
std::unordered_map<uint64_t, const SiteInfo*> g_site_by_pc;  // guarded by g_mu; deque entries are node-stable
TrampHandler g_handler = nullptr;
TrampStats g_stats;
long g_mrs_tls = -1;

uint32_t enc_stp(unsigned a, unsigned b, int off) { return 0xA9000000u | ((uint32_t(off / 8) & 0x7F) << 15) | (b << 10) | (31u << 5) | a; }
uint32_t enc_ldp(unsigned a, unsigned b, int off) { return 0xA9400000u | ((uint32_t(off / 8) & 0x7F) << 15) | (b << 10) | (31u << 5) | a; }
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
        for (unsigned r : {0u, 2u, 4u, 6u, 8u, 10u, 12u, 14u, 16u, 18u, 20u, 22u, 24u, 26u, 28u, 30u}) { c.push_back(enc_stpq(r, r + 1, off)); off += 32; }
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
        for (unsigned r : {0u, 2u, 4u, 6u, 8u, 10u, 12u, 14u, 16u, 18u, 20u, 22u, 24u, 26u, 28u, 30u}) { c.push_back(enc_ldpq(r, r + 1, off)); off += 32; }
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
            if (g_pac) {  // constants for the inline PAC sequences
                uint64_t* k = reinterpret_cast<uint64_t*>(cand + ch.used);
                const PacKeys& pk = g_pac->keys();
                k[0] = 0x9E3779B97F4A7C15ull; k[1] = 0xBF58476D1CE4E5B9ull; k[2] = 0x94D049BB133111EBull;
                for (int i = 0; i < 4; ++i) { k[3 + i] = pk.lo[i]; k[7 + i] = pk.hi[i]; }
                ch.consts = cand + ch.used;
                ch.used += 11 * 8 + 8;
                ch.used = (ch.used + 15) & ~15ull;
            }
            __builtin___clear_cache(reinterpret_cast<char*>(p), reinterpret_cast<char*>(p) + ch.used);
            g_chunks.push_back(ch);
            ++g_stats.islands;
            return &g_chunks.back();
        }
    }
    return nullptr;
}


// ---- inline PAC sequences -------------------------------------------------------------------------------------------------
uint32_t enc_logic_imm(uint32_t base, unsigned rd, unsigned rn, uint64_t mask) {  // base: AND 0x92400000
    for (unsigned immr = 0; immr < 64; ++immr)
        for (unsigned ones = 1; ones < 64; ++ones) {
            const uint64_t run = (1ull << ones) - 1;
            const uint64_t rot = immr ? ((run >> immr) | (run << (64 - immr))) : run;
            if (rot == mask) return base | (immr << 16) | ((ones - 1) << 10) | (rn << 5) | rd;
        }
    return 0;
}
uint32_t enc_mul(unsigned rd, unsigned rn, unsigned rm) { return 0x9B007C00u | (rm << 16) | (rn << 5) | rd; }
uint32_t enc_eor(unsigned rd, unsigned rn, unsigned rm) { return 0xCA000000u | (rm << 16) | (rn << 5) | rd; }
uint32_t enc_eor_lsr(unsigned rd, unsigned rn, unsigned rm, unsigned sh) { return 0xCA400000u | (rm << 16) | (sh << 10) | (rn << 5) | rd; }
uint32_t enc_add(unsigned rd, unsigned rn, unsigned rm) { return 0x8B000000u | (rm << 16) | (rn << 5) | rd; }
uint32_t enc_orr(unsigned rd, unsigned rn, unsigned rm) { return 0xAA000000u | (rm << 16) | (rn << 5) | rd; }
uint32_t enc_mov(unsigned rd, unsigned rm) { return 0xAA0003E0u | (rm << 16) | rd; }
uint32_t enc_lsl(unsigned rd, unsigned rn, unsigned sh) { return 0xD3400000u | ((64 - sh) << 16) | ((63 - sh) << 10) | (rn << 5) | rd; }
uint32_t enc_ldr_lit(unsigned rt, int64_t from, int64_t to) { return 0x58000000u | ((uint32_t((to - from) / 4) & 0x7FFFF) << 5) | rt; }
uint32_t enc_b(int64_t from, int64_t to) { return 0x14000000u | (uint32_t((to - from) / 4) & 0x03FFFFFFu); }
uint32_t enc_cbnz(unsigned rt, int64_t from, int64_t to) { return 0xB5000000u | ((uint32_t((to - from) / 4) & 0x7FFFF) << 5) | rt; }
uint32_t enc_tbnz(unsigned rt, unsigned bit, int64_t from, int64_t to) {
    return 0x37000000u | ((bit >> 5) << 31) | ((bit & 31) << 19) | ((uint32_t((to - from) / 4) & 0x3FFF) << 5) | rt;
}

// Emit the inline sign/auth/branch-auth sequence for `in` at `stub`; empty when this form is not handled inline (caller uses the
// generic stub).  Preconditions that fail at run time (non-canonical pointer, authentication failure) branch to `slow`, the generic
// stub, which re-executes the exact emulation (poisoning, fault reports and statistics are unchanged).
std::vector<uint32_t> build_inline(const Insn& in, uint64_t pc, uint64_t stub, uint64_t slow, uint64_t consts) {
    std::vector<uint32_t> o;
    if (!g_pac || !consts) return o;
    const uint64_t FIELD = g_pac->field_mask(), BIT55 = 1ull << 55;
    const unsigned va = g_pac->va_bits();
    const PacKey key = in.key;
    const bool is_sign = in.op == Op::PacSign, is_auth = in.op == Op::PacAuth;
    const bool is_br = in.op == Op::BrAuth, is_ret = in.op == Op::RetAuth;
    if (!is_sign && !is_auth && !is_br && !is_ret) return o;
    if (key == PacKey::GA || in.mod == ModSrc::SpPc) return o;
    const unsigned P = is_ret ? 30u : is_br ? in.rn : in.rd;  // pointer register
    const ModSrc mod = is_ret ? ModSrc::Sp : in.mod;
    const unsigned M = in.rm;                                  // modifier register for ModSrc::Reg
    if (P == 31 || P == 18 || (mod == ModSrc::Reg && (M == 31 || M == 18))) return o;
    if (is_br && in.link && P == 30) return o;
    unsigned t[2], n = 0;  // temps a, b: x9..x15 not used by the instruction
    for (unsigned r = 9; r <= 15 && n < 2; ++r)
        if (r != P && !(mod == ModSrc::Reg && r == M)) t[n++] = r;
    const unsigned A = t[0], B = t[1];
    const int64_t kc1 = int64_t(consts), kc2 = kc1 + 8, kc3 = kc1 + 16;
    const int64_t kk0 = kc1 + 24 + 8 * int(key), kk1 = kc1 + 56 + 8 * int(key);
    auto at = [&](size_t idx) { return int64_t(stub) + int64_t(idx) * 4; };
    auto E = [&](uint32_t w) { o.push_back(w); };
    const uint32_t m_or = enc_logic_imm(0x92400000u, A, P, FIELD | BIT55);   // and a, P, #(field|bit55)
    const uint32_t m_not = enc_logic_imm(0x92400000u, B, P, ~FIELD);         // and b, P, #~field
    const uint32_t m_field = enc_logic_imm(0x92400000u, 18, 18, FIELD);      // and x18, x18, #field
    const uint32_t m_field_a = enc_logic_imm(0x92400000u, A, P, FIELD);      // and a, P, #field
    if (!m_or || !m_not || !m_field || !m_field_a) return {};
    E(enc_stp(A, B, -144));
    std::vector<size_t> fails;  // indices of the (placeholder) failure branches
    unsigned ptr_reg;           // register holding the PAC-free pointer used as MAC input
    if (is_sign) {
        E(m_or);
        fails.push_back(o.size()); E(0);   // cbnz a, fail
        ptr_reg = P;
    } else {
        fails.push_back(o.size()); E(0);   // tbnz P, #55, fail
        E(m_not);
        ptr_reg = B;
    }
    if (mod == ModSrc::Zero) {
        E(enc_mov(18, ptr_reg));
    } else {
        unsigned mreg = M;
        if (mod == ModSrc::Sp) { E(0x910003E0u | 18); mreg = 18; }  // mov x18, sp
        E(enc_ldr_lit(A, at(o.size()), kc1));
        E(enc_mul(18, mreg, A));
        E(enc_eor(18, 18, ptr_reg));
    }
    E(enc_ldr_lit(A, at(o.size()), kk0)); E(enc_eor(18, 18, A));
    E(enc_ldr_lit(A, at(o.size()), kc2)); E(enc_mul(18, 18, A));
    E(enc_eor_lsr(18, 18, 18, 32));
    E(enc_ldr_lit(A, at(o.size()), kk1)); E(enc_add(18, 18, A));
    E(enc_ldr_lit(A, at(o.size()), kc3)); E(enc_mul(18, 18, A));
    E(enc_eor_lsr(18, 18, 18, 29));
    E(enc_lsl(18, 18, va));
    E(m_field);
    if (is_sign) {
        E(enc_orr(P, P, 18));
    } else {
        E(m_field_a);
        E(enc_eor(A, A, 18));
        fails.push_back(o.size()); E(0);   // cbnz a, fail (signature mismatch)
        if (is_auth) E(enc_mov(P, B));
        else E(enc_mov(18, B));            // branch target
    }
    E(enc_ldp(A, B, -144));
    if (is_br || is_ret) {
        if (in.link) {  // blraa*: lr = site+4 (48-bit address)
            const uint64_t ret = pc + 4;
            E(0xD2800000u | uint32_t((ret & 0xFFFF) << 5) | 30u);
            E(0xF2A00000u | uint32_t(((ret >> 16) & 0xFFFF) << 5) | 30u);
            E(0xF2C00000u | uint32_t(((ret >> 32) & 0xFFFF) << 5) | 30u);
        }
        E(0xD61F0240u);                    // br x18
    } else {
        E(enc_b(at(o.size()), int64_t(pc + 4)));
    }
    const size_t fail = o.size();
    E(enc_ldp(A, B, -144));
    E(enc_b(at(o.size()), int64_t(slow)));
    for (size_t k = 0; k < fails.size(); ++k) {
        const size_t idx = fails[k];
        o[idx] = (k == 0 && !is_sign) ? enc_tbnz(P, 55, at(idx), at(fail)) : enc_cbnz(A, at(idx), at(fail));
    }
    return o;
}

// ---- inline LSE atomics (LL/SC loops; flags are never touched) -------------------------------------------------------------
std::vector<uint32_t> build_inline_atomic(const Insn& in, uint64_t pc, uint64_t stub) {
    std::vector<uint32_t> o;
    if (in.op != Op::Cas && in.op != Op::Rmw) return o;
    if (in.op == Op::Rmw && in.rmw != Rmw::Add && in.rmw != Rmw::Clr && in.rmw != Rmw::Eor && in.rmw != Rmw::Set && in.rmw != Rmw::Swp) return o;
    if (in.op == Op::Cas && in.rs == 31) return o;
    const unsigned sz = in.size;  // 0 B, 1 H, 2 W, 3 X
    const unsigned rn = in.rn, rs = in.rs, rd = in.rd;
    unsigned t[3], n = 0;
    for (unsigned r = 9; r <= 15 && n < 3; ++r)
        if (r != rn && r != rs && r != rd) t[n++] = r;
    const unsigned A = t[0], B = t[1], C = t[2];
    auto at = [&](size_t idx) { return int64_t(stub) + int64_t(idx) * 4; };
    auto E = [&](uint32_t w) { o.push_back(w); };
    const uint32_t sf = sz == 3 ? 0x80000000u : 0;  // 64-bit data processing for X, 32-bit for B/H/W
    E(enc_stp(A, B, -144));
    E(0xF9000000u | 0 | (31u << 5) | C);  // placeholder replaced below (str c,[sp,#-152] needs a signed-offset STUR)
    o.back() = 0xF8000000u | (((uint32_t(-152)) & 0x1FF) << 12) | (31u << 5) | C;  // stur xC, [sp, #-152]
    const size_t loop = o.size();
    E((in.acquire ? 0x085FFC00u : 0x085F7C00u) | (sz << 30) | (rn << 5) | A);       // ldxr/ldaxr a, [rn]
    std::vector<size_t> done_br;
    if (in.op == Op::Cas) {
        E((sz == 3 ? 0xCA000000u : 0x4A000000u) | (rs << 16) | (A << 5) | C);       // eor c, a, rs
        if (sz == 0) E(0x12001C00u | (C << 5) | C);                                  // and wc, wc, #0xff
        if (sz == 1) E(0x12003C00u | (C << 5) | C);                                  // and wc, wc, #0xffff
        done_br.push_back(o.size()); E(0);                                           // cbnz c, done
        E((in.release ? 0x0800FC00u : 0x08007C00u) | (sz << 30) | (B << 16) | (rn << 5) | rd);  // stxr/stlxr b, rt, [rn]
    } else {
        switch (in.rmw) {
            case Rmw::Add: E((sz == 3 ? 0x8B000000u : 0x0B000000u) | (rs << 16) | (A << 5) | C); break;
            case Rmw::Clr: E((sz == 3 ? 0x8A200000u : 0x0A200000u) | (rs << 16) | (A << 5) | C); break;
            case Rmw::Eor: E((sz == 3 ? 0xCA000000u : 0x4A000000u) | (rs << 16) | (A << 5) | C); break;
            case Rmw::Set: E((sz == 3 ? 0xAA000000u : 0x2A000000u) | (rs << 16) | (A << 5) | C); break;
            default:       E((sz == 3 ? 0xAA0003E0u : 0x2A0003E0u) | (rs << 16) | C); break;  // swp: c = rs
        }
        E((in.release ? 0x0800FC00u : 0x08007C00u) | (sz << 30) | (B << 16) | (rn << 5) | C);  // stxr/stlxr b, c, [rn]
    }
    {   // cbnz wB, loop
        const int64_t from = at(o.size());
        E(0x35000000u | ((uint32_t((at(loop) - from) / 4) & 0x7FFFF) << 5) | B);
    }
    const size_t done = o.size();
    if (in.op == Op::Cas) E((sz == 3 ? 0xAA0003E0u : 0x2A0003E0u) | (A << 16) | rs);   // mov rs, a (old value)
    else if (rd != 31) E((sz == 3 ? 0xAA0003E0u : 0x2A0003E0u) | (A << 16) | rd);        // mov rt, a
    for (size_t idx : done_br) {
        const int64_t from = at(idx);
        o[idx] = (sz == 3 ? 0xB5000000u : 0x35000000u) | ((uint32_t((at(done) - from) / 4) & 0x7FFFF) << 5) | C;
    }
    E(enc_ldp(A, B, -144));
    E(0xF8400000u | (((uint32_t(-152)) & 0x1FF) << 12) | (31u << 5) | C);  // ldur xC, [sp, #-152]
    E(enc_b(at(o.size()), int64_t(pc + 4)));
    (void)sf;
    return o;
}

// ---- UDOT/SDOT as ARMv8.0 Advanced SIMD (flags and GPRs untouched) --------------------------------------------------------
// q0 = [u|s]mull t0.8h, n.8b, m.8b ; [u|s]mull2 t1.8h, n.16b, m.16b ; [u|s]addlp t0/t1.4s ; addp t0.4s, t0, t1 ; add d, d, t0.
// Products fit 16 bits and pair sums 32 bits, so the result equals the architectural modular sum.  Scratch vectors are spilled
// below the 128-byte red zone (sp moved first, so a signal on this stack cannot clobber them).
std::vector<uint32_t> build_inline_dot(const Insn& in, uint64_t pc, uint64_t stub) {
    std::vector<uint32_t> o;
    unsigned t[3], k = 0;
    for (unsigned r = 0; r < 32 && k < 3; ++r)
        if (r != in.rd && r != in.rn && r != in.rm) t[k++] = r;
    const unsigned T0 = t[0], T1 = t[1], T2 = t[2], N = in.rn, D = in.rd;
    auto E = [&](uint32_t w) { o.push_back(w); };
    E(0xD102C3FFu);                                  // sub sp, sp, #176
    E(enc_stpq(T0, T1, 0));
    E(0x3D800BE0u | T2);                             // str qT2, [sp, #32]
    unsigned M = in.rm;
    if (in.lane >= 0) {                              // dup t2.4s, m.s[lane]
        E(0x4E000400u | ((uint32_t(in.lane) << 3 | 4u) << 16) | (in.rm << 5) | T2);
        M = T2;
    }
    const uint32_t mull = in.sgn ? 0x0E20C000u : 0x2E20C000u, addlp = in.sgn ? 0x4E602800u : 0x6E602800u;
    E(mull | (M << 16) | (N << 5) | T0);
    if (in.q) E(mull | 0x40000000u | (M << 16) | (N << 5) | T1);
    E(addlp | (T0 << 5) | T0);
    if (in.q) E(addlp | (T1 << 5) | T1);
    E(0x4EA0BC00u | ((in.q ? T1 : T0) << 16) | (T0 << 5) | T0);   // addp t0.4s, t0.4s, (t1|t0).4s
    E((in.q ? 0x4EA08400u : 0x0EA08400u) | (T0 << 16) | (D << 5) | D);  // add d.<4s|2s>, d, t0
    E(enc_ldpq(T0, T1, 0));
    E(0x3DC00BE0u | T2);                             // ldr qT2, [sp, #32]
    E(0x9102C3FFu);                                  // add sp, sp, #176
    E(enc_b(int64_t(stub) + int64_t(o.size()) * 4, int64_t(pc + 4)));
    return o;
}

// ---- FP16 scalar ops via FP16-era semantics on ARMv8.0 (single precision for 2-source
// arithmetic, double precision for FMA).  FCVT H<->S/D is ARMv8.0 (assembled with
// -mattr=+v8-a); the destination H is written by FCVT, which preserves the upper bits of
// the Q register (ARM semantics match the FP16 instruction for the low half).  Scratch V
// registers are spilled below the 128-byte red zone like build_inline_dot.
std::vector<uint32_t> build_inline_fp16(const Insn& in, uint64_t pc, uint64_t stub) {
    std::vector<uint32_t> o;
    unsigned t[3], k = 0;
    for (unsigned r = 0; r < 32 && k < 3; ++r)
        if (r != in.rd && r != in.rn && r != in.rm && r != in.rs) t[k++] = r;
    const unsigned T0 = t[0], T1 = t[1], T2 = t[2];
    const unsigned D = in.rd, N = in.rn, M = in.rm, A = in.rs;
    auto E = [&](uint32_t w) { o.push_back(w); };
    // stp X30-save by dot: sub sp; save T0/T1/T2; work; restore; add sp; b pc+4..
    E(0xD102C3FFu);                                  // sub sp, sp, #176
    E(enc_stpq(T0, T1, 0));
    E(0x3D800BE0u | T2);                             // str qT2, [sp, #32]
    auto h2s = [&](unsigned src, unsigned dst) { E(0x1EE24000u | (src << 5) | dst); };  // fcvt s<dst>, h<src>
    auto h2d = [&](unsigned src, unsigned dst) { E(0x1EE2C000u | (src << 5) | dst); };  // fcvt d<dst>, h<src>
    auto s2h = [&](unsigned src, unsigned dst) { E(0x1E23C000u | (src << 5) | dst); };  // fcvt h<dst>, s<src>
    auto d2h = [&](unsigned src, unsigned dst) { E(0x1E63C000u | (src << 5) | dst); };  // fcvt h<dst>, d<src>
    // Stack slot helpers: str/ldr at [sp, #48] (STR W/STRH/LDRH/LDR H are baseline).
    auto strW = [&](unsigned src) { E(0xB9000000u | (12u << 10) | (31u << 5) | src); };
    auto strH = [&](unsigned src) { E(0x7D000000u | (12u << 10) | (31u << 5) | src); };
    auto ldrH = [&](unsigned dst) { E(0x7D400000u | (12u << 10) | (31u << 5) | dst); };
    auto ldrhW = [&](unsigned dst) { E(0x79400000u | (12u << 10) | (31u << 5) | dst); };
    auto fmovS = [&](unsigned dst, uint32_t imm) { E(dst == 0 ? 0x1E201000u | imm : 0u); };
    (void)fmovS;
    switch (in.hk) {
        case Fp16Kind::FmovImm:
            E((0x1E2E1000u & ~(0xFFu << 13) & ~0x1Fu) | ((uint32_t(in.imm) & 0xFFu) << 13) | T0);  // fmov sT, #imm8 (S) then convert
            s2h(T0, D);
            break;
        case Fp16Kind::FmovRegH: {  // hD = hN: bit copy
            strH(M); ldrH(D);
            break;
        }
        case Fp16Kind::FmovWtoH: {  // hD = Wn: str W then LDR H
            strW(M); ldrH(D);
            break;
        }
        case Fp16Kind::FmovHtoW: {  // Wd = Hn (zero-extend low half)
            strH(M); ldrhW(D);
            break;
        }
        case Fp16Kind::FcvtToH: {
            if (in.size == 2) { h2s(N, T0); s2h(T0, D); } else { h2d(N, T0); d2h(T0, D); }
            break;
        }
        case Fp16Kind::FcvtFromH: {
            if (in.size == 2) { h2s(N, T0); E(0x1EE24000u | (D << 5) | T0); }
            else { h2d(N, T0); E(0x1EE2C000u | (D << 5) | T0); }
            break;
        }
        case Fp16Kind::Fadd:
        case Fp16Kind::Fsub:
        case Fp16Kind::Fmul:
        case Fp16Kind::Fdiv: {  // compute in S, single rounding is exact (see file header)
            h2s(N, T0); h2s(M, T1);
            switch (in.hk) {
                case Fp16Kind::Fadd: E((0x1E202800u) | (T1 << 16) | (T0 << 5) | T0); break;
                case Fp16Kind::Fsub: E((0x1E203800u) | (T1 << 16) | (T0 << 5) | T0); break;
                case Fp16Kind::Fmul: E((0x1E201800u) | (T1 << 16) | (T0 << 5) | T0); break;
                case Fp16Kind::Fdiv: E((0x1E201800u) | (T1 << 16) | (T0 << 5) | T0); break;
                default: break;
            }
            s2h(T0, D);
            break;
        }
        case Fp16Kind::Fmadd:
        case Fp16Kind::Fmsub:
        case Fp16Kind::Fnmadd:
        case Fp16Kind::Fnmsub: {  // compute the full FMA in D, one rounding
            h2d(N, T0); h2d(M, T1); h2d(A, T2);
            switch (in.hk) {
                case Fp16Kind::Fmadd: E((0x1F400800u & ~0x1FC7FFu) | (T1 << 16) | (T2 << 10) | (T0 << 5) | T2); break;
                case Fp16Kind::Fmsub: E((0x1F408800u & ~0x1FC7FFu) | (T1 << 16) | (T2 << 10) | (T0 << 5) | T2); break;
                case Fp16Kind::Fnmadd: E((0x1F600800u & ~0x1FC7FFu) | (T1 << 16) | (T2 << 10) | (T0 << 5) | T2); break;
                case Fp16Kind::Fnmsub: E((0x1F608800u & ~0x1FC7FFu) | (T1 << 16) | (T2 << 10) | (T0 << 5) | T2); break;
                default: break;
            }
            d2h(T2, D);
            break;
        }
        case Fp16Kind::Fcmp: {
            h2s(N, T0); h2s(M, T1);
            E((0x1E212000u & ~0x1F3FFu) | (T1 << 16) | (T0 << 5) | T0);  // fcmp sT0, sT1 (NaN-aware flags)
            break;
        }
        case Fp16Kind::Fcmp0: {
            h2s(N, T0);
            E((0x1E202008u & ~0x3E0u) | (T0 << 5));  // fcmp sT0, #0.0
            break;
        }
        case Fp16Kind::Fcsel: {
            h2s(N, T0); h2s(M, T1); h2s(A, T2);
            E(0x1E218C02u | (T1 << 16) | (T2 << 10) | (T0 << 5) | T0);  // fcsel sD, s1, s2, cond
            s2h(T0, D);
            break;
        }
        case Fp16Kind::Fccmp: {
            h2s(N, T0); h2s(M, T1);
            E((0x1E218408u & ~0x1F3FFu) | (T1 << 16) | (in.rs << 10) | (in.imm << 12) | (T0 << 5) | T0);  // fccmp sT0, sT1, #nzcv, cond
            break;
        }
        case Fp16Kind::Fminnm: {
            h2s(N, T0); h2s(M, T1); E(0x1E226800u | (T1 << 16) | (T0 << 5) | T0); s2h(T0, D); break;  // fminnm sT0, sT0, sT1
        }
        case Fp16Kind::Fmaxnm: {
            h2s(N, T0); h2s(M, T1); E((0x1E226800u & ~0x1F3FFu) | (T1 << 16) | (T0 << 5) | T0); s2h(T0, D); break;  // fmaxnm sT0, sT0, sT1
        }
        case Fp16Kind::Fabs: {
            h2s(N, T0); E(0x1EE0C400u | (T0 << 5) | T0); s2h(T0, D); break;  // fabs sT0, sT0
        }
        case Fp16Kind::Fneg: {
            h2s(N, T0); E(0x1EE14000u | (T0 << 5) | T0); s2h(T0, D); break;  // fneg sT0, sT0
        }
        case Fp16Kind::Scvtf: {
            E(0x1E390C00u | (M << 5) | T0); s2h(T0, D); break;  // scvtf sT, wN  (baseline)
        }
        case Fp16Kind::Ucvtf: {
            E(0x1E798C00u | (M << 5) | T0); s2h(T0, D); break;  // ucvtf sT, wN
        }
        default: break;
    }
    E(enc_ldpq(T0, T1, 0));
    E(0x3DC00BE0u | T2);                             // ldr qT2, [sp, #32]
    E(0x9102C3FFu);                                  // add sp, sp, #176
    E(enc_b(int64_t(stub) + int64_t(o.size()) * 4, int64_t(pc + 4)));
    return o;
}

}  // namespace

void trampoline_set_pac(const Pac* p) { g_pac = p; }
void trampoline_set_handler(TrampHandler h) { g_handler = h; }
void trampoline_set_mrs_tls(long off) { g_mrs_tls = (off >= 0 && off < 32760 && (off & 7) == 0) ? off : -1; }

bool trampoline_wants(const Insn& in) {
#if defined(__aarch64__)
    if (!g_handler) return false;
    switch (in.op) {
        case Op::PacSign: case Op::PacAuth: case Op::PacGa: case Op::BrAuth: case Op::RetAuth: case Op::LdrAuth:
        case Op::Cas: case Op::Casp: case Op::Rmw: case Op::Ldapur: case Op::Cpa: case Op::Dot:
            // FP16 inline sequences disabled until they match the trap path (see test);
            // Fp16 words lower to BRK and are handled exactly by emulate(). Same for
            // Op::Fp16V (vector forms): the trap path computes per-lane values.
            return true;
        case Op::Fp16:
        case Op::Fp16V:
            return false;
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
        g_sites.push_back(SiteInfo{in, pc, word});
        g_site_by_pc[pc] = &g_sites.back();
        const int64_t to_stub = (int64_t(reinterpret_cast<uint64_t>(stub)) - int64_t(pc)) / 4;
        return 0x14000000u | (uint32_t(to_stub) & 0x03FFFFFFu);
    }
    if (in.op == Op::Dot) {  // self-contained SIMD sequence; no generic stub (the host handler frame does not carry V8-V15)
        auto code = build_inline_dot(in, pc, ch->base + ch->used);
        if (ch->used + code.size() * 4 + 16 > kChunk - 1024) return 0;
        uint32_t* istub = reinterpret_cast<uint32_t*>(ch->base + ch->used);
        std::memcpy(istub, code.data(), code.size() * 4);
        ch->used += (code.size() * 4 + 15) & ~15ull;
        __builtin___clear_cache(reinterpret_cast<char*>(istub), reinterpret_cast<char*>(istub) + code.size() * 4);
        ++g_stats.sites;
        ++g_stats.inlined;
        g_sites.push_back(SiteInfo{in, pc, word});
        g_site_by_pc[pc] = &g_sites.back();
        const int64_t to_i = (int64_t(reinterpret_cast<uint64_t>(istub)) - int64_t(pc)) / 4;
        return 0x14000000u | (uint32_t(to_i) & 0x03FFFFFFu);
    }
    if (in.op == Op::Fp16) {  // self-contained ARMv8.0 sequence (see build_inline_fp16)
        auto code = build_inline_fp16(in, pc, ch->base + ch->used);
        if (ch->used + code.size() * 4 + 16 > kChunk - 1024) return 0;
        uint32_t* istub = reinterpret_cast<uint32_t*>(ch->base + ch->used);
        std::memcpy(istub, code.data(), code.size() * 4);
        ch->used += (code.size() * 4 + 15) & ~15ull;
        __builtin___clear_cache(reinterpret_cast<char*>(istub), reinterpret_cast<char*>(istub) + code.size() * 4);
        ++g_stats.sites;
        ++g_stats.inlined;
        g_sites.push_back(SiteInfo{in, pc, word});
        g_site_by_pc[pc] = &g_sites.back();
        const int64_t to_i = (int64_t(reinterpret_cast<uint64_t>(istub)) - int64_t(pc)) / 4;
        return 0x14000000u | (uint32_t(to_i) & 0x03FFFFFFu);
    }
    g_sites.push_back(SiteInfo{in, pc, word});
    g_site_by_pc[pc] = &g_sites.back();
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
    if (!getenv("DS_TRAMP_NO_INLINE") && !((in.op == Op::Cas || in.op == Op::Rmw) && getenv("DS_TRAMP_NO_INLINE_ATOMIC"))) {  // fast path: inline PAC sequence in front of the generic stub, falling back to it
        auto code = (in.op == Op::Cas || in.op == Op::Rmw) ? build_inline_atomic(in, pc, ch->base + ch->used)
                                                            : build_inline(in, pc, ch->base + ch->used, reinterpret_cast<uint64_t>(stub), ch->consts);
        if (!code.empty() && ch->used + code.size() * 4 + 16 <= kChunk - 1024) {
            uint32_t* istub = reinterpret_cast<uint32_t*>(ch->base + ch->used);
            std::memcpy(istub, code.data(), code.size() * 4);
            ch->used += (code.size() * 4 + 15) & ~15ull;
            __builtin___clear_cache(reinterpret_cast<char*>(istub), reinterpret_cast<char*>(istub) + code.size() * 4);
            ++g_stats.inlined;
            const int64_t to_i = (int64_t(reinterpret_cast<uint64_t>(istub)) - int64_t(pc)) / 4;
            return 0x14000000u | (uint32_t(to_i) & 0x03FFFFFFu);
        }
    }
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

const SiteInfo* trampoline_lookup(uint64_t pc) {
    std::lock_guard<std::mutex> lk(g_mu);
    auto it = g_site_by_pc.find(pc);
    return it == g_site_by_pc.end() ? nullptr : it->second;
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
