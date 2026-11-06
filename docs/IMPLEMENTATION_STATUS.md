# Implementation status

> Snapshot: **2026-11-06T08:21Z**. This page describes the repository at that moment.
> A design statement is not an implementation. A test is not passing unless the run
> below says so. The continuation brief is [`HANDOFF_FULL.md`](HANDOFF_FULL.md).

## 1. Verification run

All commands ran in `/home/ubuntu/daysetta`. Artifacts: `build/Testing/Temporary/LastTest.log`,
`/tmp/opencode/final-ci.log`.

| Step | Command | Exact result | Class |
|---|---|---|---|
| Configure | `cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo` | exit 0 | `VERIFIED` |
| Build | `cmake --build build -j8` | exit 0; `-Wall -Wextra -Wpedantic -Werror` clean | `VERIFIED` |
| CTest | `ctest --test-dir build --output-on-failure` | 1/1 target passed, 0.39 s | `VERIFIED` |
| Direct suite | `./build/daysetta_tests` | **34/34 cases passed** (23 earlier cases + 11 `exec_*`) | `VERIFIED` |
| Docs | `python3 ci/check_docs.py` | 0 errors, 24 Markdown files, 58 path refs | `VERIFIED` |
| Local CI | `bash ci/run.sh` | **CI PASSED** | `VERIFIED` |

The end-to-end case is `e2e_two_windows_capture_and_input`. It starts a real broker and
server, connects through the SDK, creates two windows, commits `memfd` surfaces, makes
them visible, captures and re-captures identical pixels, injects input into the top
window, receives the event through the shared ring, parses scene JSON, round-trips
`ping`, writes PPM frames and tears the processes down.

## 2. Milestones

| Milestone | State | Evidence | Why it is not closed |
|---|---|---|---|
| M0 — evidence and contracts | **IN PROGRESS** | `adr/0001`–`adr/0004`, `docs/CI.md`, `ci/check_docs.py` | Review not signed; evidence index partial; macOS 27 MIG inventory missing |
| M1 — runtime and transport | **IN PROGRESS** | `src/ipc/`, `src/protocol/`, `src/event/`, broker, SDK, e2e | No fuzz/1M-message/fd-leak/peer tests; `ds-loader` and `ds-rt` absent |
| M2 — native window server | **IN PROGRESS** | `src/scene/`, `src/render/`, `src/server/`, `src/sdk/` | No multi-client, resize/occlusion, golden manifest, ASan/UBSan or soak |
| M3 — SkyLight dialect | `PLANNED` | `ROADMAP.md` §5 | No dialect; macOS 27 ids `UNKNOWN`; 27 MIG scan not done |
| M4 — substrate core | `PLANNED` | `ROADMAP.md` §6 | No Mach-O loader or `ds-rt` |
| M5 — AppKit class and materials | `PLANNED` | `ROADMAP.md` §7 | No ObjC runtime or material graph |
| M6 — hardening and 1.0 | `PLANNED` | `ROADMAP.md` §8 | No release candidate |

Passing the native e2e does **not** close M1 or M2. Their exit criteria include fuzzing,
load, soak, thread determinism and loader fixtures that do not exist yet.

## 3. Components

| Component | State | Evidence | Test status |
|---|---|---|---|
| `ds-ipc` | `IMPLEMENTED` | `src/ipc/ipc.cpp`; `SOCK_SEQPACKET`, `SCM_RIGHTS`, `SO_PEERCRED` | Exercised by broker/SDK/e2e; no isolated fuzz test |
| `ds-protocol` | `IMPLEMENTED` | `src/protocol/protocol.cpp`; 32-byte `DSM1` | Exercised by e2e/SDK; malformed corpus pending |
| `ds-event` | `IMPLEMENTED` | `src/event/event.cpp`; 40-byte event, 32-byte ring header | e2e event delivery; `event_ring_move` test; `poll_event` blocks on a per-client eventfd (no 5 ms sleep), e2e wakeup + 50 ms timeout timing checks |
| `ds-scene` | `IMPLEMENTED` / `PARTIAL` | `src/scene/scene.cpp` | 8 `scene_*` cases pass; no spaces/notes/damage scheduling |
| `ds-render` | `IMPLEMENTED` / `PARTIAL` | `src/render/` | 10 `compositor_*` cases pass; CPU single-threaded, no materials |
| `ds-sdk` | `IMPLEMENTED` | `src/sdk/sdk.cpp`, `src/sdk/broker_proto.cpp` | `sdk_fake_server`, `sdk_hello_denied`, e2e |
| `daysetta-broker` | `IMPLEMENTED` | `src/broker/broker.cpp` | `broker_proto_*`, `broker_live_registry` |
| `daysetta-server` | `IMPLEMENTED` | `src/server/server.cpp` | e2e dispatch/capture/input/frames |
| `daysetta-session` | `IMPLEMENTED`, unexercised | `src/session/main.cpp` | No automated test |
| `daysetta-ctl` | `IMPLEMENTED` | `src/ctl/main.cpp` | No automated CLI test; commands built |
| Headless output | `IMPLEMENTED` | `src/render/output.cpp` | PPM/PAM/BGRA test passes |
| Wayland / X11 / Vulkan | `PLANNED` | ADR-0004 | Development headers absent; not buildable on this host |
| `ds-exec` + `ds-exec-probe` (R2) | `PARTIAL` | `src/exec/` (software PAC, LSE/PAC/svc BRK lowering, trap engine, split-cache map + slide v5, export-trie lookup), `tools/ds-exec-probe/main.cpp` | 11 `exec_*` cases incl. 4-thread LDADD contention; run in [r2-probe.txt](../artifacts/exec/r2-probe.txt) |
| `ds-run` + `src/darwin/` (R3) | `PARTIAL` | `tools/ds-run/main.cpp`, `src/darwin/` (Mach-O arm64e loader, Darwin initial stack, commpage, shared-region dynamic-config page, BSD/Mach trap layer with DS_TRACE, ABI translation), `src/exec` extended (`mrs tpidrro_el0`, LDAPUR traps, LDAPR to LDAR rewrite) | 12 `darwin_*` cases; echo not reached: [r3-echo.txt](../artifacts/exec/r3-echo.txt), requirements record [r3-dyld27-requirements.md](../artifacts/exec/r3-dyld27-requirements.md) |
| macOS 27 MIG scan | **DONE (WP1b, static)** | `tools/macos27/scan_mig.py`, `artifacts/macos-27/mig-routines.json` | 9 subsystems, 744 routines; see WP1b section |

The 22 cases are `broker_proto_roundtrip`, `broker_proto_malformed`,
`broker_live_registry`, 10 `compositor_*` cases, `e2e_two_windows_capture_and_input`,
8 `scene_*` cases, `sdk_fake_server`, `sdk_hello_denied`.

## 4. Wire and security state

`docs/DS1_WIRE.md` is authoritative for bytes; `include/daysetta/*.hpp` is authoritative
for C++ signatures. The implemented surface is:

* 32-byte `DSM1` header; `kMaxMessage` 1 MiB; little-endian integers.
* `SOCK_SEQPACKET` datagrams; `SCM_RIGHTS` descriptors arrive atomically with the header.
* Broker `DSK1` register/lookup/unregister/list/ping; same-uid only; 64-bit tokens; the
  token is required in `hello`.
* 40-byte event records in a 32-byte-header ring with 256 slots. The server is the sole
  producer; each client is a consumer.

Security limits: same-uid only; no privilege separation between broker and server beyond
process boundaries; no audit log/revocation test; no fuzz corpus. Do not claim a security
review.

## 5. macOS 27 evidence

| Item | State | Class |
|---|---|---|
| Target | macOS 27.0.1 arm64e, 26A434, `Mac14,3` | `VERIFIED` |
| Split cache | `/home/ubuntu/macos-re/cache-arm64e/`; 4 088 image objects loaded; SkyLight vmaddr `0x187093000`, UUID `8A3B348E4637368592D06CBC2F36A234` | `VERIFIED` |
| Catalogue | 4 099 images, 4 574 612 exports, 0 ObjC class/selector names, 3 137 ObjC metadata images | `VERIFIED` from `daylight/data/apple-api/coverage.json` |
| IPSW | 26 637 307 067 bytes; SHA-256 `2f016638293c3e641b8b25391a76fbc16563b3711915a5551cf8aa0f5598a5c1`; manifest 27.0.1/26A434, `Mac14,3` supported | `VERIFIED` |
| 26.6 protocol baseline | 738 MIG routines, 9 subsystems | `VERIFIED` |
| 27 MIG ids | Scanned: 9 subsystems, 744 routines (addresses; 724 names inferred from 26.6) | `OBSERVED` (WP1b) |
| 27 ObjC names | `__objc_classname`/`__objc_methname` size 0; pointer salt partially decoded (D-20/D-21) | `UNKNOWN` |
| 27 runtime | No loader or ObjC runtime. R2 (`VERIFIED`, [r2-probe.txt](../artifacts/exec/r2-probe.txt)): genuine arm64e cache code runs on this host (no PAC, no LSE) for `_strlen`, `_memcmp`, `_strtol_l`, `_adler32`, `_crc32`; results equal host references; 6 traps (pac-sign 3, pac-auth 1, ret-auth 2), 5 pages lowered lazily. PAC is a software SipHash-2-4 MAC, not QARMA, VA 48 bits (macOS VA size `UNKNOWN`); FEAT_LRCPC `LDAPR` words exist in Apple code and are not lowered (SIGILL reported, `OBSERVED` 1 in the 5 lowered pages, 40 in libc++ `__text`); plain `_strtol` needs Darwin TSD (`tpidrro_el0`); no dyld, TLS, stubs-to-other-images or syscalls tested | `PARTIAL` |

## 6. Not verified

| Item | Class | Why |
|---|---|---|
| Multi-client scene and contention | `UNKNOWN` | Only one SDK client in e2e |
| Resize, occlusion, surface sequence gaps | `UNKNOWN` | No test coverage |
| Fuzz, 1 000 000-message loopback, fd/RSS leak | `UNKNOWN` | M1 tests not written |
| ASan/UBSan, 8-hour soak, 1/2/8-thread determinism | `UNKNOWN` | M2 tests not written |
| `daysetta-session` lifecycle | `UNKNOWN` | Builds, no automated test |
| `daysetta-ctl` CLI | `UNKNOWN` | Builds, no automated test |
| macOS 27 protocol ids/names | `UNKNOWN` | MIG scan not done; ObjC name sections empty |
| Wayland/X11/Vulkan output | `UNKNOWN` | Dev headers absent; ADR-0004 defers them |
| GitHub Actions execution | `UNKNOWN` | Workflow exists; no remote run from this host |

## Deferred: x86_64

| Item | Class | Why |
|---|---|---|
| Build/run rebar (needs macOS x86_64 host) | `UNKNOWN` | No x86_64 machine; arm64 host only. Study is docs-only: [REF_ARM_TRANSLATORS](REF_ARM_TRANSLATORS.md) |
| macOS 26.6 x86_64 guest re-runs | `UNKNOWN` | x86 guest was wiped; evidence is catalogued ([HANDOFF_FULL](HANDOFF_FULL.md) near line 382) |

## 7. Reproduce

```sh
cd /home/ubuntu/daysetta
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j8
./build/daysetta_tests
ctest --test-dir build --output-on-failure
python3 ci/check_docs.py
bash ci/run.sh
```

If a count differs, record the exact command, output and timestamp here. Do not carry a
previous pass or failure forward. The implementation and this status were committed
locally after this green run; there is no remote. Use `git log -1` for the exact
snapshot commit.

## WP1a: macOS 27 cache pointer decoder

`python3 -I tools/macos27/test_fixups.py` exited 0 (T1: unit decode of chained-pointer
formats 1/2/6/7/13; T2: real SkyLight data). OBSERVED in
`artifacts/macos-27/fixups-selftest.json`: all 14 slid mappings of the 26A434 arm64e cache
use slide info v5, SkyLight has no `LC_DYLD_CHAINED_FIXUPS`, 141/141 non-zero `__got`
slots decode into a mapping (122 equal an exported symbol address), 303/303 `__objc_classlist`
slots equal `0x180000000 + (value & 0xFFFFFFFF)`. 2940 `__got`/`__auth_got` slots are zero
(unbound, not decodable). `isa`/`superclass`/`ro` encoding: `UNKNOWN`, not tested. Decoder:
`tools/macos27/fixups.py`. No macOS 27 compatibility is claimed.

## WP1b: macOS 27 MIG scan

`python3 -I tools/macos27/scan_mig.py` exited 0; `bash tools/macos27/verify_wp1.sh | tail -1` printed
VERIFY PASS. OBSERVED in `artifacts/macos-27/mig-routines.json`: 9 subsystems, 744 routines (26.6: 9, 738).
Counts 27/26.6: CGXRendezvous 42/41, CGXServices 580/575 (13 ids added, 8 removed), CGXWindowServer 72/72,
ServicesRelocated 29/29, ServerRelocated 10/10, CGXHMD 5/5, three start-1 subsystems 2/2 each
(not attributable to CGYSpinControl/CGYCaptureReply/CGYEventTap, names "?"). Names are
`inferred-from-26.6` by (start, id) only: 724 named, 20 "?". Report: `artifacts/macos-27/MIG_27_REPORT.md`.
No macOS 27 compatibility is claimed.

## R3: genuine echo under genuine 27 dyld (`ds-run`)

`OBSERVED` (2026-11-06, `DS_TRACE=1 ./build/ds-run /home/ubuntu/macos-re/store-27/root/bin/echo hello from macOS 27`):
the genuine 27 `/usr/lib/dyld` (arm64e slice) is loaded by ds-run, runs `__dyld_start`, restarts into the dyld in the
mapped cache, finds the cache through `shared_region_check_np` + the dynamic-config page + `fsgetpath`, parses the
main executable and starts libSystem initialization. Echo does NOT print: exit status 133 (SIGTRAP) from a genuine
Apple `brk #0xb001` in `libsystem_platform.dylib+0x3d40` right after the first unimplemented
`_kernelrpc_mach_vm_map_trap` in `libsystem_kernel`. No `write` by Apple code was reached. The Darwin layer is
deliberately limited to what the trace demanded (the owner chose Darling aarch64 as the Darwin runtime); everything
the genuine dyld/libSystem asked for is recorded in [r3-dyld27-requirements.md](../artifacts/exec/r3-dyld27-requirements.md).
Lowering findings: 12 LDAPR words rewritten to LDAR in dyld, 37 `mrs tpidrro_el0` trapped, 167 svc, no LDAPUR seen.
Syscalls implemented 40 BSD names + 10 Mach traps (+ machdep 2/3, fast traps -3/-4); stubbed on purpose:
`__mac_syscall`, `workq_kernreturn`, `fcntl(F_CHECK_LV)`. No macOS 27 compatibility is claimed.
