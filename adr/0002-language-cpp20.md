# ADR-0002 — C++20, GCC 15 / Clang 21, CMake, no external dependencies for M1/M2

| Field | Value |
|---|---|
| Status | Accepted |
| Date | 2026-11-06 |
| Related | `adr/0003-transport-unix-seqpacket.md`, `adr/0004-headless-first.md`, `AGENTS.md` |

## Context

The implementation needs one language and one build system that work on the current
Linux host, keep the trusted server small, and let several agents build the same tree
without a package manager or a network fetch.

| Fact | Class | Evidence |
|---|---|---|
| The host has GCC 15.2.0 | `OBSERVED` | `g++ --version`, 2026-11-06 |
| The host has Clang 21.1.8 | `OBSERVED` | `clang++ --version`, 2026-11-06 |
| The host has CMake 4.2.3 | `OBSERVED` | `cmake --version`, 2026-11-06 |
| The host has no Rust toolchain: `cargo` and `rustc` are absent | `OBSERVED` | `command -v cargo`/`rustc`, both exit 1, 2026-11-06 |
| The build already selects C++20 and rejects extensions | `IMPLEMENTED` | `CMakeLists.txt:4-6` |
| Warnings are errors for the core library and every executable | `IMPLEMENTED` | `CMakeLists.txt:24,30`, `CMakeLists.txt` test target |
| The core is a static library; processes are small executables | `IMPLEMENTED` | `CMakeLists.txt:22-36` |
| M1/M2 may not add external libraries | `PLANNED` | `AGENTS.md:14-15` |

The relevant constraint is not language preference. A Rust implementation would need a
toolchain that is not installed and would add a second build path for fixtures, the
catalogue reader and the tests. C++20 is available on both compilers, and the existing
CMake tree already enforces the warning policy.

## Decision

1. **Language.** Use C++20, compiled with `-std=c++20`, no compiler extensions.
2. **Compilers.** Support GCC 15 and Clang 21. The host default is GCC 15.2.0; Clang
   21.1.8 is the second compiler for warning and portability checks.
3. **Build system.** Use CMake 3.20+ only. No Meson, Bazel, Cargo or handwritten
   recursive make.
4. **Dependencies.** No external libraries in M1/M2. The standard library, POSIX and
   Linux syscalls are the allowed surface. `pthread` is linked explicitly.
5. **Structure.** Build one static library (`daysetta_core`) and small executables
   (`daysetta-broker`, `daysetta-server`, `daysetta-session`, `daysetta-ctl`) plus one
   test executable. Shared code belongs in the library; process entry points stay thin.
6. **Warning policy.** `-Wall -Wextra -Wpedantic -Werror` is mandatory. A warning is a
   build failure, not a review comment.

## Consequences

* New dependencies require a new ADR and a licence review; "it is header-only" is not an
  exemption.
* The test runner stays dependency-free (`tests/test.hpp`), so a clean checkout builds
  with the compiler and CMake alone.
* C++20 features are allowed where they remove unsafe code (`std::span`,
  `std::bit_cast`, designated initialisers, concepts). Code that needs C++23 is
  rejected.
* The macOS 27 target does not change the language decision. A future Mach-O loader and
  ObjC runtime are C++20 components, not a second language runtime.
* Cross-compiling for arm64e is not part of M1/M2 and is not claimed. The host is
  aarch64 Linux; the target substrate is a separate, later work package.

## Rejected alternatives

| Alternative | Reason rejected |
|---|---|
| Rust | No toolchain on the host; second build path; no measured benefit for the M1/M2 scope |
| C | Weaker type and lifetime tools for the codec and handle tables; the project already uses C++20 |
| C++23 | Not required; both supported compilers would narrow the build matrix |
| A package-manager dependency | Network is not available in CI; the brief forbids external libraries for M1/M2 |
