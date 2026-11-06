# ESC-D5-darlingserver: task_restartable_ranges_* (genuine 27 libobjc calls it at init)

Genuine 27 libobjc (libobjc+0x19e8) calls `task_restartable_ranges_register(mach_task_self(), ranges, n)` and only tolerates
result 0, 6 (KERN_RESOURCE_SHORTAGE) or -303 (MIG_BAD_ID); anything else takes an abort path.
Darling's libsystem_kernel had no such symbol (dyld: "symbol '_task_restartable_ranges_register' missing from root ...
set to 0xBAD4007" -> `SIGSEGV addr=0xbad4007 pc=0xbad4007 lr=libobjc+0x19ec`).

Interim (client side only, done): xnu libsyscall/wrappers/darling_restartable.c returns MIG_BAD_ID without sending a message,
because darlingserver's duct-tape implements the routines as `dtape_stub_unsafe()`
(darlingserver/duct-tape/src/task.c: task_restartable_ranges_register / _synchronize).

Needed from the owner (NOT implemented here): real kern/restartable semantics in darlingserver (register ranges per task,
and on preempt/signal of a thread whose PC is in a range, restart it at the range's recovery offset; `synchronize`
must wait for threads currently inside ranges), then drop the client stub so the MIG request goes to the server.
Without it objc msgSend cache fills run un-restartable: safe only while the cache-flush race is tolerated.

# ESC-D5-darlingserver (2): TASK_AUDIT_TOKEN pid version (val[7]) is 0

Verbatim failure (genuine 27 echo on the 27 cache via dyl4, after the hang/arm64e-dispatch fixes below), MLDR_DSEXEC_LOG:

  ds-exec: guest brk #0x1 pc=0x18054ce70 (/usr/lib/system/libxpc.dylib+0x2e70) lr=0x18054ce34 (/usr/lib/system/libxpc.dylib+0x2e34) fp=0xfffff2130 x0=0x12 x1=0x0
  ds-exec:   x2=0x0 x8=0x0 x16=0x14 x17=0x1e7ff3990 x19=0x8478 x20=0x12 ...

libxpc `__fetch_self_token` (called from `__xpc_get_self_audit_token` <- `__libxpc_initializer`, i.e. every process that loads libSystem):
`task_info(mach_task_self(), TASK_AUDIT_TOKEN=15, &token, &8)`, then requires `token.val[5] == getpid()` (holds: 0x12 == 0x12) and
`token.val[7] != 0` (the pid version), else `brk #1`.

Cause: darlingserver duct-tape/src/task.c sets only `audit_token.val[5] = saved_pid` (task.c:118); val[7] stays 0
(from KERNEL_AUDIT_TOKEN / the parent's token). Needed (NOT done here, owner rule): give every task a non-zero pid version in
audit_token.val[7] (real XNU: proc_pidversion, starts at 1, bumped on exec) and keep it consistent for `audit_token_to_pidversion`,
`proc_pidversion`/kpersona callers. There is no client-side place to patch: task_info reaches the server as a MIG request.
This stops the D5 line (echo / plutil / AppKit probe all pass through libxpc's initializer).

Fixed this round and NOT darlingserver related: (a) hang in `___ulock_wait2`: genuine libmalloc xzone reads the CPU-cluster count
from commpage +0x02F (`_COMM_PAGE_CPU_CLUSTERS`, mldr commpage.c now sets 1; 0 gave an empty segment-group array whose
element 0 overlapped the metapool); (b) `mrs x, TPIDR_EL0` (Darwin CPU/cluster hint, used by xzone) now lowered to 0 by ds-exec;
(c) libdispatch `_dispatch_Block_invoke` re-signs the block invoke pointer on arm64e.
