# xnu arm64 port: upstream darling-xnu arm64-support (7b824e5c9) onto owner's xnu-12377 (3780f211)
Branch: src/external/xnu `daysetta/arm64` (built in worktree /home/ubuntu/xnu-port-wt; upstream-as-is baseline = `daysetta/arm64-oldxnu`).
Method: `git cherry-pick -x` of the 16 commits e7fca411..7b824e5c9 (histories unrelated: owner = Apple xnu-12377 + vendored libsystem_kernel; upstream = old xnu). Applied = cherry-pick clean; adapted = hand-resolved.
Result column = new hash on daysetta/arm64. COMPILE/RUN status: see end (not yet verified when this file was written unless noted).

| upstream | subject | status | new |
|---|---|---|---|
| c7b26b13 | Add ARM64 build support | ADAPTED: conflict libkern/libkern/arm/OSByteOrder.h (upstream deletes/rewrites the old header; 12377 has newer Apple header). Kept 12377 version (ours); rest applied | c57f286d |
| 015b75cf | arm64: BSD syscall + ABI + build fixes | ADAPTED: conflict libsyscall/os/tsd.h; merged: DARLING+arm64 TSD via sys_thread_get_tsd_base(), else-branch keeps 12377's `const` attribute (upstream had `pure`); PTR_MUNGE zero-token hunk kept | 99b9a09e |
| e6873a1c | sys_pipe x1 | applied | b6c08fb0 |
| 0962fbdc | sys_pipe stdint/types | applied | 723f8116 |
| 8db016be | epoll_event not packed on non-x86_64 | applied | 686ae9a4 |
| 4cc5ec43 | epoll.h syntax | applied | 3ed48a89 |
| 6d5f8e30 | mach_traps low-VA mmap MAP_FIXED_NOREPLACE+CAS | applied (clean once c7b26b13/015b75cf were in; owner's mach_traps.c +92 lines vs old upstream auto-merged) | 127e2766 |
| d51762bc | fail fixed mach_vm_map over existing | applied | 08af3a72 |
| 1cf93ad1 | low-VA arena cursor on slot collisions | applied | 0373fb0f |
| 6d118fec | mman: constrain arm64 to darwin range | applied | 1ff642f7 |
| 00356543 | tls cross-thread TSD fix | applied | b8906546 |
| f5f3d984 | tls hash by high bits | applied | 564832b4 |
| a2fbfe51 | register main thread TSD base | applied | 820ad942 |
| 82c81e5b | tls: don't export sys_thread_has_tsd_base | applied | 2850931d |
| 0e856745 | hw.machine arm64 | applied | 333ff075 |
| 7b824e5c | termios speeds in c_cflag | applied | 0002abf5 |
Note: first attempt cherry-picked the later commits before c7b26b13/015b75cf and they conflicted (tls.c, mach_traps.c) - they depend on those two; applying in upstream order resolved all. Final order on branch differs slightly from upstream (pipe/epoll/sysctl/termios first); touched files are disjoint.
Skipped: none. Risk: textual merge only; "applies" != "correct on 12377" (12377 libsyscall/gen/bsdsyscalls changed, e.g. libsyscall/custom/SYS.h, ___fork.S, tsd.h). Needs compile + D2 runtime check.

## Status after D2 (2026-11-06)
All 16 commits are on src/external/xnu `daysetta/arm64`. Extra 12377 adaptation commits on top: a0a8a9cc (_structs.h), libsyscall CMake (mach_vm_internal LIBSYSCALL_INTERFACE, os/x18.c), arm OSByteOrder.h, __vfork.S/___vfork.S (see D2-FIXES.md #5-#8).
Compiles (full cli build, 0 failures) and RUNS: `darling shell uname -a / /bin/echo hello / bash -c 'echo $((6*7))'` OK (D2-run.txt), launchd+shellspawn start, so the cherry-picked BSD syscall ABI, low-VA mmap arena, TSD-without-TPIDRRO_EL0 and hw.machine work at this level. Not separately tested: pipe x1, epoll packing, termios speeds, mman range test (darling/tests/mmap-arm64-range.c not run).
