// User-space trampolines for lowered instructions (replaces BRK + SIGTRAP on the hot path).
//
// A lowered PAC/LSE/... word at `site` becomes `b stub`; the stub (16 bytes in an RWX island within +-127 MB of the site)
// loads the SiteInfo pointer into x18 (Darwin's reserved platform register, never used by Apple code) and branches to the
// island's common block, which saves x0-x30 + NZCV below the guest's red zone, calls ds_tramp_c(frame, site) on the guest
// stack, restores everything and continues at frame[NEXT] (site+4, or the branch target) through x18.  Explicit emulation
// is unchanged: ds_tramp_c runs the same emulate() as the SIGTRAP path.
//
// A `b stub` word is PC-relative: it is only valid at its own site.  Copying lowered bytes to
// another address (vm_remap with copy) therefore yields a branch that lands nowhere.  Sites are
// recorded so a copy can have its original words restored (see restore_original in lowering.hpp).
#pragma once

#include <cstddef>
#include <cstdint>

#include "lowering.hpp"
#include "pac.hpp"

namespace ds::exec {

struct SiteInfo {
    Insn in;
    uint64_t pc;
    uint32_t word;
};

// Recorded site whose original word is `word` at `pc`, or null when `pc` was not lowered
// through a trampoline.  Restores vm_remap copies of lowered bytes: the destination holds a
// stale PC-relative `b stub`, so it must be rewritten to the original word before it runs.
const SiteInfo* trampoline_lookup(uint64_t pc);

// Frame written by the common block (offsets in 8-byte slots): x0..x30 = 0..30, NZCV = 31, guest SP = 32, NEXT = 33.
constexpr int kFrameX = 0, kFrameNzcv = 31, kFrameSp = 32, kFrameNext = 33;

// Handler called by the common block.  Set once by the trap engine; null disables trampolines.
using TrampHandler = void (*)(uint64_t* frame, const SiteInfo* site);
void trampoline_set_handler(TrampHandler h);
// Pac used by the inline sign/auth sequences (keys are baked into the island constant blocks; set before the first island is made).
void trampoline_set_pac(const Pac* p);

// True when `op` is executed through a trampoline (when an island is in reach); others keep the BRK path.
bool trampoline_wants(const Insn& in);

// Build the stub for `site` (original `word` decoded as `in`); returns the `b stub` word to write at `site`,
// or 0 when no island within reach could be created.  Thread-safe.
uint32_t trampoline_make(uint32_t* site, uint32_t word, const Insn& in, uint64_t skip = 0);
// `skip`: do not place a new island chunk closer than this above `site` (a loader may still map segments right behind the code).

// `mrs xN, tpidrro_el0` stubs read the thread's TSD base from a thread-local at TPIDR_EL0+off (no call, no frame).  off<0: disabled.
void trampoline_set_mrs_tls(long off);

struct TrampStats {
    uint64_t sites = 0, islands = 0, no_island = 0, inlined = 0;
};
TrampStats trampoline_stats();

// AArch64 encoder of `and xd, xd, #mask` for masks that are one cyclic run of ones (used for the inline XPAC); 0 if not encodable.
uint32_t encode_and_imm64(unsigned rd, uint64_t mask);

}  // namespace ds::exec
