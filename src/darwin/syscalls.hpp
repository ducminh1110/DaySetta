// Darwin syscall / Mach trap layer for ds-run.
//
// ABI (xnu osfmk/arm64/sleh.c:2743-2790 handle_svc, bsd/dev/arm/systemcalls.c): `svc #0x80` with
// the call number in x16 (as a signed int).  n > 0: BSD syscall; n < 0: Mach trap (-n), with the
// two fast traps MACH_ARM_TRAP_ABSTIME (-3) and MACH_ARM_TRAP_CONTTIME (-4)
// (osfmk/mach/arm/traps.h:36-37); n == PLATFORM_SYSCALL_TRAP_NO (0x80000000,
// osfmk/arm64/proc_reg.h:2885) is the machdep call selected by x3 (osfmk/arm64/machine_routines.c,
// platform_syscall: 2 = set TSD base from x0, 3 = get TSD base into x0).
// BSD result convention: success -> x0 = value, carry clear; error -> x0 = Darwin errno, carry
// set.  Mach traps return kern_return_t / port name in x0 and leave the flags alone.
//
// Numbers are never typed here: handlers register by *name* and the number comes from the
// generated sysnames.inc (xnu syscalls.master / syscall_sw.c).
#pragma once

#include <cstdint>
#include <cstdio>
#include <map>
#include <utility>
#include <mutex>
#include <string>

#include "exec/trap.hpp"

namespace ds::darwin {

struct SysRet {
    int64_t v = 0;   // x0 on success
    int64_t v1 = 0;  // x1 (second return value)
    int err = 0;     // Darwin errno, 0 = success
};

struct SysContext;
using BsdHandler = SysRet (*)(SysContext&, const uint64_t a[8]);
using MachHandler = int64_t (*)(SysContext&, const uint64_t a[8]);  // returns x0 (kern_return_t / name)

struct SysContext {
    uint64_t shared_cache_base = 0;  // returned by shared_region_check_np; 0 = no region (ENOMEM)
    uint64_t main_mh = 0;
    bool trace = false;
    std::string (*describe)(uint64_t pc) = nullptr;  // "image+0xoff" for traces
    uint64_t current_pc = 0;                          // pc of the svc being handled
    // statistics
    std::map<std::string, uint64_t> hist;
    std::map<std::string, uint64_t> unimplemented;
    std::map<std::string, std::string> stubbed;  // name -> reason (explicit stubs that were hit)
    std::mutex mu;
    // Mach port namespace (opaque names; the values below are ours, not Apple constants).
    uint32_t next_port = 0x1103;
    // (st_dev, st_ino) -> path for fsgetpath(2): the files whose ids we hand to the guest (the cache).
    std::map<std::pair<int32_t, uint64_t>, std::string> fileids;
};

const char* bsd_name(int n);   // "?" when unknown
const char* mach_name(int n);  // "?" when unknown

// Lookup tables are built from names; exposed for tests.
BsdHandler find_bsd(const std::string& name);
int bsd_number(const std::string& name);  // -1 if unknown
int mach_number(const std::string& name);

void set_tsd_base(uint64_t v);  // per-thread emulated TPIDRRO_EL0
uint64_t tsd_base();

// Handle one trapped svc: reads x16/x0.., writes x0 and the carry flag.  Exit calls do not return.
void dispatch_svc(SysContext& ctx, ds::exec::CpuState& cpu);
void print_summary(SysContext& ctx, FILE* out);

}  // namespace ds::darwin
