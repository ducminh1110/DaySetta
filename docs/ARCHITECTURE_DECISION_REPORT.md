# Architecture decision report (2026-11-06)

Status: **PROPOSAL, awaiting owner approval.** Nothing in the primary docs has been
rewritten yet. Audits backing every claim: `artifacts/assessment/daysetta.md`,
`artifacts/assessment/daylight.md`, `artifacts/assessment/runtime.md`,
`artifacts/assessment/darling.md`, `artifacts/assessment/conflicts.md`.
Evidence classes: **EXEC** = executed artifact (screenshot, log, test run);
**CODE** = source present; **PROSE** = documentation claim only; **UNKNOWN**.

Owner direction (highest authority): DaySetta is the continuation of Daylight, an
IPSW-driven macOS userland compatibility platform on Linux (Wine/Darling-like). Real Apple
components extracted from the user's IPSW are executed at runtime. Not a VM, not a fake
desktop, not an isolated window server. Apple binaries are never committed to the repo.

## 1. Daylight: verified capabilities

| Capability | Class | Evidence |
|---|---|---|
| Real BaseSystem 26.6 x86_64 Terminal.app drawn, keyboard + click input | EXEC | `daylight/docs/milestones/*.png` (10 screenshots, Oct 30-31) |
| AppKit/CoreText/QuartzCore run unmodified, drawn via client-side canvases | EXEC | `docs/milestones/hello2-darling-sf-font.png`, commit `e8126f6` |
| SkyLight MIG server (Mach front end) | CODE | `daylight/server/src/dispatch.c`, `dl_routines.c`; 15/738 routines real, rest canned (`dl_canned.c`) |
| X11 display frontend | CODE | `daylight/frontends/x11/daylight-x11.c` |
| 26.6 protocol baseline, API catalogue (26.6 + 27) | CODE/data | `daylight/protocol/macos-26.6-x86_64.json`, `daylight/data/apple-api/coverage.json` |
| Mach-O / split-cache reader | CODE | `daylight/tools/apicatalog/machoinfo.py` |
| WebKit pages, Dock, authd, 25-app scan | PROSE | no artifact in tree |
| Runtime host (x86 Darling guest) | **LOST** | KI-14, `daylight/docs/knowledge/RECOVERY_AND_RECONSTRUCTION.md` |

## 2. DaySetta: verified capabilities

| Capability | Class | Evidence |
|---|---|---|
| Native ds/1 server, broker, scene, CPU compositor, memfd surfaces, event ring + eventfd wakeup | EXEC | `./build/daysetta_tests` 23/23, `bash ci/run.sh` CI PASSED (commit `e35c379`) |
| arm64e cache pointer decoder (slide info v5) | EXEC | `tools/macos27/fixups.py`, `test_fixups.py` exit 0 |
| macOS 27 SkyLight MIG inventory (9 subsystems, 744 routines) | EXEC | `artifacts/macos-27/mig-routines.json`, `verify_wp1.sh` VERIFY PASS |
| Mach transport, MIG dispatch, loader, Darwin layer, frameworks, display backend | ABSENT | `artifacts/assessment/daysetta.md` |

## 3. IPSW extraction and runtime

| Item | Class | Evidence |
|---|---|---|
| 27.0.1 IPSW on disk (26.6 GB, 180 identities) | EXEC | `/home/ubuntu/macos-re/ipsw/`, BuildManifest |
| System volume, BaseSystem, SystemOS cryptexes are AEA-encrypted; AppOS + Rosetta plain | EXEC | `artifacts/assessment/runtime.md` |
| `ipsw` 3.1.739 supports `extract --fcs-key`, `--pem-db`, `fw aea -k/-p` | EXEC | `ipsw extract --help`, `ipsw fw aea --help` |
| 27 arm64e dyld cache extracted (6.6 GB) | EXEC | `/home/ubuntu/macos-re/cache-arm64e/` |
| APFS reader | ABSENT | no apfs-fuse; no extracted filesystem tree |
| BaseSystem 26.6 extracted tree | **MISSING** on this host | `~/macos-re/basesystem`, `~/macos-re/recovery` absent |
| `ia-cryptex` = RIDIFF10 delta from InstallAssistant 26.7.2, base unknown | EXEC | `/home/ubuntu/cleanup-report/IA_CRYPTEX.txt`; kept |
| Host CPU: ARMv8.0, **no pauth, no LSE atomics** | EXEC | `/proc/cpuinfo`; 27 cache uses BRAA/LDRAA/RETAB + LSE -> cannot run natively |

## 4. Darling reuse assessment

| Facility | Verdict | Why |
|---|---|---|
| darlingserver (Mach IPC emulation) | Adapt (reference) / optional backend | x86-only thread state; clean upstream locally; GPLv3 |
| libsystem_kernel shims (xnu-12377 port) | Adapt (reference) | port vendored (`3780f21`) but later fixes lost |
| launchd + XPC v5 pipe protocol | Reimplement from documented findings | `xpc_pipe_v5.c` lost; protocol knowledge in memory/handoff PROSE |
| mldr (ELF loader of Mach-O) | Replace | `#error TODO: aarch64` at `mldr.c:260,436` |
| Whole fork as Darwin layer | Not now | 138/149 submodules uninitialised, not buildable, not reproducible to last good state, x86-only |

Darling stays an explicit option (owner decision): usable as an out-of-process backend on an
x86_64 host, and as the primary reference for Darwin semantics. Final choice: **owner Q2**.

## 5. Duplicates and integration boundary

| DaySetta | Daylight counterpart | Verdict |
|---|---|---|
| `src/server`, `src/scene`, `src/render` | `daylight/server/src/*.c` | Overlap in purpose; DaySetta's is tested, Daylight's speaks MIG. **Integrate**: put Daylight's MIG dispatch in front of DaySetta's scene/compositor |
| ds/1 protocol, SDK, ctl | none | Keep as internal/native test path, not the product interface |
| planned `ds-loader`, `ds-rt` | Darling + dyld-1378 | **Drop as clean-room plans**; replaced by real Apple dyld/frameworks |
| `tools/macos27/fixups.py`, `scan_mig.py` | `machoinfo.py`, `mig_scan.py` | New/extension; keep |

## 6. Genuinely missing (needs new work)

1. IPSW pipeline: AEA decrypt (wrap `ipsw`), DMG check, APFS read, component store + provenance.
2. Execution substrate for arm64e on this host (emulator or PAC/LSE-capable host).
3. aarch64 Darwin layer: syscall/Mach-trap entry (`svc #0x80`, x16), Mach IPC, bootstrap/launchd, commpage, signals.
4. SkyLight Mach front end attached to DaySetta's server (Daylight's dispatch as the start).
5. Linux display backend in DaySetta (X11 from Daylight, Wayland later).

## 7. Recommended unified architecture

```
IPSW -> ds-ipsw (wraps ipsw: fcs keys, AEA, DMG, APFS) -> component store (per build/arch, provenance JSON)
     -> ds-exec: Apple dyld + real dyld cache, on an arm64e-capable CPU path
     -> Darwin layer: Mach IPC + BSD/trap shim + launchd/XPC (Darling-informed, DaySetta-owned)
     -> real Apple frameworks (libSystem .. AppKit, SkyLight client side)
     -> SkyLight MIG front end (from Daylight) -> DaySetta scene/compositor -> X11/Wayland
```

## 8. Roadmap to the first real Apple app

| Phase | Goal | Acceptance |
|---|---|---|
| R0 | Docs reconciled to owner direction | conflict register applied (`artifacts/assessment/conflicts.md`), `ci/check_docs.py` 0 errors |
| R1 | IPSW component store for 27.0.1 | `ipsw` decrypts SystemOS/BaseSystem AEA (key source logged per image); DMG valid; APFS file list + SHA-256 manifest; no Apple bytes in repo |
| R2 | First real Apple code runs | a pure function from the 27 arm64e cache returns the same result as on the reference Mac |
| R3 | First real Apple executable | Apple `/bin/echo` (27, from R1 store) via Apple dyld + real cache prints byte-identical output to the Mac |
| R4 | CoreFoundation/Foundation client | Apple `plutil`/`defaults` run (needs launchd/XPC subset) |
| R5 | First real window | AppKit hello -> SkyLight MIG front end -> DaySetta compositor -> X11 window; screenshot artifact |
| R6 | First system app | Terminal.app from 27 interactive (parity with Daylight 26.6 milestone) |

## 9. Owner decisions needed

1. **Execution path for arm64e** (blocks R2+): (a) emulator such as `qemu-aarch64 -cpu max` (has PAC+LSE, slow, Darwin syscalls still need our shim); (b) a PAC+LSE-capable arm64 Linux host (ARMv8.3+); (c) x86_64 route with the x86_64 SystemOS cryptex + Darling (proven on 26.6, host unavailable).
2. **Darwin layer**: DaySetta-owned selective reimplementation (recommended), Darling fork, or hybrid backend.
3. **Licence**: DaySetta has no LICENSE; linking Darling code means GPLv3.
4. **Target**: 27.0.1 arm64e first (recommended, matches the IPSW on disk) or 26.x x86_64.
5. Approve applying the conflict register (R0).
