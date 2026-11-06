# AI Agent Handoff

> Snapshot: **2026-11-06T08:21Z**. This is the short entry point; the full brief is
> [`docs/HANDOFF_FULL.md`](docs/HANDOFF_FULL.md).

## What this project is

DaySetta runs genuine Apple userland from the user's IPSW on Linux; its window server
hosts real SkyLight clients. Darwin, launchd/XPC and Metal are compat layers. The first runtime
target is **macOS 27.0.1 arm64e** (26A434, Mac14,3); macOS 26.6 x86_64 is the
protocol/evidence baseline. DaySetta is an IPSW-driven compatibility platform that runs
genuine Apple macOS 27 arm64e userland and apps on Linux (Wine/Darling-like, not a VM);
the native `ds/1` server is one component.

## Owner decisions (read first; override older docs)

- Authority: owner > verified evidence > reports > docs.
- Genuine Apple components from the user's IPSW are used at runtime, never committed.
- Target macOS 27 arm64e on ARMv8.0 via native execution + explicit PAC/LSE/LRCPC
  lowering/emulation (no blanket PAC stripping).
- Darling = reference/optional backend. AEA keys via `ipsw` (never hardcode). No fake/Qt/GTK/HTML macOS UI.
- 33 known doc conflicts in `artifacts/assessment/conflicts.md` are not yet applied;
  where a doc contradicts this section, this section wins.
- Progress needs real screenshots in `artifacts/screenshots/`.

## Verified now

| Check | Result |
|---|---|
| `cmake -S . -B build ... && cmake --build build -j8` | exit 0, `-Werror` clean |
| `ctest --test-dir build --output-on-failure` | 1/1 target passed, 0.39 s |
| `./build/daysetta_tests` | **23/23 cases passed** |
| `bash ci/run.sh` | **CI PASSED** (build + ctest + docs check) |
| e2e path | broker + server + 2 windows + capture + input ring + JSON + PPM |
| 26.6 baseline | 738 MIG routines / 9 subsystems (`protocol/macos-26.6-x86_64.json`) |
| 27 arm64e cache | 4 099 catalogue images, 4 574 612 exports, **0 ObjC names** |
| IPSW pipeline (27.0.1) | `tools/ipsw`, commit `e69288a`, `artifacts/ipsw-27/PIPELINE_REPORT.md` |
| ds-exec R2 | genuine Apple functions run on ARMv8.0; commit `41fb8cf`, `artifacts/exec/r2-probe.txt`, 34/34 tests |

## Read in this order

1. [`docs/HANDOFF_FULL.md`](docs/HANDOFF_FULL.md) — aim, architecture, Apple evidence, next tasks.
2. [`docs/IMPLEMENTATION_STATUS.md`](docs/IMPLEMENTATION_STATUS.md) — exact status and reproduce commands.
3. [`docs/DS1_WIRE.md`](docs/DS1_WIRE.md) — byte-exact `ds/1` and broker protocol.
4. [`adr/0001-target-macos-27.md`](adr/0001-target-macos-27.md) … [`adr/0004-headless-first.md`](adr/0004-headless-first.md).
5. [`AGENTS.md`](AGENTS.md) — ownership, build and evidence rules.
6. Apple evidence: `/home/ubuntu/daylight/docs/knowledge/`, `/home/ubuntu/daylight/data/apple-api/`.

## Immediate next work

1. **R3:** Apple dyld + `/bin/echo` via `ds-run` (in progress).
2. **R5:** first window.
3. Milestones M1/M2 and later work: see [`ROADMAP.md`](ROADMAP.md).
4. **Docs:** update `docs/IMPLEMENTATION_STATUS.md` each run; this file stays ≤ 60 lines.

## Rules that must not be broken

Every claim gets an evidence class + path/artifact. Never invent Apple constants,
selectors, MIG ids or line numbers. Do not call DaySetta "macOS 27 compatible". Never commit Apple binaries/dylibs/caches (runtime use is intended). Preserve user work; local commits only, no remote.
Report to the user in Vietnamese; code and docs stay in English.
