# D8 handoff (2026-11-07)
Done (commits): kevent workq+workloop emulation (xnu a6da292f; KEVENT_WORKQ.md), cross-ABI rule (daysetta 8d010a2: unsigned ptr into plain-arm64 range passes branch-auth; shim strips PAC at jump sites),
race-free futex semaphore, mach trap 70 stub (KERN_FAILURE), mldr _exit; perf: user-space trampolines + inline PAC/LSE (daysetta a8ab887, 9091207, later "opt-in lazy slide"), parallel cache slide, TSD mrs stub,
cfprefsd 27 extracted into store-27 + launchd job plist in ~/.darling/Library/LaunchDaemons (root-owned).
Numbers: see D8-run.txt and ~/daysetta/artifacts/bench/*.txt (echo 5.4->0.87 s, plutil -help 67->1.27 s, objc_msgSend 553->24 ns, dispatch_async 1e6 stall->0.33 us/op).
Stopped at: dyl4 prebuilt loaders. Setting PREBUILTLOADER_VERSION to the 27 cache hash 0x9593B355 makes dyl4 use PrebuiltLoaders but crashes (libsystem_platform brk #0xb001): 27's PBL layout differs from dyld-1378.
Reverted (header back to 0xDA7114E0, uncommitted nothing). Lazy cache slide is opt-in only (DS_EXEC_LAZY_SLIDE=1): guest mprotect of cache data (dyld DATA_CONST) bypasses PROT_NONE faults.
Next 3 steps: (1) Mach vouchers in darlingserver/libsystem_kernel (host_create_mach_voucher trap 70, bank/importance attr, voucher ports) + run 27 cfprefsd as launchd job (DARLING_DYLINKER env) -> plutil -p, AppKit probe.
(2) dyl4 JIT loader cost (~0.5 s of plutil -help): port 27 PrebuiltLoader layout, or cache JIT results; also cold start 16 s (slide I/O): lazy slide needs a guest-mprotect hook.
(3) no_island fallback (a few hundred sites use BRK; island chunk 512 KB would pack the 3 MB gaps better); casp/ldr-auth still generic; fix clock_gettime glitches seen in bench.
Open ESC: ESC-D8-1 (vouchers, cfprefsd). ESC-D7-1 resolved (kevent workq). MetalKit link failure still listed.
Tools: /tmp/inst_mldr.sh, /tmp/inst_sk.sh (rebuild+install mldr / libsystem_kernel, restart container; recreate from D8 notes), ~/daysetta/tools/bench/{build,run}.sh, MLDR_DSEXEC_TIMING=1 prints map time, trap counts.
Env knobs: MLDR_DSEXEC_NO_TRAMP, NO_STRIP, NO_MRS_STUB, DS_TRAMP_NO_INLINE, DS_TRAMP_NO_INLINE_ATOMIC, DS_TRAMP_DEBUG, DS_EXEC_SLIDE_THREADS.
