// Instruction classification and BRK lowering for ds-exec (milestone R2).
//
// Encodings below were produced with `clang --target=aarch64-linux-gnu
// -march=armv8.5-a+pauth+lse` + llvm-objdump (class VERIFIED, table in tests/test_exec.cpp).
// A word is lowered only when it matches exactly; everything else stays native.
#pragma once

#include <atomic>
#include <unordered_map>
#include <cstdint>
#include <string>

#include "pac.hpp"

namespace ds::exec {

enum class Op : uint8_t {
    None,
    PacSign,   // PACIA/PACIB/PACDA/PACDB (+Z forms) and PACIASP/PACIA1716/... hints
    PacAuth,   // AUT* and AUTIASP/AUTIA1716/... hints
    PacStrip,  // XPACI/XPACD/XPACLRI
    PacGa,
    BrAuth,    // BRAA*/BLRAA*
    RetAuth,   // RETAA/RETAB
    LdrAuth,   // LDRAA/LDRAB
    Cas,
    Casp,
    Rmw,       // LD<op>/ST<op>/SWP, all sizes and orderings
    Svc,       // svc #imm (Darwin uses #0x80)
    Mrs,       // mrs Xt, tpidrro_el0 (Darwin TSD base; reads 0 on Linux)
    Ldapur,    // FEAT_LRCPC2 LDAPUR{B,H,SB,SH,SW,}: emulated with an acquire (LDAR-class) load
    Cpa,       // FEAT_CPA ADDPT/SUBPT/MADDPT/MSUBPT (Arm ARM 2026-03 pseudocode, PointerAddCheck)
    MrsTpidr,  // mrs Xt, tpidr_el0: Darwin keeps a CPU/cluster hint there (libmalloc xzone index); the Linux host value is a TLS pointer, so emulate as 0
    Dot,       // FEAT_DotProd UDOT/SDOT (vector and by-element): expanded to ARMv8.0 widening multiply + pairwise adds
    Fp16,      // FP16 (FEAT_FP16) scalar ops: fmov/fcvt/fadd/… on H regs. Host is ARMv8.0 (no FP16), so lowered
    Fp16V,     // FP16 (FEAT_FP16) vector H ops (.4h/.8h): per-lane widen-compute-narrow via the trap path
    kCount
};

enum class Rmw : uint8_t { Add, Clr, Eor, Set, Smax, Smin, Umax, Umin, Swp, kCount };
enum class ModSrc : uint8_t { Zero, Reg, Sp, SpPc };  // SpPc: FEAT_PAuth_LR, SP combined with a PC (see emulate)

// FP16 (FEAT_FP16) decoded kinds, selected in Insn::hk (Op::Fp16 only).  Half-precision
// scalar ops run in single precision then FCVT back to half (a single rounding is exact
// when p_src >= 2*p_dst + 2); the FMA class runs in double precision for the same reason.
enum class Fp16Kind : uint8_t {
    FmovImm,    // fmov hd, #imm8          (imm = imm8 field)
    FmovRegH,   // fmov hd, hn             (bit copy, upper bits preserved)
    FmovHtoW,   // fmov wd, hn             (zero-extend low half)
    FmovWtoH,   // fmov hd, wn             (bits into low half, upper preserved)
    FcvtToH,    // fcvt hd, sn|dn          (size: 2=S, 1=D)
    FcvtFromH,  // fcvt sn|dn, hn          (size: 2=S, 1=D)
    Fadd,  Fsub,  Fmul,  Fdiv,
    Fmadd, Fmsub, Fnmadd, Fnmsub,           // sub=multiply-subtract, mul=negate-product
    Fcmp, Fcmp0, Fcsel, Fccmp,             // Fcmp: rm = compare reg (mul = signalling)
    Fminnm, Fmaxnm, Fabs, Fneg,
    Scvtf, Ucvtf,                           // rn = general register
    // Vector .4h/.8h
    FnegV, FabsV,
    FaddV, FsubV, FmulV, FdivV, FmaxV, FminV, FmaxnmV, FminnmV,
    FmlaV, FmlsV,
    FcmeqV, FcmgeV, FcmgtV, Fcmeq0V, Fcmge0V, Fcmgt0V,
    ScvtfV, UcvtfV, FcvtzsV, FcvtzuV, FcvtnuV,
    FrintiV, FrmV, MoviV, DupV,
    FaddpV, FmaxpV, FminpV,
    FmulIdxV, FmlaIdxV, FmlsIdxV,
};

struct Insn {
    Op op = Op::None;
    PacKey key = PacKey::IA;
    ModSrc mod = ModSrc::Zero;
    uint8_t rd = 0;      // pointer register (PAC), Xt (LDRAA/atomics), link flag below
    uint8_t rn = 0;      // base / branch target / value register
    uint8_t rm = 0;      // modifier register (mod == Reg)
    uint8_t rs = 0;      // CAS/atomic operand register
    uint8_t size = 0;    // log2 bytes (0..3); CASP: 2 = W pair, 3 = X pair
    bool acquire = false;
    bool release = false;
    bool link = false;   // BLRAA*
    bool writeback = false;
    bool sub = false;    // CPA: SUBPT/MSUBPT; FP16 FMA: product subtract
    bool mul = false;    // CPA: MADDPT/MSUBPT (Ra in rs, shift unused); FP16 FMA: negate product; FCMP: signalling
    int32_t imm = 0;     // LDRAA offset, SVC immediate, LDAPUR offset, PAuth_LR label distance in bytes (pc - label)
    uint8_t sext = 0;    // LDAPUR: 0 zero-extend, 1 sign-extend to X, 2 sign-extend to W
    Rmw rmw = Rmw::Add;
    int8_t lane = -1;    // Dot: by-element index (0..3), -1 = vector form
    bool sgn = false;    // Dot: SDOT
    bool q = false;      // Dot: 128-bit (4S) form
    Fp16Kind hk = Fp16Kind::FmovImm;  // Op::Fp16 only
};

const char* op_name(Op op);
const char* rmw_name(Rmw r);

// True when `w` is an instruction the host cannot execute faithfully; fills `out`.
bool decode(uint32_t w, Insn& out);

// FEAT_LRCPC LDAPR{,B,H}: not trap-lowered; lower_range rewrites them in place to the
// LDAR{,B,H} encoding (RCsc acquire is stronger than RCpc: a valid strengthening, counted in
// LowerCounts::ldapr_rewritten).  Returns true for those words.
bool is_known_unsupported(uint32_t w);

constexpr uint32_t brk_word(uint32_t idx) { return 0xD4200000u | (idx << 5); }

// Side table of original words, indexed by the BRK immediate.  Entries are written before
// the patched word becomes visible; `pc` is checked at trap time so a genuine `brk #n` in
// Apple code is never mistaken for a lowered instruction.
struct TrapEntry {
    uint64_t pc;
    uint32_t word;
    uint32_t pad;
};

class TrapTable {
public:
    static constexpr uint32_t kMax = (1u << 16) - 1;  // index 0xFFFF is the overflow marker
    static constexpr uint32_t kOverflowIdx = 0xFFFF;
    int64_t add(uint64_t pc, uint32_t word);  // -1 when full
    const TrapEntry* find(uint32_t idx, uint64_t pc) const;
    uint32_t size() const { return n_.load(std::memory_order_acquire); }

private:
    TrapEntry e_[kMax];
    std::atomic<uint32_t> n_{0};
    // BRK carries 16 bits: once kMax-1 entries exist, further ones use the reserved index 0xFFFF and are found by pc
    std::unordered_map<uint64_t, TrapEntry> overflow_;
    mutable std::atomic_flag overflow_lock_ = ATOMIC_FLAG_INIT;
};

TrapTable& trap_table();

struct LowerPolicy {
    bool pac = true;
    bool lse = true;
    bool svc = true;
    bool sysreg = true;  // mrs tpidrro_el0, LDAPUR
    bool ldapr = true;   // LDAPR -> LDAR rewrite
    bool simd = true;    // UDOT/SDOT
    // Fast paths (aarch64 runtime only; tests keep the BRK-only default):
    bool trampolines = false;  // rewrite to `b stub` (user-space emulation) where an island is in reach
    uint64_t island_skip = 16ull << 20;  // new island chunks start at least this far above the code (loaders map data segments behind it)
    uint64_t strip_mask = 0;   // nonzero: XPAC{I,D,LRI} become `and xd, xd, #strip_mask` in place (exact for pointers with bit 55 clear)
};

struct LowerCounts {
    uint64_t words = 0;
    uint64_t patched[static_cast<int>(Op::kCount)] = {};
    uint64_t unsupported_seen = 0;
    uint64_t trampolined = 0;      // words rewritten to a branch into a user-space stub
    uint64_t inlined_strip = 0;    // XPAC words rewritten to an inline AND
    uint64_t ldapr_rewritten = 0;  // LDAPR{,B,H} rewritten in place to LDAR{,B,H} (strengthening, see lower_range)
};

// Patch the words [begin, end) in place (memory must be writable; caller flushes the icache
// via flush_icache).  Returns false and sets *err when the side table is full.
bool lower_range(uint32_t* begin, uint32_t* end, const LowerPolicy& policy, LowerCounts& counts,
                 std::string* err);
void flush_icache(void* begin, void* end);

// Copy-based lowering (vm_remap) copies live, already lowered bytes into the destination. That
// copy holds BRK trap words keyed by the source PC and `b island` branches addressed from the
// source PC, so it cannot run at the destination. This rewrites dst[i] back to the original
// instruction wherever the source pc has a lowered entry (trap table or trampoline site), so the
// copy holds original words again and can be lowered fresh at its own address. Unpatched words
// already equal their originals and are left alone; inline in-place rewrites (strip AND, mov#0,
// LDAPR->LDAR) are PC-independent and also left alone. Returns the number of restored sites.
size_t restore_original(uint32_t* dst, const uint32_t* src_pc, size_t n);

}  // namespace ds::exec
