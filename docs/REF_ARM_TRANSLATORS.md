# Reference survey: two ARM64 -> x86-64 translators

Docs-only, read-only study (WP-R step 2). No code, text or structure from either project is
copied here; everything is paraphrased, with a citation. Nothing in this file is a claim about
DaySetta, macOS 27, or any compatibility. Nothing was built or run; nothing was downloaded.

Evidence classes: `VERIFIED` = seen in a local clone or release JSON (a vendor's own
statement is verified only as *text*, never as behaviour); `UNKNOWN` = not in the material.
Clones: `/home/ubuntu/ref/{1401,nvidia-macos-driver,rebar}`, each a depth-1 sparse checkout
(one commit each). Release JSON: `/home/ubuntu/ref/releases-*.json`.

Pitfall: `nvidia-macos-driver/translator/` is an Apple AIR -> SPIR-V *shader* translator
(LGPL-3.0, metal2vulkan-based, not cloned). It is unrelated to the ARM-app translator.

## 1. Seed facts

Status VERIFIED / CONTRADICTED / UNVERIFIED. Evidence = clone file + lines, or release tag.

| Id | Seed fact (short) | Status | Evidence |
|---|---|---|---|
| N1 | v2.7: opt-in, experimental AOT of ARM code to x86-64 | VERIFIED (text) | release `v2.7` of nvidia-macos-driver and 1401; `/home/ubuntu/ref/nvidia-macos-driver/CHANGELOG.md:25`; `/home/ubuntu/ref/1401/CHANGELOG.md:28` |
| N2 | Original app unchanged; "Turn off" removes translator and copies | VERIFIED (text) | `/home/ubuntu/ref/1401/mac/Sources/ArmApps.swift:9-14` (copies go to `~/Applications/NullMoth ARM`, uninstaller removes them) |
| N3 | CLI tools OK (lz4, xxhsum, cmake, z3 byte-identical); windowed experimental; Go/Flutter/Java/Electron/SVE video codecs no; games unsupported in 2.7 | VERIFIED (text); behaviour UNVERIFIED | `/home/ubuntu/ref/nvidia-macos-driver/CHANGELOG.md:26`; release `v2.7` |
| N4 | Needs AES-capable CPU and Xcode CLT | VERIFIED, understated | CPU floor is AES, PCLMULQDQ, SSE4.2, POPCNT: `/home/ubuntu/ref/1401/mac/Sources/ArmApps.swift:428-437`; CLT prompt `/home/ubuntu/ref/1401/mac/Sources/ArmApps.swift:479-481`; same sentence in `/home/ubuntu/ref/nvidia-macos-driver/CHANGELOG.md:26` |
| N5 | v2.7.1: translator under /Library/NullMoth, installer keeps it readable | VERIFIED (text) | release `v2.7.1`; `/home/ubuntu/ref/nvidia-macos-driver/CHANGELOG.md:20`; `/home/ubuntu/ref/1401/CHANGELOG.md:22`; install root `/home/ubuntu/ref/1401/mac/Sources/ArmApps.swift:17-18` |
| N6 | 1401 commit "Changelog 2.7.2: translation fixes and the shared system page" | UNVERIFIED | Clones hold one commit each; 1401 HEAD subject differs, and the nvidia clone's "Changelog 2.7.2: ..." subject names a test driver. Content is corroborated by changelog entries `/home/ubuntu/ref/1401/CHANGELOG.md:7-15` (translation fixes line 13, system page line 14), not by the commit message |
| R1 | Interpreter + x86-64 JIT, Mach-O loader doing dyld's job, bridge to host system libs | VERIFIED | `/home/ubuntu/ref/rebar/README.md:5`; `/home/ubuntu/ref/rebar/loader.h:1-8`; `/home/ubuntu/ref/rebar/bridge.h:3-13` |
| R2 | Builds on macOS x86_64 + Xcode CLT; `./build.sh` -> `./rebar`; run on an arm64 binary | VERIFIED (text; not executed) | `/home/ubuntu/ref/rebar/README.md:7-21` |
| R3 | Component file names | VERIFIED except `samples/` | All listed headers/`.c`/`launcher/`/`gpucheck/`/`tools/` exist (`ls`). `samples/` is absent from the sparse clone but is the directory the test runner enters: `/home/ubuntu/ref/rebar/run_tests.sh:1-5` |
| R4 | Cache of block start PCs; objc_msgSend fast path; Swift interplay; iOS apps with UIKit and own keychain; Homebrew bottles via rebrew; LSE/CSSC/I8MM/BF16; unwind personality | VERIFIED in sources and release notes, not in commit log | cache `/home/ubuntu/ref/rebar/jitcache.h:1-6`; fast path release `v0.6.0`; Swift `/home/ubuntu/ref/rebar/objc.h:714-717`; iOS `/home/ubuntu/ref/rebar/tools/ipa.py:6-9`, keychain `/home/ubuntu/ref/rebar/build.sh:10`; rebrew `/home/ubuntu/ref/rebar/tools/rebrew.py:2-4`; LSE `/home/ubuntu/ref/rebar/atomics.h:1-4`; CSSC release `v0.6.0`; I8MM `/home/ubuntu/ref/rebar/fpsimd.h:1274-1275`, BF16 `/home/ubuntu/ref/rebar/run_tests.sh:207-208`; unwind `/home/ubuntu/ref/rebar/unwind.h:1-4` |
| R5 | rebar license unknown; treat as all-rights-reserved | VERIFIED | no LICENSE file: `/home/ubuntu/ref/LICENSES.txt:21-22` |

Tally: 10 VERIFIED, 0 CONTRADICTED, 1 UNVERIFIED (N6).

## 2. Q1 - where each translator lives

* **nullmoth ARM-app translator: binary/download only.** Its source is not in the checkout
  (`/home/ubuntu/ref/SOURCE_PRESENCE.txt:1-3`). The Mac app downloads a package named
  `nullmoth-arm-<version>.tar.gz` via a manifest URL
  (`/home/ubuntu/ref/1401/mac/Sources/ArmApps.swift:16`, name rule `:32-34`), and installs it
  as `/Library/NullMoth/arm/bin/nm-arm` (`:17-18`). No release asset of v2.7, v2.7.1 or v2.7.2 in
  either releases JSON carries such a package (asset lists in `releases-1401.json`,
  `releases-nvidia-macos-driver.json`). The manifest was not fetched. Stopping here by design: no
  inspection of the shipped package.
* **rebar: source in a public repo, plus release zips.** C sources are present
  (`/home/ubuntu/ref/SOURCE_PRESENCE.txt:7-11`). Releases v0.1.0..v0.6.1 each carry a `rebar`
  zip and, from v0.2.0, a launcher app zip (`releases-rebar.json`, `.[].assets[].name`).
  License: no file, so all rights reserved for our purposes.

## 3. Q2 - architecture as far as text shows

| Topic | nullmoth ARM apps | rebar |
|---|---|---|
| Strategy | Ahead-of-time: ARM code of an app compiled to x86-64 and written as a new copy (release `v2.7`; `/home/ubuntu/ref/1401/mac/Sources/ArmApps.swift:9-14`). Internal design UNKNOWN | Interpreter plus template JIT as accelerator; user-mode dynamic translation (`/home/ubuntu/ref/rebar/jit.h:1-2`; release `v0.6.0` first line) |
| Input unit | Whole app bundle / program (translation of "an app's ARM code"); finer unit UNKNOWN. Homebrew formula binaries translated at install, helpers on first run (`/home/ubuntu/ref/1401/CHANGELOG.md:8`) | Basic block: straight-line code up to the next branch (`/home/ubuntu/ref/rebar/jit.h:3-4`) |
| System libraries | UNKNOWN. Not shown whether translated or host-linked | Guest imports resolve to stubs; host x86-64 libSystem and system frameworks are loaded with dlopen (`/home/ubuntu/ref/rebar/bridge.h:3-13`, `/home/ubuntu/ref/rebar/loader.h:1-10`) |
| ObjC / Swift | UNKNOWN | Guest classes registered with the host libobjc in place; msgSend bridged (`/home/ubuntu/ref/rebar/objc.h:1-9`); Swift classes held until the Swift runtime is ready (`/home/ubuntu/ref/rebar/objc.h:714-717`) |
| Exceptions | UNKNOWN | Guest frames unwound by rebar's own code, host frames by host unwinder; listed limits: DWARF-mode entries, exception specs, dependent exceptions, exceptions through callbacks (`/home/ubuntu/ref/rebar/unwind.h:1-19`) |
| Atomics | UNKNOWN | Host atomics; LSE ops as CAS loops, exclusive pairs via per-thread monitor, ABA tolerated (`/home/ubuntu/ref/rebar/atomics.h:1-4`); JIT maps acquire/release onto x86 ordering (`/home/ubuntu/ref/rebar/jitmem.h:1-6`) |
| SIMD / NEON | SVE, SM3, SM4 and the random-number register "behave as on Apple silicon" (`/home/ubuntu/ref/1401/CHANGELOG.md:9`); design UNKNOWN | Element-wise interpreter, vector fast paths, SSE/NEON JIT templates (`/home/ubuntu/ref/rebar/fpsimd.h:1-9`, `/home/ubuntu/ref/rebar/simdfast.h:1-5`, `/home/ubuntu/ref/rebar/jitneon.h:1-6`); SVE/SME unsupported (release `v0.6.0`) |
| FP semantics | UNKNOWN | Host double precision then rounded; FPCR rounding modes, flush-to-zero, exception flags and exact estimate tables not modelled; JIT sends NaN lanes to a helper (`/home/ubuntu/ref/rebar/fpsimd.h:1-9`, `/home/ubuntu/ref/rebar/jitneon.h:1-6`) |
| Translation cache | "Translated copies" on disk under `~/Applications/NullMoth ARM`; shared parts translated once and reused (`/home/ubuntu/ref/1401/CHANGELOG.md:8`); interrupted translation redone (`:12`) | Per-program list of block start offsets in `~/Library/Caches/rebar/jit`, tied to image path, size, mtime; machine code not stored in the 0.5-era design (`/home/ubuntu/ref/rebar/jitcache.h:1-6`); release `v0.6.0` says a code snapshot is now also mapped back |
| W+X memory | Refused as on Apple silicon unless requested for JIT (`/home/ubuntu/ref/1401/CHANGELOG.md:10`) | UNKNOWN |
| Unsupported / limits | Go, Flutter, Java/Electron, SVE video codecs, games (release `v2.7`); not on macOS later than 26 (`/home/ubuntu/ref/1401/CHANGELOG.md:11`) | SVE/SME (release `v0.6.0`); unwind limits above; process-wide JIT arena 1920 MB (release `v0.6.0`) |

Process helpers in rebar: an injected shim reroutes exec/spawn of arm64-only targets back
through rebar (`/home/ubuntu/ref/rebar/execshim.c:1-4`).

## 4. Q3 - comparison

| Row | nullmoth ARM apps | rebar |
|---|---|---|
| Strategy | AOT to a translated copy | Interpreter + JIT, run time |
| Host needed | macOS 15 or 26 on x86-64 with AES, PCLMULQDQ, SSE4.2, POPCNT (`/home/ubuntu/ref/1401/CHANGELOG.md:11`, `/home/ubuntu/ref/1401/mac/Sources/ArmApps.swift:428-437`) | macOS on x86_64 + Xcode CLT (`/home/ubuntu/ref/rebar/README.md:9`) |
| Runs (claimed) | CLI tools; Homebrew arm64 formulas (release `v2.7.2` of 1401) | Many CLIs and runtimes, Metal games, Steam windows (release `v0.6.0`) |
| Does not run | Go, Flutter, Java/Electron, games in 2.7 | SVE/SME |
| Dynamic-code runtimes | Java/Electron not working in 2.7; JIT-requested W+X is allowed | Claims HotSpot, CoreCLR, V8, JSC (releases `v0.4.0`, `v0.5.0`) |
| GUI apps | "Experimental" (release `v2.7`) | Launcher app; Steam/Godot/Zed claimed (release `v0.6.0`); UIKit iOS apps via `rebar-ios` (`/home/ubuntu/ref/rebar/tools/ipa.py:6-9`) |
| Releases | 3 (v2.7, v2.7.1, v2.7.2) of 78 in 1401 and 76 in nvidia-macos-driver | 7 (v0.1.0..v0.6.1) |
| Tests | UNKNOWN (source absent); outputs compared to Apple silicon for 4 tools (release `v2.7`) | 182 programs vs native x86_64 builds, per release `v0.6.0`; instructions checked against a C model; runner `/home/ubuntu/ref/rebar/run_tests.sh:1-5` |
| Support matrix | Prose list in release notes | Prose "Also working" and "Not supported" in release notes |
| Stars | unknown (user-pasted ~34) | unknown |
| License | PolyForm Noncommercial 1.0.0 for the repos; package license UNKNOWN (`/home/ubuntu/ref/LICENSES.txt:1-20`) | none found; all rights reserved |
| Install | Opt-in card, verified download, root install, removable | Unzip and run; or launcher app |

## 5. Q4 - ideas that might transfer later (ideas and test methods only)

* Opt-in, fully removable install: the feature lives in its own prefix with a single
  uninstaller; "off" removes derived data too (N2).
* Non-destructive output: never touch the original; write a derived copy elsewhere (N2).
* Cache keyed by binary identity (path, size, mtime) with image-relative offsets, valid across
  tool versions, with an off switch and a size cap (`/home/ubuntu/ref/rebar/jitcache.h:1-6`).
* CPU-feature gate before offering the feature; test features, not vendor
  (`/home/ubuntu/ref/1401/mac/Sources/ArmApps.swift:428-437`).
* Differential testing: run the same program through the translator and natively, compare output
  byte for byte (release `v2.7`; `/home/ubuntu/ref/rebar/run_tests.sh:1-3`).
* Instruction-level model check: execute encodings through two paths (interpreter vs JIT, fast
  path vs slow path) and compare registers (`/home/ubuntu/ref/rebar/simdfast.h:1-5`).
* Per-ISA-extension decode corpus, plus a probe that lists encodings not yet implemented
  (release `v0.5.0`, `--probe-insns`).
* Honest support matrix written as "works / experimental / not yet", with known gaps named.
* Treat dangerous guest requests like the real CPU does (W+X refusal, feature-absent
  instructions) rather than failing the whole run.

## 6. Q5 - implications for the North Star

* Neither project is shown, in the material read, to virtualize or boot a full macOS guest.
  Both are user-space translators for individual arm64 programs. UNKNOWN beyond the text.
* rebar bridges to the host's own system libraries, so it needs a macOS host
  (`/home/ubuntu/ref/rebar/bridge.h:3-13`); its README says macOS on x86_64
  (`/home/ubuntu/ref/rebar/README.md:9`). A Linux host has no such libraries to bridge to.
* nullmoth is ahead-of-time with stated limits on dynamic-code runtimes (release `v2.7`) and
  also targets a macOS host (`/home/ubuntu/ref/1401/CHANGELOG.md:11`).
* The user's vision is a whole macOS desktop inside one window, used like a VM. That is a
  different class of problem from translating programs; these references only inform the later
  "run macOS apps on x86_64" item. See ROADMAP.md, "North Star (not started)".

## 7. Not checked

Behaviour of either translator (nothing run); contents of the nullmoth package and manifest;
stars; rebar commit history beyond one depth-1 commit; `rebar/samples/`; non-clone files.
