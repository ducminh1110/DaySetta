// Trap engine: emulates lowered instructions (see lowering.hpp) from SIGTRAP, reports SIGILL
// and instruction-fetch faults precisely, and lowers executable pages lazily on first fetch.
#pragma once

#include <atomic>
#include <cstdint>
#include <string>

#include "lowering.hpp"
#include "pac.hpp"

namespace ds::exec {

// Register file as seen by the emulator; marshalled from/to ucontext by the handler so the
// emulator itself can be unit-tested on a synthetic state.
struct CpuState {
    uint64_t x[31] = {};
    uint64_t sp = 0;
    uint64_t pc = 0;
    uint64_t pstate = 0;
    // Emulated EffectiveCPTA/CPTM (SCTLR2_EL1.CPTA0/CPTM0 at EL0).  Both 0 = ADDPT behaves as ADD.
    bool cpta = false, cptm = false;
    uint64_t tpidrro = 0;  // emulated TPIDRRO_EL0 (Darwin TSD base), loaded by the handler
    uint8_t (*v)[16] = nullptr;  // V0-V31 (signal frame fpsimd_context), null when unavailable
    uint64_t get(unsigned r) const { return r == 31 ? 0 : x[r]; }       // XZR
    uint64_t get_sp(unsigned r) const { return r == 31 ? sp : x[r]; }  // SP
    void set(unsigned r, uint64_t v) { if (r != 31) x[r] = v; }
    void set_sp(unsigned r, uint64_t v) { if (r == 31) sp = v; else x[r] = v; }
};

constexpr uint64_t kPstateC = uint64_t(1) << 29;
constexpr uint64_t kDarwinENOSYS = 78;  // Darwin <sys/errno.h>; see tests/test_exec.cpp for the cited source

enum class Emu : uint8_t {
    Ok,
    AuthFault,   // combined auth failed: pc (branch) or fault_addr (load) is poisoned
    Misaligned,  // exclusive/atomic access with a misaligned address (alignment fault on HW)
    Bad          // entry not emulatable (cannot happen for decode()d instructions)
};

struct EmuOutcome {
    Emu status = Emu::Ok;
    uint64_t fault_addr = 0;
    bool svc = false;
    int64_t svc_x16 = 0;
    uint32_t svc_imm = 0;
};

struct TrapStats {
    std::atomic<uint64_t> by_op[static_cast<int>(Op::kCount)] = {};
    std::atomic<uint64_t> by_rmw[static_cast<int>(Rmw::kCount)] = {};
    std::atomic<uint64_t> auth_fail{0};
    std::atomic<uint64_t> brk_traps{0};   // SIGTRAP round trips (the slow path)
    std::atomic<uint64_t> tramp_calls{0}; // user-space trampoline executions (the fast path)
    std::atomic<uint64_t> lazy_pages{0};
    uint64_t total() const {
        uint64_t t = 0;
        for (auto& c : by_op) t += c.load();
        return t;
    }
};

// Cross-ABI rule (arm64e Apple code calling plain-arm64 Darling shim code): a branch-with-authentication whose pointer
// carries NO signature (PAC field zero) and whose target lies in a registered plain-arm64 executable range is accepted
// unsigned.  A signed pointer that fails authentication, or an unsigned one outside the ranges, still faults.
// The reverse direction (plain code using an arm64e pointer) is the shim's job: it strips with strip_ptr().
void plain_range_add(uint64_t start, uint64_t size);
bool plain_range_contains(uint64_t addr);

// Emulate one decoded instruction at cpu.pc (advances pc, writes registers/memory).
EmuOutcome emulate(const Insn& in, CpuState& cpu, const Pac& pac, TrapStats& stats);

struct TrapRuntime {
    const Pac* pac = nullptr;
    TrapStats stats;
    // describe(pc) -> "image+0xoff" for reports; may be null.
    std::string (*describe)(uint64_t pc, void* ctx) = nullptr;
    // lazy_lower(pc): lower + make executable the page of an instruction fetch fault in the
    // mapped cache; false when pc is not in a lowerable range.  May be null.
    bool (*lazy_lower)(uint64_t pc, void* ctx, std::string* err) = nullptr;
    // lazy_slide(addr): slide the page a faulting data access touched (lazily slid cache mappings); true = retry the access.
    bool (*lazy_slide)(uint64_t addr, void* ctx) = nullptr;
    void* ctx = nullptr;
    bool log_svc = true;
    // Darwin syscall hook: called after a lowered `svc` was emulated (x0/carry preset to ENOSYS);
    // may rewrite x0/pstate carry/pc.  Null keeps the ENOSYS default.
    void (*svc)(CpuState& cpu, uint32_t imm, void* ctx) = nullptr;
    // Value of the calling thread's emulated TPIDRRO_EL0; null reads 0.
    uint64_t (*tsd)(void* ctx) = nullptr;
    // Called after every successfully emulated trap (stats dumping); may be null.
    void (*after_trap)(void* ctx) = nullptr;
};

// Install SIGTRAP/SIGILL/SIGSEGV handlers (SA_SIGINFO|SA_ONSTACK) for the process.
bool install_trap_handlers(TrapRuntime* rt);
// For runtimes that own the signal handlers (Darling's sigexc): emulate the lowered instruction
// at the SIGTRAP context `uc` (a ucontext_t*).  False = not a lowered instruction (the BRK is the
// guest's own; nothing changed, nothing printed).  Requires install_trap_handlers' TrapRuntime.
bool handle_sigtrap(void* uc, uint64_t tsd = 0);  // tsd: emulated TPIDRRO_EL0 when TrapRuntime::tsd is null
// Instruction-fetch fault in a lazily lowered range (TrapRuntime::lazy_lower): lowers the page and returns true
// so the fetch can be retried.  `si` is a siginfo_t* (si_code/si_addr).  Nothing is printed on false.
bool handle_sigsegv(void* uc, void* si);
// Give the calling thread an alternate signal stack (the installing thread gets one already).
void thread_init();

}  // namespace ds::exec
