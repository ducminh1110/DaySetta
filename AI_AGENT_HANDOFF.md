# AI Agent Handoff

> Snapshot: **2026-11-06T08:21Z**. This is the short entry point; the full brief is
> [`docs/HANDOFF_FULL.md`](docs/HANDOFF_FULL.md).

## What this project is

DaySetta is a Linux-native window server that re-implements the shape of Apple's
SkyLight/CGX system without Apple's kernel, `launchd`, XPC or Metal. The first runtime
target is **macOS 27.0.1 arm64e** (26A434, Mac14,3); macOS 26.6 x86_64 is the
protocol/evidence baseline. Today it is a native `ds/1` prototype, **not** a macOS
compatibility layer.

## Verified now

| Check | Result |
|---|---|
| `cmake -S . -B build ... && cmake --build build -j8` | exit 0, `-Werror` clean |
| `ctest --test-dir build --output-on-failure` | 1/1 target passed, 0.39 s |
| `./build/daysetta_tests` | **22/22 cases passed** |
| `bash ci/run.sh` | **CI PASSED** (build + ctest + docs check) |
| e2e path | broker + server + 2 windows + capture + input ring + JSON + PPM |
| 26.6 baseline | 738 MIG routines / 9 subsystems (`protocol/macos-26.6-x86_64.json`) |
| 27 arm64e cache | 4 099 catalogue images, 4 574 612 exports, **0 ObjC names** |
| 27 MIG table | **NOT SCANNED**; `tools/macos27/` and `artifacts/macos-27/` are empty |
| Wayland/X11/Vulkan | not buildable; dev headers absent on this host |

## Read in this order

1. [`docs/HANDOFF_FULL.md`](docs/HANDOFF_FULL.md) — aim, architecture, Apple evidence, next tasks.
2. [`docs/IMPLEMENTATION_STATUS.md`](docs/IMPLEMENTATION_STATUS.md) — exact status and reproduce commands.
3. [`docs/DS1_WIRE.md`](docs/DS1_WIRE.md) — byte-exact `ds/1` and broker protocol.
4. [`adr/0001-target-macos-27.md`](adr/0001-target-macos-27.md) … [`adr/0004-headless-first.md`](adr/0004-headless-first.md).
5. [`AGENTS.md`](AGENTS.md) — ownership, build and evidence rules.
6. Apple evidence: `/home/ubuntu/daylight/docs/knowledge/`, `/home/ubuntu/daylight/data/apple-api/`.

## Immediate next work

1. **macOS 27 MIG scan (critical path):** decode `LC_DYLD_CHAINED_FIXUPS`, find the 9
   SkyLight subsystems, diff ids against 26.6, emit `artifacts/macos-27/mig-routines.json`
   and `MIG_27_REPORT.md`. Never invent a routine name. If decoding is incomplete,
   publish the blocker and partial evidence, not a table.
2. **Event-ring wakeup:** `poll_event` currently sleeps 5 ms; add a wakeup descriptor or
   document the limitation. Add a `Ring` move regression test; the old move operations
   munmapped the destination (fixed in `src/event/event.cpp`).
3. **Close M1:** fuzz/property tests, 1 000 000-message loopback, fd-leak checks,
   peer-credential tests, and the missing `ds-loader`/`ds-rt` fixtures.
4. **Complete M2:** multi-client, resize/occlusion, damage, golden frames, ASan/UBSan,
   thread-count determinism, 8-hour soak.
5. **Docs:** update `docs/IMPLEMENTATION_STATUS.md` after every run; keep this file
   ≤ 60 lines (`ci/check_docs.py` enforces it).

## Rules that must not be broken

Every claim gets an evidence class + path/artifact. Never invent Apple constants,
selectors, MIG ids or line numbers. Do not call DaySetta "macOS 27 compatible". Do not
add Apple binaries, dylibs or caches. Preserve user work; local commits only, no remote.
Report to the user in Vietnamese; code and docs stay in English.
