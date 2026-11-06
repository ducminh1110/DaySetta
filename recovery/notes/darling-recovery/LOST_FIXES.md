# LOST_FIXES (D1, 2026-11-06)
H = /home/ubuntu/DARLING-MACOS26-HANDOFF.md (line numbers). Presence = checked by grep/find in /home/ubuntu/darling (branch daysetta/recovered) and /home/ubuntu/dyld-1378 on 2026-11-06.
"Layout note": modern Darling keeps libsystem_kernel emulation inside xnu: `src/external/xnu/darling/src/libsystem_kernel/emulation/{src,include}/...` (E below). The owner's xnu commit 3780f21 (Nov 4) is already in the tree and contains the xnu-12377 port (patch src_external_xnu.patch is a strict subset: same 1117 files).
Behaviour text below is paraphrased from the cited line; nothing here is re-implemented.

## A. Fixes (status / evidence / source doc:line)
| # | Fix | Status | Evidence in tree | Behaviour to re-implement (source) |
|---|---|---|---|---|
| 1 | `src/launchd/src/xpc_pipe_v5.c` (XPC pipe v5: lookup 207/804, register 204, parent 203, session type 301; reply must carry client's `req_pid`/`rec_execcnt`) | MISSING | `find` no file; only copy `/home/ubuntu/darling-port-tools/xpc_pipe_v5.c.copy` (14299 B, Oct 30 01:05, pre-routine-718) | H:143 |
| 1b | routine 718 (xpc_main: reply `identifier` + `xpcservice-dict`) added to xpc_pipe_v5.c | MISSING | not in .copy (dated before) | H:153 |
| 2 | `src/launchd/src/xpc_domain_v5.c` (PID-domain / bundle XPC services launched by launchd) | MISSING | `find` no file | H:213-214 |
| 3 | `libdispatch_apple` target + `src/external/libdispatch-apple` (libdispatch-1542.100.32, x86_64 only; `DISPATCH_USE_KEVENT_WORKQUEUE=0`, `KEVENT_WORKLOOP=0`, `VOUCHER_USE_EMPTY_MACH_BASE_VOUCHER=1`, mgr queue on own pthread) | MISSING | no dir, `grep libdispatch_apple` in CMakeLists = 0; build helper `darling-port-tools/mkdispatch.sh` survives | H:79,107,142,179 |
| 4 | `libkernel_table.h` v4 (Darling `_libkernel_functions`) vs v5 (Apple `apple[]`) detection; elfcalls from `apple[]`; `posix_spawn` uses Apple `vfork` | MISSING | only old `_libkernel_functions_t` in E/src/other/mach/lkm.c:26, E/.../sigexc.c; no libkernel_table.h | H:119 |
| 5 | `restartable.c` in emulation (restartable ranges via signal RTMIN+2) | MISSING | only upstream stubs duct-tape/xnu/osfmk/kern/restartable.c (darlingserver) | H:120 |
| 6 | darlingserver task stubs: restartable -> KERN_NOT_SUPPORTED, exc_guard, dyld cache info; audit token pid version != 0 | MISSING (unverified fully) | darlingserver is upstream HEAD, 0 local mods (assessment/darling.md) | H:123-124 |
| 7 | `necp.c` (`impl/network/necp.c`, NECP syscalls) | MISSING in emulation | `find necp*` only xnu `bsd/net/necp.c` (kernel src) and SDK/duct-tape headers; no emulation file | H:215 |
| 8 | `attrlist_pack.h` + `getattrlist`/`getattrlistbulk` rewrite (real getattrlistbulk; NSFileManager dir listing; `ls` fts_read) | PARTIAL: drafts survive, not merged | `/home/ubuntu/darling-port-patches/work/{getattrlistbulk.c,getattrlist_generic.c,attrlist_pack.h}` (Oct 31); tree has only upstream getattrlistbulk.c stub (E/CMakeLists.txt:322) | H:98,142,154 |
| 9 | fcntl OFD locks (F_OFD_GETLK/SETLK/SETLKW 90-93), `F_SETCONFINED`, `F_GETPROTECTIONCLASS` | MISSING | `grep F_OFD\|CONFINED` in E: none | H:153 |
| 10 | `F_ADDFILESIGS_RETURN` (CodeDirectory code limit) | PRESENT | E/src/xnu_syscall/bsd/impl/fcntl/fcntl.c:138 | H:117 |
| 11 | `ulock_wait2` | MISSING | `grep ulock_wait2` = 0 (only ulock_wait.h) | H:142 |
| 12 | `mk_timer_arm_leeway` in emulation | MISSING in emulation (kernel trap table only) | xnu osfmk/mach/syscall_sw.h:193/195 is stock xnu; no emulation impl | H:156 |
| 13 | `mach_msg2_trap` | PRESENT | E/.../mach/mach_syscall_table.c, mach_traps.h; test `darling-port-patches/tests/mach_msg2_test.c` ("13/13" = doc claim) | H:89 |
| 14 | `csops` entitlements read from code signature (app-sandbox dropped) | MISSING | E/.../misc/csops.c exists = upstream version (not inspected for entitlements; no evidence either way) | H:216 |
| 15 | overlay + whiteouts (`overlay_basesystem.py`: symlink BaseSystem resources into ~/.darling; hide Darling stub frameworks; WebKit/AVFoundation stubs hidden; skip dangling symlinks; `/var -> private/var`) | PRESENT (script) / prefix state LOST | `/home/ubuntu/darling-port-tools/overlay_basesystem.py` (5432 B Oct 30; later Nov-3 fix "skip dangling symlink" H:271 may not be in it) | H:145,220,271 |
| 16 | configd `dnsinfo.c` resolv.conf parser enabled, defaults port=53 timeout=5 search_order (commit `1de5c00`) | MISSING | `src/external/configd/darling/src/dnsinfo/dnsinfo.c:28,149` still `#if 0`; configd HEAD 98e52de; `1de5c00` not present | H:228-230 |
| 17 | dyld-1378 `4b10c6c`: add `/usr/lib/system/libsystem_configuration.dylib` to `kernelLayerPaths` | MISSING | `dyld-1378/dyld/DyldProcessConfig.cpp:1821-1824` lists only libsystem_kernel + libdispatch; git has 1 commit (fd8d0c4) | H:231 |
| 18 | dyld-1378 `041388c`: `hasAppKit \|\| hasSwiftUI` so bridge loads into SwiftUI Dock | MISSING | no commit; bridge hook `DARLING_GUI_BRIDGE` not found | H:267 |
| 19 | dyld-1378 `7b4b661` | UNKNOWN | hash named in task brief; NO doc in /home/ubuntu cites it (grep); not in object db | - |
| 20 | dyld-1378 base `#ifdef DARLING` changes (mach_init+sigexc_setup, isOverridablePath, /usr/lib/darling/macos26 override dir, simulatorFileMatchesDyldCache removed, pageInLinkingMode 0, cache-path fallback, glue.c stdio/Blocks, DARLING_DYLD_TRACE) | PARTIAL: uncommitted working changes | `dyld-1378` 5 M files + `darling-compat/` untracked; 22 `DARLING` hits in dyld/*.cpp | H:126-135 |
| 21 | libkqueue merged into libsystem_kernel (works with Apple libSystem); weak `kqueue_impl` hook | MISSING | E/CMakeLists.txt:66 for-libkqueue.c (upstream); `$<TARGET_OBJECTS:kqueue>` commented at libsystem_kernel/CMakeLists.txt:43 | H:96,142 |
| 22 | libkqueue timer `EV_ADD\|EV_ENABLE` re-arm bug (Darling commit `0aabeafd6`, ~26 s late dispatch timers) | MISSING | libkqueue HEAD b0795a2; commit absent | H:152 |
| 23 | `EMULATED_RELEASE` 26.2.0 (uname) | MISSING | E/CMakeLists.txt:11 still "20.6.0"; :12 "Darwin Kernel Version 20.6.0" | H:100,142 |
| 24 | `__mac_syscall` (381) returns ENOSYS silently; AMFI/Sandbox/Quarantine MAC answers; Sandbox check -> allow | MISSING | `grep mac_syscall` in E = 0 | H:99,142,154,216 |
| 25 | `macos26_exports.c` (kernelVersion, netname, `register_uexc_handler`), `work_interval_instance_*` getters, `voucher_mach_msg_clear`, skywalk/exclaves/vm_reclaim/notify.defs/os_log_coprocessor | PRESENT (inside xnu 3780f21) except macos26_exports.c MISSING | libsyscall/wrappers/{exclaves,work_interval}.c, _libkernel_init.h ; `grep kernelVersion` none | H:113-116 |
| 26 | mldr `apple[]` ptr_munge/stack_guard/malloc_entropy | PRESENT (uncommitted before D1, now committed) | src/startup/mldr/stack.c:74,154 | H:125 |
| 27 | mldr `DARLING_DYLINKER` / `DARLING_DYLINKER_PATHS` (children use dyl4) | MISSING | grep in src/startup = 0 | H:154 |
| 28 | `bsdthread_register` workqueue feature for Apple libpthread; canonical-rsp fix | PRESENT (3780f21) | E/.../bsdthread/bsdthread_register.c | H:119 (commit msg of 86136988) |
| 29 | `PROC_PIDUNIQIDENTIFIERINFO` correct size | PRESENT (case exists) | E/.../misc/proc_info.c:132 (size correctness unverified) | H:122 |
| 30 | sigexc: fatal-signal report with symbols (dladdr), re-send via `kill(getpid())` not `kill(0)` | MISSING | sigexc.c:141 still `sys_kill(0, SIGTRAP, 0)`, no dladdr | H:123,142 |
| 31 | `mach_generate_activity_id` trap | PRESENT | E/.../mach_syscall_table.c:38, mach_traps.c:855 | H:142 |
| 32 | darlingserver fileport RPC (memory entry memfd+fileport, appended at END of RPC list), shared-remap (AttributeGraph), thread_info no-abort, asid 100000 audit token for all tasks | MISSING | `grep fileport` in darlingserver src/include = 0; asid 100000 not found | H:149,155,215 |
| 33 | `mach_port_is_connection_for_service` (libxpc "Bogus check-in") | MISSING | grep not run beyond libsystem_kernel/darlingserver; treat as missing | H:155 |
| 34 | `mk_timer_arm_leeway`, OOL empty physical-copy, `Aqua.car` from user's Mac (`appearance_overlay.py`), IOSurface via iokitd `IOSurfaceRootApple`, dyld patch of IOKit traps, font short-path lookup in dyld | MISSING (tool `appearance_overlay.py` PRESENT in darling-port-tools) | - | H:156 |
| 35 | libdispatch cooperative-queue refcount, workloop identity + ulock rc | MISSING (with #3) | - | H:154,156,216 |
| 36 | pathconf macOS names, birthtime stat64, workqueue thread-list lock (stand-in os_unfair_lock), 8 MB workqueue stacks | MISSING | grep birthtime in E = 0 | H:154 |
| 37 | WebKit-list: mach_vm_map alignment, MAP_NORESERVE, `/.nofollow/`, dyld cache alias lookup, `change_fdguard_np`, `connectx`, `SO_NREAD`, libkqueue EVFILT_SOCK, proc_pidinfo sizes, chflags no-op, `renameatx_np` | MISSING | greps in E for connectx/SO_NREAD/nofollow/renameatx/fdguard = 0; chflags.c exists = upstream | H:215-217 |
| 38 | `asroot` (Recovery apps as root), `hfs_hardlinks.py`/`hfs_relink.sh` | tools PRESENT (`darling-port-tools/asroot.c`, hfs_*), runtime wiring MISSING | - | H:154 |
| 39 | xnu-12377 port itself, libsyscall additions | PRESENT | submodule xnu @3780f21 (branches port/xnu-12377.121.6, daysetta/recovered) | assessment/darling.md |
| 40 | Superproject `ee43614a`/`599c2f5e` pointers (Libinfo + configd) | MISSING | not in shallow history | H:232 |
Counts (rows): present 7 (#10,13,26,28,29,31,39), partial 3 (#8,20,25), tool-only/runtime-lost 2 (#15,38), unknown 1 (#19), missing 27.

## B. Replacements (Daylight ports used instead of Darling components)
Verified fact first: `/home/ubuntu/daylight/tools/build-darling.sh` (27 lines) does NOT build Darling. It cross-builds the Daylight Mach service + tests with `clang -target x86_64-apple-macos11 -isysroot <Darling SDK>` and Darling's `x86_64-apple-darwin20-ld` from an existing `$DARLING_BUILD` (default ~/darling/build) (build-darling.sh:5-14). So it is not an aarch64 baseline recipe and is hard-wired to x86_64. Darling itself was built with `cd ~/darling/build && cmake . && make -j8 <targets>` (darling-port-tools/mkdispatch.sh:3, kinstall.sh:3) targets `system_kernel system_kernel_static64 libdispatch_apple` + full build installed to /usr/local (H:50). I found no evidence in any doc/script that the build skips frameworks; skipping is a plan below, not a recorded fact. `daylight-stack.sh` only starts/stops systemd units (daylight-server, -ca, -dock, -publisher, -web), no build.
| Component | Darling part it replaces | Lives now | Source |
|---|---|---|---|
| Apple userland from dyld shared cache (AppKit, SkyLight, QuartzCore, CF, libxpc, objc, Swift...) | Darling's cocotron Cocoa/CoreFoundation/Foundation/AppKit, libobjc, libxpc... | cache in `~/macos-re/cache-arm64e` (macOS 27 arm64e, 6.6 GB; x86 BaseSystem 26.6 cache NOT on disk, assessment/darling.md s3) | H:9-12,30 |
| dyld-1378 port "dyl4" (+isOverridablePath / kernelLayerPaths: only libsystem_kernel + libdispatch overridable) | Darling's dyld (src/external/dyld) | `/home/ubuntu/dyld-1378` (uncommitted edits, DyldProcessConfig.cpp:1821-1850); darling-compat/ | H:12,105,126-135 |
| libsystem_kernel (kept) | - | Darling emulation in xnu/darling/src/libsystem_kernel | H:12 |
| libdispatch-1542.100.32 (`libdispatch_apple`) | Darling libdispatch (older) | LOST (see A#3) | H:79 |
| darling-configd fix (resolv.conf) | configd dnsinfo `#if 0` | LOST (A#16) | H:228-231 |
| overlay_basesystem.py whiteouts/links | Darling stub frameworks (WebKit, AVFoundation, launchservicesd stub kept) | `/home/ubuntu/darling-port-tools/overlay_basesystem.py` | H:145,220 ; script docstring l.1-12 |
| Daylight services: dnssd stand-in (DNS-SD/mDNSResponder) | Darling has none | `/home/ubuntu/daylight/services/dnssd/dnssd-standin.c` | H:219,236 |
| mountnotify stand-in | - | `/home/ubuntu/daylight/services/mountnotify/mountnotify-standin.c` | file listing (no doc line) |
| rootfs (apfs-util stand-in, Info.plist) | - | `/home/ubuntu/daylight/services/rootfs/` | file listing |
| metal (metal2vulkan runtime on Vulkan, Rust) | no Metal in Darling | `/home/ubuntu/daylight/services/metal/` (Cargo.toml rev 0dfdbcd5..); needs network crates | Cargo.toml:1-9 ; RECOVERY doc l.54 |
| Daylight WindowServer replacement (server/, session handshake, CA render server `ca/daylight-ca.m`, `bridge/daylight-bridge.m`) | WindowServer/SkyLight server side | `/home/ubuntu/daylight/{server,ca,bridge}` (git, branch dev) | H:62-66,156-157 |
| Apple BaseSystem daemons (launchd plists excepted): notifyd, cfprefsd, fontd, runningboardd, trustd, authd, launchservicesd via `mkdaemons.py` | Darling's own daemons/stubs | `/home/ubuntu/darling-port-tools/mkdaemons.py` (prefix jobs LOST) | H:38,154,218 |

### What can be skipped in an aarch64 build (PLAN, not verified)
Per H:12,30 and the table above the hybrid runs Apple's own cache and keeps only Darling's libsystem_kernel, mldr, darlingserver, launchd (+ shellspawn/darling CLI), plus libkqueue/libdispatch as targets. Candidates to skip (set cmake targets explicitly, see BUILD_BASELINE.md) to save build time/disk: cocotron/AppKit/Foundation/CoreFoundation/CFNetwork/CoreServices frameworks, perl/ruby/python/bash/openssl/sqlite/zlib/bzip2/expat/liblzma userland, dbuskit, WebCore/JavaScriptCore, cctools-port (but KEEP: it gives ld64 needed to link dyld and Daylight, H:135). Whether the CMake graph allows building only those targets is untested.
