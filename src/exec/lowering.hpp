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
    kCount
};

enum class Rmw : uint8_t { Add, Clr, Eor, Set, Smax, Smin, Umax, Umin, Swp, kCount };
enum class ModSrc : uint8_t { Zero, Reg, Sp, SpPc };  // SpPc: FEAT_PAuth_LR, SP combined with a PC (see emulate)

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
    bool sub = false;    // CPA: SUBPT/MSUBPT
    bool mul = false;    // CPA: MADDPT/MSUBPT (Ra in rs, shift unused)
    int32_t imm = 0;     // LDRAA offset, SVC immediate, LDAPUR offset, PAuth_LR label distance in bytes (pc - label)
    uint8_t sext = 0;    // LDAPUR: 0 zero-extend, 1 sign-extend to X, 2 sign-extend to W
    Rmw rmw = Rmw::Add;
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

}  // namespace ds::exec
