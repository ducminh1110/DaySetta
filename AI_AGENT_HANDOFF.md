# AI Agent Handoff

Short, on purpose. Read this and then go to the docs.

## Entry points

| Question | Read |
|---|---|
| What is DaySetta? | `daysetta/README.md`, `VISION.md` |
| What does Daylight do today? | `daylight/docs/knowledge/DAYLIGHT_OVERVIEW.md` |
| What is implemented? | `daylight/docs/knowledge/DAYLIGHT_IMPLEMENTATION_STATUS.md` |
| What is broken? | `daylight/docs/knowledge/KNOWN_ISSUES.md` |
| Why is it built this way? | `daylight/docs/knowledge/ENGINEERING_DECISIONS.md` |
| What Apple API surface exists? | `daylight/docs/knowledge/apple-internals/API_INDEX.md` |
| Which MIG routines exist? | `daylight/data/apple-api/mig-routines.json` |
| Is symbol X exported? | `zcat daylight/data/apple-api/symbols/*.jsonl.gz \| grep -F x` |
| What is the plan? | `daysetta/ROADMAP.md` |
| What is next? | `knowledge-work/NEXT_TASKS.md` |

## Rules

1. Every claim gets an evidence class + a reference.
2. Never invent a constant, a line number, or an API signature.
3. Check line numbers with `grep -n` before citing them.
4. Documentation first; no source deletion, no git reset, no force-push.
5. Local commits only — there is no remote.
6. Short answers in Vietnamese to the user; English in code and docs.
7. Use sub-agents only for independent batches; verify their output.
8. Report progress concisely. Do not paste long internal reasoning.

## Hard constraints

* Do **not** reinstall/rebuild the wiped x86 guest (`~/darltest`, `~/build-darling`).
* The `~/daylight` repo is the source of truth; `~/macos-re` is read-only evidence.
* Standard macOS app/Dock binaries must never be committed (they are Apple binaries).
* The catalog (`data/apple-api/`) is an artifact — do not claim it is exhaustive.

## Number you will use most

738 MIG routines (9 subsystems) · 4 878 453 exported symbols (two releases) ·
15 implemented handlers · bridge ids `0x44594c01..ff` · server version 600.0 ·
window id `0x1f6` · window shm `0xb0` bytes · realtime block 4096 bytes / 16.666667 ms.
