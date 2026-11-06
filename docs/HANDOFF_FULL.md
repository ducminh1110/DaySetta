# DaySetta — full continuation handoff

> Snapshot: **2026-11-06T08:21Z**. Audience: an AI or engineer continuing this
> repository with no access to the previous chat. Read this file after
> [`AI_AGENT_HANDOFF.md`](../AI_AGENT_HANDOFF.md).
>
> Evidence classes used here: `VERIFIED`, `IMPLEMENTED`, `PARTIAL`, `OBSERVED`,
> `INFERRED`, `UNKNOWN`, `PLANNED`. A claim without a path or named artifact is a
> bug in this document.

## 1. Mission

DaySetta is a **Linux-native window server** that re-implements the *shape* of Apple's
SkyLight/CGX window system without Apple's kernel, `launchd`, XPC or Metal stack.

The project has two profiles over one core:

| Profile | Client | Mechanism | State |
|---|---|---|---|
| Native (`T0`/`T1`) | Linux ELF client, DaySetta SDK | `ds/1` over Unix `SOCK_SEQPACKET` | `IMPLEMENTED` prototype path |
| Compatibility (`T2`) | Unmodified macOS 26.6 x86_64 or macOS 27 arm64e client | `ds-loader` + `ds-rt` + SkyLight dialect | `PLANNED`, research target, narrow |

`T3` (macOS system services, XPC, `launchd`) is out of scope. See
[`COMPATIBILITY_SCOPE.md`](../COMPATIBILITY_SCOPE.md).

**First runtime target:** macOS 27.0.1 arm64e, build **26A434**, `Mac14,3`.
**Protocol/evidence baseline:** macOS 26.6 x86_64, 738 MIG routines in 9 subsystems.
DaySetta is **not** "macOS 27 compatible" today. It has no Mach-O loader, no ObjC
runtime and no SkyLight dialect.

The immediate engineering goal is to close the native M1/M2 prototype, then build the
macOS 27 evidence base needed for M3. The macOS 27 MIG inventory is the critical path.

## 2. Non-negotiable rules

1. Every technical claim needs an evidence class and a path, artifact or command.
2. Never invent an Apple constant, selector, MIG id, MIG routine name, line number or
   private API signature. If it was not recovered, write `UNKNOWN`.
3. Do not call the result "macOS 27 compatible", "macOS compatible" or "a macOS
   replacement".
4. Do not add Apple binaries, dylibs, dyld caches or extracted SDK headers to this
   repository. Evidence stays under `/home/ubuntu/macos-re/` and
   `/home/ubuntu/daylight/`.
5. Preserve user work. Do not delete or reset a working tree without an explicit
   request. Local commits only; there is no remote configured.
6. `include/daysetta/*.hpp` and `CMakeLists.txt` are interface files. Change them only
   when the interface itself is the task, and then update `docs/DS1_WIRE.md` too.
7. `docs/DS1_WIRE.md` is authoritative for bytes; the headers are authoritative for
   C++ signatures. Change one without the other and the tree is inconsistent.
8. After every real test run, update [`docs/IMPLEMENTATION_STATUS.md`](IMPLEMENTATION_STATUS.md)
   and the status table in [`README.md`](../README.md). Do not preserve old passing or
   old failing claims after the state changes.
9. Keep [`AI_AGENT_HANDOFF.md`](../AI_AGENT_HANDOFF.md) at **60 lines or fewer**;
   `ci/check_docs.py` enforces this.

## 3. Repository map

| Path | What it is |
|---|---|
| `include/daysetta/` | Public/interface headers: result, ipc, protocol, event, scene, render, sdk, daemon |
| `src/ipc/` | Unix `SOCK_SEQPACKET` transport, `SCM_RIGHTS`, peer credentials, dir helpers |
| `src/protocol/` | `DSM1` header, typed payload reader/writer, request/reply/error helpers |
| `src/event/` | 40-byte input events, shared-memory ring, producer/consumer |
| `src/scene/` | Windows, clients, surfaces, ordering, hit test, JSON dump |
| `src/render/` | CPU BGRA compositor, damage rendering, SHA-256, PPM/PAM/BGRA writers |
| `src/sdk/` | Broker protocol (`DSK1`), native SDK client |
| `src/broker/` | Per-user broker: registry, tokens, lookup, same-uid enforcement |
| `src/server/` | Server event loop, dispatch, capture, input, frame output, broker registration |
| `src/session/` | Optional broker+server supervisor with pidfile and `--stop` |
| `src/ctl/` | `daysetta-ctl`: list/dump/ping/status/inject/capture |
| `tests/` | One binary, 22 cases: broker, compositor, e2e, scene, SDK |
| `docs/DS1_WIRE.md` | Byte-exact `ds/1` and broker protocol |
| `docs/IMPLEMENTATION_STATUS.md` | Current verified state and reproduce commands |
| `adr/` | ADR-0001 target, ADR-0002 C++20, ADR-0003 transport, ADR-0004 headless |
| `ci/` | `check_docs.py`, `run.sh`; `ci/run.sh` is the local CI entry point |
| `AGENTS.md` | Build/test/ownership rules |
| `ROADMAP.md` | M0–M6 gates; use it to decide whether a milestone is closed |

## 4. Verified state at the snapshot

All commands below were executed in `/home/ubuntu/daysetta` at **2026-11-06T08:21Z**.
Artifacts: `build/Testing/Temporary/LastTest.log`, `/tmp/opencode/final-ci.log`.

| Check | Command | Result | Class |
|---|---|---|---|
| Configure | `cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo` | exit 0 | `VERIFIED` |
| Build | `cmake --build build -j8` | exit 0, `-Wall -Wextra -Wpedantic -Werror` clean | `VERIFIED` |
| CTest | `ctest --test-dir build --output-on-failure` | 1/1 target passed, 0.39 s | `VERIFIED` |
| Direct suite | `./build/daysetta_tests` | **22/22 cases passed** | `VERIFIED` |
| Docs | `python3 ci/check_docs.py` | 0 errors, 23 Markdown files, 58 path refs | `VERIFIED` |
| Local CI | `bash ci/run.sh` | **CI PASSED** | `VERIFIED` |

The end-to-end case `e2e_two_windows_capture_and_input` proves this path:

1. Broker starts, publishes `<runtime>/broker.sock`, registers the server.
2. SDK looks up `org.daysetta.server`, connects and completes `hello` with the
   broker token.
3. Two windows are created at different levels, surfaces are committed through
   `memfd` + `SCM_RIGHTS`, and made visible.
4. Capture at 320×240 returns the expected blue/red/black pixels and is deterministic
   across two captures (SHA-256 equality, not a hardcoded hash).
5. Input at (60,60) targets the top window; the event is delivered through the shared
   ring; `scene_dump` JSON contains both titles; `ping` round-trips.
6. PPM frames are written by the server; teardown removes the children and sockets.

What this does **not** prove: multi-client contention, resize/occlusion, 1 000 000
messages, fd leaks under churn, fuzz, ASan/UBSan, an 8-hour soak, thread-count
determinism, or macOS 27 runtime behaviour. Those are M1/M2 exit criteria in
[`ROADMAP.md`](../ROADMAP.md) §3–§4, not completed work.

## 5. Implemented components

| Component | State | Evidence | Known gaps |
|---|---|---|---|
| `ds-ipc` | `IMPLEMENTED` | `src/ipc/ipc.cpp`; `Channel`, `Fd`, `SO_PEERCRED`, `SCM_RIGHTS`, `kMaxMessage=1 MiB` | No isolated unit/fuzz test; broker/server tested through e2e |
| `ds-protocol` | `IMPLEMENTED` | `src/protocol/protocol.cpp`; 32-byte `DSM1`, bounds-checked reader/writer | No malformed-header unit test yet |
| `ds-event` | `IMPLEMENTED` | `src/event/event.cpp`; 40-byte event, 32-byte ring header, 256 slots | Ring has no wakeup descriptor; move regression test missing |
| `ds-scene` | `IMPLEMENTED` / `PARTIAL` | `src/scene/scene.cpp`; tests `scene_*` | Single server lock; no spaces, notes or damage scheduling |
| `ds-render` | `IMPLEMENTED` / `PARTIAL` | `src/render/compositor.cpp`, `sha256.cpp`, `output.cpp`; `compositor_*` tests | CPU single-threaded; no materials; no golden manifest |
| `ds-sdk` | `IMPLEMENTED` | `src/sdk/sdk.cpp`; `sdk_fake_server`, `sdk_hello_denied`; e2e | Event wait is a 5 ms poll; no reconnect/pooling |
| `daysetta-broker` | `IMPLEMENTED` | `src/broker/broker.cpp`; `broker_live_registry` | Same-uid only; no audit log/revocation yet |
| `daysetta-server` | `IMPLEMENTED` | `src/server/server.cpp`; e2e | One poll loop; no frame pacing budget; resize/occlusion untested |
| `daysetta-session` | `IMPLEMENTED`, unexercised | `src/session/main.cpp` | No test; starts/stops children |
| `daysetta-ctl` | `IMPLEMENTED` | `src/ctl/main.cpp`; list/dump/ping/status/inject/capture | `status` is hello fields + scene JSON, not a separate op |
| Docs/ADRs/CI | `IMPLEMENTED` | `adr/`, `docs/CI.md`, `ci/`, `.github/workflows/ci.yml` | M0 review not signed; evidence index partial |

The 22 test cases, grouped by file:

* `tests/test_broker.cpp`: protocol round-trip, malformed corpus, live registry.
* `tests/test_compositor.cpp`: clear/opaque pixel, exact alpha, negative/oversized
  clipping, RGBA swap, damage-only changes, SHA-256/checksum vectors, PPM/PAM/BGRA,
  bad sizes, deterministic scene hash.
* `tests/test_e2e.cpp`: the full path in §4.
* `tests/test_scene.cpp`: monotonic ids, ordering, visibility/hit test, raise/lower,
  invalid geometry/title/surface, opacity clamp/JSON escape, counters/client removal.
* `tests/test_sdk.cpp`: fake server, every SDK call, commit fd contents, hello denial.

## 6. Wire contracts

Authoritative detail: [`docs/DS1_WIRE.md`](DS1_WIRE.md). Summary of invariants:

* Transport: Unix `SOCK_SEQPACKET`; one send is one datagram; 32-byte header plus
  payload; `SCM_RIGHTS` fds arrive with the same datagram.
* Header: magic `DSM1` (`0x314d5344`), size, op, flags (`reply=1`, `error=2`,
  `one_way=4`, `handle_bearing=8`), sequence (`u64`), handle count (`u16`),
  reserved `u16 = 0`, payload length (`u32`).
* `string` = `u32` length + bytes, no NUL. `rect` = four `i32` x/y/w/h. All integers
  little-endian.
* Ops: hello `0x0001`, create/destroy/frame/visible/order/opacity/title
  `0x0010`–`0x0016`, commit `0x0020`, subscribe/ack `0x0030`–`0x0031`, scene dump
  `0x0040`, inject `0x0041`, capture `0x0042`, ping `0x00f0`, status `0x00f1`.
* Broker protocol: magic `DSK1` (`0x314b5344`), version 1, ops register/lookup/
  unregister/list/ping; same-uid only; lookup returns path + 64-bit token; the token
  is presented in `hello`; list never returns tokens.
* Event ring: 32-byte header (`magic DSEV`, version, capacity, head, tail, dropped)
  followed by `capacity` 40-byte records. Server is producer, client is consumer.
  Capacity 256. There is currently **no wakeup fd**; `src/sdk/sdk.cpp` polls the ring
  every 5 ms after a non-blocking socket check.

## 7. Apple evidence map (internal docs)

The evidence lives in `/home/ubuntu/daylight` (read-only for this project). Do not
copy binaries into `daysetta`.

| Path | What it answers |
|---|---|
| `daylight/docs/knowledge/DAYLIGHT_OVERVIEW.md` | What the predecessor Daylight does and why DaySetta exists |
| `daylight/docs/knowledge/DAYLIGHT_IMPLEMENTATION_STATUS.md` | 15 modelled handlers, traces, known limits |
| `daylight/docs/knowledge/KNOWN_ISSUES.md` | KI-09 (27 ObjC names), KI-10 (27 protocol ids) and other blockers |
| `daylight/docs/knowledge/TECHNICAL_DISCOVERIES.md` | D-02 handshake, D-13 glass approximation, D-20/D-21 pointer encoding |
| `daylight/docs/knowledge/api/mig-interface-catalog.md` | MIG routine catalogue and 9-subsystem structure |
| `daylight/docs/knowledge/api/ipc-interface-catalog.md` | Private IPC/bridge message ids |
| `daylight/docs/knowledge/apple-internals/API_INDEX.md` | Entry point to the API surface index |
| `daylight/docs/knowledge/apple-internals/WINDOWSERVER_APIS.md` | SkyLight/CGX window-server API surface |
| `daylight/docs/knowledge/apple-internals/GRAPHICS_APIS.md` | CoreGraphics/QuartzCore surface |
| `daylight/docs/knowledge/apple-internals/OBJC_RUNTIME.md` | ObjC runtime and metadata constraints |
| `daylight/docs/knowledge/apple-internals/COVERAGE_REPORT.md` | What the catalogue covers and what it does not |
| `daylight/docs/knowledge/apple-internals/SYMBOL_INVENTORY_METHOD.md` | How exports/ObjC/symbols were recovered |
| `daylight/docs/knowledge/internals/skylight.md` | SkyLight architecture notes |
| `daylight/docs/knowledge/internals/windowserver.md` | Window-server behaviour notes |
| `daylight/docs/knowledge/internals/mig.md` | MIG wire/routine-descriptor background |
| `daylight/docs/knowledge/internals/mach-o.md` | Mach-O layout notes used by the readers |
| `daylight/docs/knowledge/internals/mach-ipc.md` | Mach IPC evidence (context only; DaySetta is not Mach) |
| `daylight/docs/knowledge/internals/objective-c-runtime.md` | ObjC runtime evidence |
| `daylight/data/apple-api/mig-routines.json` | Machine-readable MIG inventory (26.6) |
| `daylight/data/apple-api/coverage.json` | Authoritative catalogue counts and gaps |
| `daylight/data/apple-api/symbols/*.jsonl.gz` | Export lookup, one JSONL per image |
| `daylight/protocol/macos-26.6-x86_64.json` | 738-routine protocol baseline, 9 subsystems |
| `daylight/tools/apicatalog/machoinfo.py` | Reusable Mach-O/split-cache reader |
| `daylight/tools/mig_scan.py` | The 26.6 x86_64 MIG scanner (reference algorithm) |

### 26.6 MIG baseline (parsed from the JSON)

| Subsystem | Routines | Id range | Contiguity |
|---|---:|---|---|
| `CGYSpinControl` | 2 | 1–2 | contiguous |
| `CGYCaptureReply` | 2 | 1–2 | contiguous |
| `CGYEventTap` | 2 | 1–2 | contiguous |
| `CGXRendezvous` | 41 | 29000–29040 | contiguous |
| `CGXServices` | 575 | 30000–30636 | **gaps** |
| `CGXWindowServer` | 72 | 32000–32073 | **gaps** |
| `ServicesRelocated` | 29 | 33000–33028 | contiguous |
| `ServerRelocated` | 10 | 34000–34009 | contiguous |
| `CGXHMD` | 5 | 35000–35004 | contiguous |

Total: **738 routines, 9 subsystems** (`VERIFIED` by parsing
`daylight/protocol/macos-26.6-x86_64.json`). The gap counts matter: do not assume the
27 table is dense.

## 8. macOS 27 evidence and blockers

### 8.1 What is present

| Artifact | State |
|---|---|
| `/home/ubuntu/macos-re/cache-arm64e/` | macOS 27.0.1 26A434 arm64e split cache; 85 files, 6.6 GB |
| `SplitCache` load of that cache | **4 088 images** in 0.5 s; SkyLight at vmaddr `0x187093000`, UUID `8A3B348E4637368592D06CBC2F36A234` |
| `daylight/data/apple-api/coverage.json` | 4 099 images, **4 574 612** exported symbols, **0** ObjC class/selector names, 3 137 ObjC metadata images |
| `/home/ubuntu/macos-re/dyldinfo-27/` | `all_exports.txt` (391 MB), `all_load_commands.txt`, `all_objc.txt`, `all_uuid.txt`, `all_linked_dylibs.txt` |
| `/home/ubuntu/macos-re/re/` | SkyLight disassembly, CGS structure notes |
| `/home/ubuntu/macos-re/traces/` | Protocol traces (evidence, not a 27 table) |
| `/home/ubuntu/macos-re/ipsw/UniversalMac_27.0.1_26A434_Restore.ipsw` | 26 637 307 067 bytes; SHA-256 `2f016638293c3e641b8b25391a76fbc16563b3711915a5551cf8aa0f5598a5c1` |
| `/home/ubuntu/macos-re/ipsw/manifest/BuildManifest.plist` | `ProductVersion 27.0.1`, `ProductBuildVersion 26A434`, `Mac14,3` supported |

The 4 088 vs 4 099 image-count difference is **`UNKNOWN`** and unreconciled; report both
with their sources rather than choosing one silently.

### 8.2 What is missing

* **macOS 27 MIG table: `UNKNOWN`.** `tools/macos27/` and `artifacts/macos-27/` exist
  but are **empty**. No subsystems, ids, names or delta were produced.
* The arm64e cache stores `__DATA_CONST` pointers as chained-fixup payloads, not plain
  VM addresses. Earlier scans saw values such as `0x100000677bd6d8`; a scanner that
  treats them as addresses finds zero valid `mig_subsystem` descriptors.
* `LC_DYLD_CHAINED_FIXUPS` parsing is not implemented in `machoinfo.py`; it exposes the
  blob offset/size only. A decoder must handle at least
  `DYLD_CHAINED_PTR_ARM64E` (1), `DYLD_CHAINED_PTR_64` (2),
  `DYLD_CHAINED_PTR_64_OFFSET` (6) and `DYLD_CHAINED_PTR_ARM64E_OFFSET` (7).
* **ObjC names: `UNKNOWN`.** `__objc_classname`/`__objc_methname` have size 0 for the
  inspected 27 images (D-20). D-21: classlist entries were partially resolved with
  `cache_base + (value & 0xFFFFFFFF)` (cache base `0x180000000`), but `isa`,
  `superclass` and `ro` use another encoding. The salt is only partly understood.
* **No 27 runtime validation.** No Mach-O loader, no ObjC runtime, no arm64e execution
  probe. The Linux host reports `asimd` but not `pauth`; PAC behaviour is unvalidated.

Do not "fix" any of this by inventing ids or names. A partial decoder plus a precise
blocker report is acceptable; a fabricated table is not.

## 9. Next work packages, in priority order

### WP1 — macOS 27 MIG scan (critical path, blocks M3)

**Deliverables**

* `tools/macos27/scan_mig.py` (stdlib + `machoinfo.SplitCache`; no copies of the
  900-line reader).
* `artifacts/macos-27/mig-routines.json` (< 2 MB).
* `artifacts/macos-27/MIG_27_REPORT.md`.

**Method**

1. Load the split cache and locate `/System/Library/PrivateFrameworks/SkyLight.framework/Versions/A/SkyLight`
   (vmaddr `0x187093000`, UUID `8A3B348E4637368592D06CBC2F36A234`).
2. Parse `LC_DYLD_CHAINED_FIXUPS`: header, starts-in-image, starts-in-segment,
   per-segment page starts and chain words. Decode rebases and binds; resolve bind
   symbols through the imports table. Mask PAC bits and validate canonical form.
3. Scan data sections for 32-byte `mig_subsystem` headers followed by `N` 40-byte
   routine descriptors: server/stub pointers in `__TEXT`, `0 < end-start <= 2000`,
   `maxsize` in range, `reserved == 0`, `argc <= 64`, `descr_count <= 64`.
4. Expect 9 subsystems. Compare every range/count with the 26.6 table in §7.
5. Name a routine only when the id exists in the 26.6 inventory and the subsystem
   range/count supports the match; mark `name_source: "26.6-id-match"` and
   `confidence: "inferred"`. Otherwise `"?"`, `name_source: "unknown"`.

**Acceptance**

* Two runs produce byte-identical JSON; `python3 -m json.tool` accepts it.
* At least 20 routine descriptors are hand-checked with raw 40-byte dumps and resolved
  pointers inside `__TEXT`.
* Counts and ranges are reconciled against 738; every difference is listed.
* If the fixup decoder cannot be completed: write the exact blocker, the partial
  result and the failed validation; do not publish a table.

### WP2 — event-ring wakeup and regression tests

Current `poll_event` waits in 5 ms sleeps (`src/sdk/sdk.cpp`) because the server writes
shared memory but sends nothing on the socket. Add a wakeup mechanism (for example use
a reserved header word for an `eventfd`, or a documented one-way notification) and
update `docs/DS1_WIRE.md`, `include/daysetta/event.hpp` and both sides. Add a regression
test for `event::Ring` move construction/assignment: the move must transfer ownership
**without munmapping** the mapping. This bug was fixed in `src/event/event.cpp` but has
no dedicated test.

Acceptance: an event injected while the client is already blocked is observed within
the stated budget, with no busy loop; malformed/unsolicited socket data is rejected.

### WP3 — close M1

M1 is not closed by the 22 cases. Add:

* Unit/property/fuzz tests for `DSM1` headers, reader/writer bounds, op/flag/sequence
  validation, and malformed broker payloads.
* 1 000 000-message loopback with fd-count, RSS and leak checks.
* Peer-credential tests (same-uid accept, cross-uid reject); document any test that
  needs privileges and genuinely skip it.
* `ds-loader`/`ds-rt` fixtures per `ROADMAP.md` §3; neither exists yet.

Acceptance: `ROADMAP.md` §3 acceptance and exit criteria, including the fuzz corpus and
security review. Do not claim M1 because the e2e case passes.

### WP4 — complete M2

Add multi-client scene tests, resize and occlusion, surface sequence gaps, damage
scheduling, golden frames, ASan/UBSan, 1/2/8-thread determinism (the compositor is
currently single-threaded) and an 8-hour churn soak. Wayland/X11/X11/Vulkan remain
blocked on missing development headers (see ADR-0004); headless PPM/PAM/BGRA is the
supported output today.

### WP5 — macOS 27 ObjC names

Continue from D-20/D-21 and KI-09: decode `isa`, `superclass` and `ro`, recover class
and selector names, and use exported class symbols as a fallback. Acceptance: counts
and names with confidence, cross-checked against the 26.6 inventory; no guessed
selectors.

### WP6 — M3 SkyLight dialect

Only after WP1 and WP5: descriptor/NDR codec, 9-subsystem routing, the 15
Daylight-modelled routines, session notes `0x390`/`0x391`/`0x33b`, trace replay with
normalized ports/pids/timestamps, 738-routine coverage, and no canned replies in a
production path. See `ROADMAP.md` §5 and `PROTOCOL_STRATEGY.md`.

### WP7 — hardening and later backends

Threat model, fd lifetime/handle-table review, broker audit/revocation, materials,
Wayland/X11 when headers are installable, Vulkan later. Do not start Vulkan before the
CPU reference path is stable.

## 10. Known traps and recent fixes

* **Ring move bug (fixed, regression missing).** `event::Ring`'s move operations used
  to call `other.reset()`, which munmapped the destination's mapping. The e2e test
  crashed with `SIGSEGV` at `ring header + 8`. Move now nulls the source fields without
  unmapping (`src/event/event.cpp`). Never reintroduce a reset that owns the mapping.
* **Listener accept.** `Server::accept_clients()` accepts exactly one connection per
  `poll()` wakeup because the listener is blocking. Do not restore an unbounded
  `for(;;) accept()` without making the listener non-blocking.
* **Event wait is a workaround.** The SDK checks the socket non-blocking, sleeps 5 ms,
  then rechecks the ring. This is acceptable for the current tests, not a final design.
* **Frame assertion.** A 320×240 PPM is exactly 15 + 320·240·3 bytes; use `>=`, not
  `>`.
* **Orphaned test daemons.** If a test crashes, kill
  `daysetta-broker`/`daysetta-server` with `--runtime-dir /tmp/daysetta-e2e-*` and
  remove the temp dirs before rerunning. A live daemon can hold the test pipe open and
  look like a hang.
* **Stale status documents.** `docs/IMPLEMENTATION_STATUS.md` and `README.md` were
  written when the broker still failed to link. This handoff and the current status
  snapshot supersede that failure. Update them after every run.
* **Broker link.** `CMakeLists.txt` globs `src/broker/*.cpp`; both `main.cpp` and
  `broker.cpp` are compiled into `daysetta-broker`. Do not add a duplicate definition
  or include `broker.cpp` from `main.cpp`.
* **Same-uid only.** The broker rejects other uids. Cross-uid tests need a second
  account or privileges; skip with an explicit note if unavailable.
* **No external dependencies.** M1/M2 must remain C++20 + CMake + pthread + libc. Do
  not add a package manager, Rust, Go, SDL, X11 or Vulkan dependency to make a test
  pass.

## 11. Host environment

| Fact | Value |
|---|---|
| OS / arch | Ubuntu 26.04.1 LTS, `aarch64` |
| CPU / RAM | 8 cores, ~15 GiB |
| Compilers | GCC 15.2.0, Clang 21.1.8 |
| Build | CMake 4.2.3, GNU Make 4.4.1; no Ninja |
| Missing toolchains | Rust, Go, .NET |
| Missing dev packages | X11, Wayland client, Vulkan, SDL2, libpng, zlib (`pkg-config` reports missing) |
| Emulation | no `qemu-aarch64`; `/proc/cpuinfo` has `asimd` but no `pauth` |
| Disk | ~47 GB free after the 25 GB IPSW; `cache-arm64e` is 6.6 GB, keep it |
| Runtime target host | a `Mac14,3` on macOS 27.0.1 is available for validation; ask the user for access details rather than guessing credentials |

Do not reinstall or rebuild the wiped x86 guest (`~/darltest`, `~/build-darling`). The
macOS 26.6 x86_64 evidence is already catalogued; the 27 arm64e cache is the active
target.

## 12. Git and workspace

* `/home/ubuntu/daysetta` is a local git repository. The design-docs baseline is
  `4c873ea`; the implementation, tests and this handoff were committed locally after
  the green run. No remote is configured; do not push. Use `git log -1` for the exact
  snapshot commit.
* `/home/ubuntu/daylight` has uncommitted audit/documentation changes; do not reset or
  delete them.
* Build outputs (`build/`, `build-*/`) are ignored. `*.ipsw` is ignored.
* The macOS 27 IPSW is verified but is not part of the repo. Do not add it.

Useful reset for test leftovers (does not touch source):

```sh
pkill -f 'daysetta-(broker|server) --runtime-dir /tmp/daysetta-' || true
rm -rf /tmp/daysetta-e2e-* /tmp/daysetta-test-*
```

## 13. Reproduce and verify

```sh
cd /home/ubuntu/daysetta
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j8
./build/daysetta_tests                 # expect 22/22
ctest --test-dir build --output-on-failure
python3 ci/check_docs.py               # expect 0 errors
bash ci/run.sh                         # expect CI PASSED
```

If a count differs, record the exact command, output and timestamp in
`docs/IMPLEMENTATION_STATUS.md`; do not edit this handoff to claim a pass you did not
see.

## 14. First moves for the next agent

1. Read `AI_AGENT_HANDOFF.md`, this file, `docs/IMPLEMENTATION_STATUS.md`,
   `docs/DS1_WIRE.md`, `ROADMAP.md` §3–§5 and `AGENTS.md`.
2. Run the five commands in §13 and confirm 22/22 and CI PASSED before changing code.
3. Inspect the empty `tools/macos27/` and `artifacts/macos-27/`; start WP1 unless the
   user redirects.
4. Keep the native prototype green while working on macOS 27 evidence. Do not couple
   Apple evidence code into `src/` unless it is a runtime feature.
5. Update the status docs and commit locally when a work package has passing evidence.
6. Report in Vietnamese to the user; keep code, comments, docs and commit messages in
   English. Every claim gets an evidence class and a path/artifact.
