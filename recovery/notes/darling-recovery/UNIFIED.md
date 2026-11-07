# UNIFIED (D6): owner x86 port/macos26 + daysetta/arm64 -> branch `daysetta/unified` (local only, not pushed)
Source of x86 refs: ~/daysetta/recovery/x86 bundles (read-only), fetched as refs/remotes/x86/port-macos26 (dyld-1378: x86/master). Equivalent to the ssh heads in MANIFEST.txt.

| repo | result |
|---|---|
| superproject | real merge (base 3447ff5ab). Conflicts: SDK `mach/*.h` MIG headers (local deleted, owner modified) -> kept owner copies; `bsm/audit_session.h` comment -> local; `AvailabilityInternal.h` -> added owner's xros aliases; `mldr/loader.c` -> owner `dylinker_override()`/DARLING_DYLINKER_PATHS, local global DARLING_DYLINKER only when PATHS unset; 13 submodule pointer conflicts -> unified commits. libdispatch-apple tree (owner) comes in. |
| xnu | owner e23775e + local. Both added darling/src/libsystem_kernel independently (add/add x80), so a 3-way with base 3780f211 (shared vendored snapshot) was done. Real conflicts: `ulock_wait.c` -> owner (64-bit/shared ulocks, ns); `ulock_wait.h` -> local decl; `lkm.c` -> owner `darling_libkernel_functions_extended()` + local arm64 TSD registration under `#if __aarch64__`. Fixed after merge: duplicate `[544]` syscall table entry; `__NR_open/__NR_access` (absent on aarch64) -> openat/faccessat; `struct stat64` alias on arm64; cpu_in_cksum.S needs LIBSYSCALL_INTERFACE; `darling_kqueue_signal_note` weak no-op default (static dyld link). Added aarch64 PC fixup in owner's restartable.c. |
| darlingserver | clean merge (owner 8 commits + upstream arm64 40275fb). Owner's audit-session commit fixes TASK_AUDIT_TOKEN pid version: libxpc `brk #1` gone. |
| dyld-1378 (dyl4) | roots unrelated; 3-way on shared tree 63bf5ba. One conflict, DyldProcessConfig.cpp kernel-layer path list: union (owner adds libsystem_info/libsystem_configuration; tested echo without them: same result). |
| bootstrap_cmds | conflict CMakeLists -> local (DARLING_HOST_MACOS-guarded superset). |
| foundation | conflict NSUserDefaults.m setPersistentDomain -> owner's suite impl (local CFPreferences variant dropped). |
| IOKitUser, iokitd, libc, libmalloc, libsystem, libxpc, libkqueue, network_cmds, syslog | clean merges. configd, OpenDirectory, Libinfo: fast-forward to owner. |
| cocotron, corefoundation, file_cmds, libtrace, metal | x86 side = older upstream, unrelated history, no owner changes: local kept. |
| libressl-*, others | identical / no x86 bundle. |

Dropped: D5 interim `darling_restartable.c` MIG_BAD_ID stub (removed from libsyscall CMake; file left on disk; owner's real restartable.c used). No audit-token workaround existed in the tree.
Not applied: owner's uncommitted xnu diff (4 files: WQ_FLAG_THREAD_REUSE/wq slot rework plus `PROBE` sigexc code, WIP) and OpenDirectory diff (logging to /tmp, libinfo mach service): debug/WIP, not part of this work.
Notes: uname now reports Darwin 26.2.0 (owner EMULATED_RELEASE). MetalKit fails to link (NSView ivars, pre-existing since D2); install used the previously installed copy as placeholder.
Backup of prior install: ~/darling-recovery/usr-local-darling-D5.tgz.

## Provenance of Apple images (owner rule: macOS 27 only)
Cache: /home/ubuntu/macos-re/cache-arm64e (from UniversalMac_27.0.1_26A434 IPSW). Binaries: macos-re/store-27. No 26.6 image (basesystem, caches, apps, overlay_basesystem) was run, copied or put on the load path.
The owner's `libdispatch_apple` -> usr/lib/darling/macos26 override (26.6 path) is installed by the build but was moved aside to `macos26.disabled-27rule` on this machine; for 27, libdispatch is Darling's own build (arm64e, ~/dispatch-a64e), not an Apple 26.6 image. Flagged for retargeting: dyld `DARLING_MACOS_OVERRIDE_ROOT=/usr/lib/darling/macos26`, libdispatch-apple install destination, owner scripts dl4.sh/runapp.sh (BaseSystem 26.6 env).
