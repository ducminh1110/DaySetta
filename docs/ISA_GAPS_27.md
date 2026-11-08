# ISA gaps in macOS 27 arm64e shared cache (vs ARMv8.0 host)

Method: extracted AppKit, CoreFoundation, Foundation, CoreGraphics, QuartzCore,
CoreText, ImageIO, SkyLight, libsystem_malloc, libcompression, Security,
libsqlite3, libobjc, CoreAutoLayout, HIToolbox, Metal, CoreImage to
`~/macos-re/m27x`, disassembled with `llvm-objdump -d --mattr=+v8a`, piped every
`<unknown>` word through ds-exec `decode()` (`tools/scan.cpp`: build with
`g++ -std=c++20 -I src/exec -Isrc tools/scan.cpp src/exec/lowering.cpp
src/exec/pac.cpp src/exec/trampoline.cpp -o scan -pthread`), and named the
representatives with `llvm-mc`/`llvm-objdump -mattr=+all`.

Total: 476,816 words the ARMv8.0 disassembler cannot name; ds-exec already
covered 472,265; 4,551 distinct occurrences remain uncovered (class VERIFIED).

| Group | Count | Top libraries | Status |
|---|---|---|---|
| FP16 (scalar h-reg + vector .8h/.4h) | ~3,900 | CoreGraphics (3,700), QuartzCore, ImageIO | OUT OF SCOPE (another agent) |
| MTE (ldg/stg/st2g/irg/gmi) | 188 | libsystem_malloc (167), QuartzCore (21) | DO NOT EMULATE (see below) |
| SHA3 (BCAX 40, EOR3 2) | 42 | CoreGraphics (40), Foundation, Metal | IMPLEMENTED (this change) |
| Data false positives (ffffffxx, 0x03xxxxxx tables) | ~400 | libcompression, QuartzCore, libsqlite3 | not instructions |
| SME fmopa | 1 | libcompression (data table, not code) | DO NOT EMULATE (see below) |
| FHM, RDM, FCMA, JSCVT, FRINT32/64, FLAGM, SHA512 | 0 | — | absent from all 17 libs; nothing to cover |

SHA3 encodings (clang -march=armv8.2-a+sha3): EOR3 `0xCE020C20`-family
(bits[23:21]=000, bit15=0), BCAX (001), RAX1 (011, bit15=1, bits[14:12]=000,
bits[11:10]=3), XAR (100, imm6). Semantics from arm64emu `exec_fpsimd.c`
`crypto_sha3_512` (Apache-2.0, arm64emu by sylirre); BCAX = `Vn ^ (Vm & ~Va)`
checked against qemu. SHA512 (same 0xCE space, bits[11:10]=0/1/2) is left
unsupported. Decode+emulate+inline ARMv8.0 trampolines in src/exec
(lower `policy.sha3`).

## MTE / SME / SVE: do not emulate

MTE/SME/SVE are architecturally incompatible with trap-and-emulate here (MTE
needs tag memory; SME/SVE need register-file widths this host lacks), so Apple
code must never take those paths. Apple code decides via
`sysctlbyname("hw.optional.arm.FEAT_MTE"|"FEAT_SME"|"FEAT_SVE"|"FEAT_SVE2",...)`
(BOOL; missing sysctl = ENOENT = absent) and commpage capability bits.

Darling view (READ ONLY):
- `~/darling/src/external/xnu/darling/src/libsystem_kernel/emulation/src/xnu_syscall/bsd/helper/misc/sysctl_hw.c`
  (table `sysctls_hw` ~line 47): only availcpu/ncpu/physicalcpu/logicalcpu/
  memsize/pagesize/cputype/cpusubtype/cpu64bit_capable/machine/model/
  cpufrequency. No `optional`/`FEAT_*` entries exist.
- Unknown names fail with `-ENOENT`
  (`.../bsd/impl/misc/sysctl.c:138`), i.e. features are reported absent today.
- DaySetta commpage (`src/darwin/commpage.cpp` in this worktree) only sets
  capability bits the host really implements (HWCAP-gated); no SME/SVE/MTE bit
  can ever be set on this ARMv8.0 host.

Proposed (not applied) change to `sysctl_hw.c`: to make absence explicit
rather than rely on ENOENT, add handlers for `machdep`/`hw.optional` names
returning 0 (absent) for FEAT_MTE, FEAT_SME, FEAT_SVE, FEAT_SVE2 — oids must
come from the Apple sysctl header, not invented here. Equivalently, keep the
current ENOENT behaviour and add a comment stating the intent. Do NOT add
handlers reporting these features present.

Counts above justify this: MTE appears only in libsystem_malloc/QuartzCore
(unguarded? — under FEAT_MTE off it is never executed, and our sysctl reports
absent, so the dead code stays dead), and SME/SVE appear only as data tables.
