# Upstream Darling arm64/aarch64 work (queried 2026-11-06, read-only; nothing merged)
Source of every row: `git ls-remote --heads` + GitHub REST `compare`/`pulls` API (unauthenticated), run 2026-11-06.

## Key finding
Upstream opened **PR #1818 "ARM64 (aarch64) Linux host support"** the same day (branch `darlinghq/darling:arm64-support`, 28 commits, 178 files, +67 behind master).
It supersedes **PR #1753** (kkHAIKE, 2026-05-20, branch kkHAIKE:arm64-support, 9 commits). Both open. https://github.com/darlinghq/darling/pull/1818 , https://github.com/darlinghq/darling/pull/1753
PR text claims (author claims, NOT verified by us): full ~26k-step ninja build on Debian 13 / clang 19.1.7 in privileged Docker on a 48-bit-VA 4K-page aarch64 kernel;
`darling shell` runs native arm64 Mach-O: bash, coreutils, pipes/job control, launchctl, Perl 5.18, Ruby 2.6, Python 2.7. x86_64 NOT re-tested.
#1753 text claims arm64e PAC-signed macOS-26 CLT binaries "load and run" (unverified; host there is not stated to lack PAC).

## Superproject branches (github.com/darlinghq/darling)
| Branch | vs master | Covers |
|---|---|---|
| `arm64-support` (PR #1818) | ahead 28, behind 67, 178 files (src 132, Developer SDK 39, cmake 2, tools 2) | `TARGET_ARM64` + aarch64 host detect (bd9472b77), SDK arm/arm64 header symlinks (60f57a976), libm ARM64 from openlibm (c0f5ebf95, ca075ab6e), mldr arm64 startup incl. low-VA (<2^47) initial mmap (a52f9f2cd), framework/tool build adj (e23ecfd91), clang16+ strictness relax (375078096), shellspawn/darling.c fixes by fervi (poll loop instead of kqueue, 60 s timeout, sid==0), mldr binfmt path, FSEvents fanotify.h, submodule pointers -> each submodule's `arm64-support` (223ced6de, a44ce5a56, c64a0d7a1: TSD races, hw.machine=arm64, persistent NSUserDefaults), libpthread `chkstk_darwin` without TPIDRRO_EL0 (bcd2af07a), ttyname/termios speeds (235413b55), shutdown-user.sh tooling |
| `feature/arm-support` | ahead 30, behind 260 (2024-06) | Older CuriousTommy attempt (PR #1450, open): build `core` + hello-world on ARM64; SDK arm headers, signal/asm work |
| `feature/arm-support_first_attempt` | ahead 23, behind 1379 (2020) | pre-darlingserver LKM-era ARM64 signal support; obsolete layout (src/kernel/emulation/linux) |
| `feature/arm-support_second_attempt` | ahead 12, behind 817 (2021) | `__darling_bsd_syscall` branch instead of svc; SDK arm headers; obsolete |

## Submodule repos with arm64 branches
- `darlinghq/darlingserver`: `arm64-support` ahead 4 / behind 8 (66b46776e, 5baf90543 ARM64 build support; fc5e5fb45 detach launchd session; 40275fb61 duct-tape priority_queue 64-bit pointer packing + timer_call runaway guard; touches duct-tape/{host,locks,memory,thread,timer}.c, `rtclock_arm64.c`, xnu/osfmk/arm/*). Also `feature/arm-support` (2024, 14 ahead). PR darlingserver#23.
- `darlinghq/darling-xnu`: `arm64-support` ahead 16 / behind 17 (c7b26b13e, 015b75cf7 BSD syscall+ABI; sys_pipe x1; epoll_event packing; low-VA mmap arena with MAP_FIXED_NOREPLACE + CAS retry (6d5f8e304, d51762bcf, 1cf93ad1e, 6d118fec8); TSD base on arm64 without TPIDRRO_EL0 (003565439, f5f3d9840, a2fbfe51c, 82c81e5b1); hw.machine arm64 (0e856745a); termios speeds). PR darling-xnu#29. NOTE: Darling upstream xnu is the OLD xnu, not the owner's xnu-12377 port: conflicts expected with `3780f21`.
- `arm64-support` and/or `feature/arm-support` exist for: darling-dyld, darling-libpthread, darling-libffi (arm64-support only), darling-objc4, darling-corefoundation, darling-libkqueue, darling-libxpc, darling-Libc (checked via ls-remote). PR #1818 lists 31 submodule PRs: IOKitUser#8, bash#6, bootstrap_cmds#10, bzip2#5, cctools-port#8, cfnetwork#5, cocotron#75, corefoundation#14, darlingserver#23, dbuskit#2, dyld#8, expat#3, foundation#34, Libc#6, libffi#2, libkqueue#11, liblzma#3, libmalloc#4, libplatform#6, libpthread#4, Libsystem#8, libunwind#5, libxpc#11, objc4#5, openssl#5, perl#6, ruby#5, security#8, sqlite#2, xnu#29, zlib#3.
- Older per-repo ARM PRs referenced by #1450 (2023): darlingserver#12, bootstrap_cmds#5, corefoundation#8, dyld#3, Libc#4, libkqueue#5, libplatform#3, libpthread#2, libxpc#9, libunwind#3, objc4#1, xnu#7, zlib#1.

## Other related issues/PRs
#642 Getting Darling to build on an ARM64 device (open), #1429 fails under Rosetta in Linux ARM64 VM "Failed to allocate stack" (open), #1520 aarch64 make error (closed), #1576 ARM emulator (closed), #252 PowerPC/ARM emulation idea (open).
Other branches of possible interest on master repo: `syscall_rework`, `alternative-mldr-exec`, `less-undefined`, `vchroot`, `launchd-session`.

## What it does NOT cover (from PR text / our audit; gaps for D2+)
- Targets macOS-era (Darwin 20.x) userland and arm64 binaries from "CLT"; NOT xnu-12377 / macOS 26-27 Apple shared cache + dyld-1378 flow.
- No evidence of arm64e/PAC handling on a PAC-less ARMv8.0 host (no PAC/LSE/LRCPC): unknown, to be tested.
- Uses `TPIDRRO_EL0` workaround (TSD hashing) — Darwin arm64 binaries read TPIDRRO_EL0 directly; the upstream approach is a shim, worth reviewing for arm64e user code.
- x86_64 not re-tested by the PR author.
