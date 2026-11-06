# X86_64_READINESS (X1, 2026-11-06)

Goal: x86_64 Linux as a first-class target next to aarch64. Nothing here ran on a real x86_64 host; evidence is cross-compile, qemu-user and cmake configure on the aarch64 box.

## 1. DaySetta (~/daysetta, commit d46a507)
- `cmake/x86_64-linux-gnu.cmake`, `DS_ARCH=x86_64 bash ci/run.sh` cross-builds into `build-x86_64/` and runs `daysetta_tests` under `qemu-x86_64` (qemu-user; `qemu-user-static` has no candidate on Ubuntu 26.04, use `qemu-user`). Needs `g++-x86-64-linux-gnu qemu-user`.
- ds-exec on x86_64 = decode/lower/emulate only. Soft PAC (siphash), instruction decoder, lowering encoder, LSE/PAC/CPA/PAuth_LR *emulation semantics* (CAS via `__atomic`, CASP via mutex), Mach-O/cache parsing and the Darwin ABI tables all build and are unit-tested. Live trap handling (`ucontext` regs, LL/SC asm, `install_trap_handlers`) is `#if defined(__aarch64__)`; on x86_64 `install_trap_handlers` returns false and `handle_sig*` return false. Why: executing arm64 guest code needs an arm64 CPU; x86_64 cannot run lowered arm64 words, so a trap engine has no meaning there. Running Apple arm64 binaries on x86_64 would need a CPU emulator (out of scope).
- `ds-run`, `ds-exec-probe`: stub `main` (exit 77) on non-aarch64. Tests that execute guest code (`exec_live_lowered_code_traps`, `exec_ldadd_four_thread_contention`, `exec_probe_genuine_apple_functions`, `darwin_r3_echo_integration`) skip on x86_64. `darwin_syscall_dispatch_errno_carry` skips its tail under qemu-user (no `process_vm_writev`).
- `commpage.cpp` HWCAP read only on aarch64 (x86_64: no native arm64 caps).
- `ci/check_docs.py`: `recovery/` excluded (host-loss bundles exceeded the 2 MiB rule; CI was red before this change).
- `.github/workflows/ci.yml`: matrix aarch64 (ubuntu-24.04-arm) + x86_64 (ubuntu-24.04) native, plus x86_64 cross+qemu job on the arm runner.

## 2. Darling arch audit (branch daysetta/arm64, base origin/arm64-support, 13 commits + submodules)
Note: `git log --not --remotes` is empty here (remote refs are stale); real delta = `origin/arm64-support..HEAD` plus each submodule pointer change. xnu x86-era base = `3780f211` (branch port/xnu-12377.121.6 + that commit); audited `3780f211..HEAD`.

Fixed (commits: xnu 6d4c2478, superproject 5aa675fa2):
| Item | Problem on x86_64 | Fix |
|---|---|---|
| xnu `bsd/impl/network/connect.c` | unconditional O_NONBLOCK clear around connect(): breaks non-blocking connect/EINPROGRESS | `#if defined(__aarch64__)` |
| xnu `bsd/impl/network/socket.c` | on EMFILE raises RLIMIT_NOFILE and retries: changes limit semantics | `#if defined(__aarch64__)` |
| xnu `sigexc.c` | `#include <elfcalls.h>` + `extern _elfcalls` outside the aarch64 guard | moved inside guard |
| `CMakeLists.txt` | `uname -m` forced; cannot configure the x86 tree from an aarch64 box | `-D_HOST_ARCH=x86_64` override (default unchanged) |

Checked, already correct (guarded or arch-neutral): `src/CMakeLists.txt` `-mcpu=generic+crc+aes` (under TARGET_ARM64); mldr ds-exec sources and `MLDR_DSEXEC` (aarch64 only, CMake condition; verified absent in x86 configure); `commpage.c` CPU_CLUSTERS + RO commpage (`__aarch64__`); `mldr.c` stack_top (split `#if`); `elfcalls.c` TSD table (`__aarch64__`; non-aarch64 sets dsexec_* = NULL; `tsd_get/tsd_set` stay zero in static storage); libsystem_kernel `tls.c`, `sysctl_machdep.c`, `sysctl_hw.c` (hw.machine), `sigprocmask.c` (synchronous-signal unblock), `pipe.c`, `mman.c` low-VA arena, `__fork/__vfork/__pipe/custom.S`, `SYS.h`, `syscall_sw.h`, `tsd.h`, `bsd/mach/machdep_syscall.S`, `xtrace-hooks.S`, `sig_restorer.S`, `linux-syscall.S`, `sigaction.[ch]` (x86_64/i386 branches kept), `open.h` (O_DIRECTORY etc. split by arch), `epoll.h` (packed only x86_64), libsyscall CMake (`select-pre1050` stays on x86; `x18.c` aarch64 only), libsystem_kernel `-msse*` (gated to x86), libdispatch `_dispatch_Block_invoke` (`__has_feature(ptrauth_calls)`), `mach/machine/vm_types.h` (i386 branch includes mldr's own `mach/i386/vm_types.h`, exists).

Arch-neutral behaviour changes that now also hit x86_64 (NOT guarded; each needs the x86 smoke test, see section 4):
1. `mach_traps.c` `_kernelrpc_mach_vm_map_trap`: fixed mapping without VM_FLAGS_OVERWRITE now `MAP_FIXED_NOREPLACE` and returns KERN_NO_SPACE (XNU semantics; was silent overwrite). Needs kernel >= 4.17 (fine).
2. `abort_with_payload.c`: now `exit_group(128+SIGABRT)` after the SIGABRT (was return 0). `terminate_with_payload` new.
3. `lkm.c mach_driver_init`: elfcalls taken from `elf_calls=` apple[] string first; the old `dyld_func_lookup("__dyld_get_elfcalls")` only when `_libkernel_functions->version < 5`. x86 Darling dyld must keep reporting version < 5 (or provide `elf_calls=`; mldr always does).
4. `pselect.c` timespec fix (strictly a bug fix); `ulock_wait.c` split into `_ns` helper + `ulock_wait2` (544); syscalls 489/520/544 added to the table.
5. `libsyscall/CMakeLists.txt` `-DLIBSYSCALL_INTERFACE=1` around MIG generation (xnu-12377 mach_vm clash); `darling_restartable.c` defines `task_restartable_ranges_*` stubs returning MIG_BAD_ID; check no duplicate symbol on x86 (would show at link of libsystem_kernel).
6. `stack.c` apple[] strings ptr_munge/stack_guard/malloc_entropy (all arches; 7 slots).
7. `loader.c` `DARLING_DYLINKER` env override (all arches; inert unless set).
8. SDK header link changes: 18 stale checked-in MIG headers dropped, `i386/hw_lock_types.h`, `i386/io_map_entries.h` links removed (dangling after xnu-12377), `machine/*` and `i386/_endian.h`, `i386/string.h` added. x86 build is expected to regenerate MIG headers from the xnu-12377 defs.
9. Submodule pointers for libc, libmalloc, syslog, IOKitUser, iokitd, network_cmds, bootstrap_cmds, foundation, AvailabilityVersions are `recovered: darling-port-patches/...` merges = the owner's x86-era xnu-12377 adaptation; arch-neutral.

Not changed here, relevant for the x86 tree: `src/external/libdispatch-apple`, `xpc_pipe_v5.c`, `xpc_domain_v5.c`, `restartable.c`, getattrlistbulk, `__mac_syscall` ENOSYS, etc. are MISSING on daysetta/arm64 (see LOST_FIXES.md items 1-9, 21-24, 27, 30). The canonical x86 tree had them on `port/macos26`; after restore, prefer: check out `port/macos26` (superproject + xnu `port/xnu-12377.121.6`), then merge `daysetta/arm64` and resolve; the guards above make the merge arch-safe.

Evidence (aarch64 box): `-fsyntax-only` of every touched libsystem_kernel/libsyscall object retargeted to `x86_64-apple-darwin20` (21 emulation objects + 3 libsyscall objects, rc=0) and the same 21 for aarch64 (rc=0); 6 of the arch-sensitive files also for `i386-apple-darwin20` (rc=0); mldr `mldr.c`, `commpage.c`, `elfcalls.c` compiled with `--target=x86_64-linux-gnu` (rc=0; object is `ELF x86-64`). `cmake` configure succeeded for the x86_64 target (see recipe, "cross configure check").

## 3. Build recipe for the x86_64 host
```
# toolchain (Ubuntu 24.04): clang (>=19), cmake, ninja, bison, flex, libfuse-dev, libudev-dev, libcap2-bin, pkg-config, libssl-dev, ... (Darling's list: cat debian/control Build-Depends)
cd ~/darling && git checkout daysetta/arm64   # or port/macos26 + merge, see section 2
git submodule update --init --recursive      # xnu on daysetta/arm64 or port/xnu-12377.121.6
mkdir -p build && cd build
cmake -G Ninja -DCOMPONENTS=cli,cli_dev_gui_stubs -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_POLICY_VERSION_MINIMUM=3.5 ..
# defaults on x86_64: TARGET_x86_64=ON TARGET_i386=ON TARGET_ARM64=OFF. Add -DTARGET_i386=OFF to shrink (D1 baseline did).
ninja -j8 && sudo ninja install        # /usr/local. DO NOT build on a box other people use.
```
Notes: `-mcpu`, ds-exec and DSEXEC_SRC are inert on x86_64 (CMake guards on `CMAKE_SYSTEM_PROCESSOR`, derived from `uname -m`). Use `timeout`/background for long builds (handoff trap). After building libsystem_kernel: `darling shutdown` before replacing it; replace running mldr/darlingserver via `cp .new; mv -f` (handoff section 6). Run Terminal.app milestone via the owner's scripts (`dl4.sh`, `kinstall.sh`, `runapp.sh`), restored with `~/darling-port-tools`.

Cross configure check (what was run on aarch64, not a build): 
```
export QEMU_LD_PREFIX=/usr/x86_64-linux-gnu    # configure runs x86 test binaries through binfmt qemu
cmake -G Ninja -D_HOST_ARCH=x86_64 -DCOMPONENTS=system -DCMAKE_BUILD_TYPE=Release -DCMAKE_SYSTEM_NAME=Linux \
  -DTARGET_i386=OFF -DCMAKE_C_COMPILER=<clang --target=x86_64-linux-gnu wrapper> -DCMAKE_CXX_COMPILER=<clang++ wrapper> \
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5 ..      # dir: ~/darling/build-x86_64-check2 (build-x86_64-check holds a stale aarch64 CMakeSystem, ignore)
```
Result: configure OK; `TARGET_x86_64=ON`, triple `x86_64-apple-darwin20`, no `MLDR_DSEXEC`, no `-mcpu`.

## 4. Risks and untested
- No x86_64 Darling object was linked or run. Syntax-only compiles cannot catch link-time duplicates (`task_restartable_ranges_*`, `kpersona_pidinfo_audit_token`, `sys_ulock_wait2`) or runtime regressions.
- First things to run on the restored host: full build; `darling shell`, `uname`, `defaults`; the 26.6 Terminal.app milestone; a non-blocking `connect()` test (guard restored); `mach_vm_map` fixed-address user (libmalloc nano zone) after change 1; `abort()` path after change 2; `mach_msg2_trap` 13/13 test (`darling-port-patches/tests`).
- aarch64-only `sigprocmask` signal unblocking and `sys_connect` hack are intentionally not applied on x86_64; if x86 shows the same symptoms, remove the guard per item.
- The cross-configure used `-D_HOST_ARCH=x86_64` on an aarch64 box; a real x86 host does not need it.
- `i386` slices were only syntax-checked for 6 files; mldr `stack.c` emits pre-existing 32-bit pointer-cast warnings when TARGET_i386 builds.
- DaySetta x86_64 tests run under qemu-user, so threading/atomics semantics are emulated, not hardware-tested; `process_vm_writev` is absent under qemu-user (one test tail skipped).
- DaySetta x86_64 cannot execute Apple arm64 code (by design, no CPU emulator).
