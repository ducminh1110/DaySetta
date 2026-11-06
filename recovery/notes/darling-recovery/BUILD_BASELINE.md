# BUILD_BASELINE (D1, 2026-11-06) - aarch64 host, no porting done
Host: Ubuntu aarch64 (Linux 7.0.0-42), 8 cores, clang 21.1.8, cmake 4.2.3, ninja. Tree: /home/ubuntu/darling @ branch daysetta/recovered (600ae5da), all 149 submodules checked out. Build dir: /home/ubuntu/darling/build-aarch64 (89 MB after failed build; excluded via .git/info/exclude).
Logs: /home/ubuntu/darling-recovery/{cmake-configure.log (1st, failed), cmake-configure2.log (ok), ninja-build.log, apt-install.log}.

## Configure
```
cd /home/ubuntu/darling/build-aarch64
cmake -G Ninja -DCOMPONENTS=system -DCMAKE_BUILD_TYPE=Release -DTARGET_i386=OFF \
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_POLICY_VERSION_MINIMUM=3.5 ..
ninja -k 0 -j8
```
- `COMPONENTS=system` (+ implied `core`) = smallest meaningful set (cmake/darling_parse_components.cmake:3-5); default is `stock` (cli, python, ruby, perl, gui...). 6029 ninja steps for this set. `TARGET_x86_64` left ON (no arm64 target exists in this tree; upstream arm64-support adds `TARGET_ARM64`, see UPSTREAM_ARM64.md). `-DCMAKE_POLICY_VERSION_MINIMUM=3.5` is for cmake 4.x vs old submodule cmake_minimum_required.
- Metal not built: Vulkan/LLVM dev not found (log: "building without Metal support"). Python 2 absent: bytecode compile off.
- The owner's `tools/build-darling.sh` is NOT a Darling build recipe (it only builds Daylight with an existing Darling build, x86_64-hardwired); see LOST_FIXES.md section B. Recipe used here is Darling's own cmake flow (mkdispatch.sh/kinstall.sh pattern: cmake + make targets).

## Configure blocker (hit first, worked around outside the repo history)
1. `cmake/compiler_include.cmake:3` (`#include <cpuid.h>` probe) -> `cpuid.h:14: this header is for x86 only` + `CMake Error ... Cannot detect compiler header include path (src/CMakeLists.txt:72)`.
   Temporary workaround to continue (stdbool.h instead of cpuid.h): saved as /home/ubuntu/darling-recovery/baseline-workaround-compiler_include.patch; REVERTED in the tree (not committed). Re-apply it before re-running this baseline.
   Also top CMakeLists.txt:103-105 only rejects i686/i386; no aarch64 branch, `architecture.cmake` is x86_64/i386 only.

## Build result: configure OK with workaround; ninja stopped: 596 FAILED of 6029 steps (2219 steps run, ~2 min).
Failed steps by component: libc 284, corefoundation 103, libxpc 37, objc4 29, cctools 25, libpthread 21, dyld 21, libmalloc 19, Libinfo 15, bootstrap_cmds 14, commoncrypto 9, libplatform 6.

## Distinct errors by component (first occurrence file:line, count of occurrences)
### Compiler/host headers
- /usr/lib/llvm-21/lib/clang/21/include/cpuid.h:14: this header is for x86 only (x1) [configure probe above; also seen at build]
- cpuid.h:312: invalid output constraint '=a' in asm (x4)  (x86 inline asm pulled in by an x86-only include)
### SDK headers (Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk/usr/include) - affects every target that includes <mach/mach.h> (libc, libpthread, libmalloc, libplatform, cctools, corefoundation, libxpc ...)
- mach/task.h:825: unknown type name 'suid_cred_path_t' (x578)
- mach/task.h:826: unknown type name 'suid_cred_uid_t' (x1156)
- mach/task.h:827: unknown type name 'suid_cred_t' (x578)
- mach/mach_host.h:174: unknown type name 'io_master_t' (x576)
  (Not an aarch64 issue: owner's macOS-26 SDK header sync (tools/sync-xnu-sdk-headers.py) installed 26-era task.h/mach_host.h without the matching mach_types.defs/types; likely a lost fix. This is the dominant blocker: ~3000 of the error lines.)
### bootstrap_cmds (host tool migcom; blocks MIG for everything)
- src/external/bootstrap_cmds/darling/include/mach/machine/boolean.h:37: #error architecture not supported (x56)
- src/external/bootstrap_cmds/migcom.tproj/strdefs.h:89: unknown type name 'boolean_t' (x146)
- src/external/bootstrap_cmds/darling/include/mach/port.h:149: unknown type name 'natural_t' (x42)
- src/external/bootstrap_cmds/darling/include/mach/port.h:371: unknown type name 'integer_t' (x14)
### objc4
- src/external/objc4/runtime/objc-runtime.mm:128: mismatch in debug-ness macros (x1) (arch-independent; upstream PR #1818 notes the same OBJC_IS_DEBUG_BUILD/NDEBUG issue, UPSTREAM_ARM64.md)
### src/startup/mldr
- src/startup/mldr/include/libkern/OSByteOrder.h:57: fatal: 'libkern/machine/OSByteOrder.h' file not found (x2) (no arm machine header)
- src/startup/mldr/elfcalls/threads.c:273: undeclared 'arg3'; invalid operands 'struct arg_struct' and 'int' (x3)
- src/startup/mldr/elfcalls/threads.c:274: assigning to 'struct arg_struct' from incompatible type 'long' (x1)
- src/startup/mldr/elfcalls/threads.c:321: #error Not implemented (x1) (x86-only thread start)
- src/startup/mldr/commpage.c:36-52: undeclared _COMM_PAGE64_BASE_ADDRESS, _COMM_PAGE32_BASE_ADDRESS, _COMM_PAGE64_AREA_LENGTH, _COMM_PAGE32_AREA_LENGTH, _COMM_PAGE_SIGNATURE, _COMM_PAGE_START_ADDRESS (x4), _COMM_PAGE_VERSION, _COMM_PAGE_CPU_CAPABILITIES64, _COMM_PAGE_CPU_CAPABILITIES, _COMM_PAGE_THIS_VERSION, _COMM_PAGE_NCPUS (no arm cpu_capabilities.h)
(Total distinct messages: 31. Components libc/corefoundation/libxpc/libpthread/dyld/etc. failed only with the SDK-header errors above in the first pass; their own arch errors are hidden behind it. Also expected next layers (not reached): libsystem_kernel emulation x86 asm/linux-syscall.S, darlingserver duct-tape x86 rtclock, arm64 thread state - upstream arm64-support touches all of these.)

## Dependencies
Installed with `sudo -n apt-get install --no-install-recommends` (sudo works): cmake clang lld ninja-build bison flex pkg-config git python3 libfuse-dev libudev-dev libcap-dev libglu1-mesa-dev libcairo2-dev libgl1-mesa-dev libtiff-dev libfreetype-dev libxml2-dev libegl1-mesa-dev libfontconfig1-dev libbsd-dev libxrandr-dev libxcursor-dev libgif-dev libpulse-dev libavformat-dev libavcodec-dev libswresample-dev libdbus-1-dev libxkbfile-dev libssl-dev libstdc++-14-dev libxcb-image0-dev libxcb-keysyms1-dev libxcb-ewmh-dev libxcb-icccm4-dev libxcb-xkb-dev libwayland-dev libxkbcommon-dev libjpeg-dev libpng-dev xz-utils rustc cargo.
(List compiled from memory of upstream's documented Linux build deps and the log; not taken from a file in the repo. Configure found everything it needed except Vulkan/LLVM (Metal) and Python 2.)
Side effect: apt reported `systemd-resolved 259.9 ... iU (unpacked, not configured)`; dpkg error code 1; DNS still resolves (github.com ok). Not touched. All -dev packages configured fine.
Toolchain gaps recorded: no Darling ld64 / cctools-port tools yet (built by the same ninja graph; cctools failed on SDK headers, 25 steps), no Python 2, no Vulkan SDK, no gcc-multilib (not needed on aarch64).
df after: 24 GB free on / (98G, 75% used).
