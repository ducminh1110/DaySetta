# Contributing

How to work on DaySetta.

## 1. Ground rules

1. **Documentation first.** Code changes must be accompanied by the relevant
   document update, in the same change.
2. **Every claim carries an evidence class** — `VERIFIED`, `IMPLEMENTED`, `PARTIAL`,
   `OBSERVED`, `INFERRED`, `UNKNOWN`, `PLANNED` — plus a reference (file:line,
   commit, or artifact).
3. **Never invent a constant.** If a number or name is not in the source or in the
   catalog, it is `UNKNOWN`.
4. **Never delete source**, never reset git, never force-push.
5. **Local commits only** — there is no remote.
6. **Report in Vietnamese** to the user; write code and docs in English.

## 2. Repository layout

| Directory | Contents |
|---|---|
| `/home/ubuntu/daysetta/` | This repo — the DaySetta design and (eventually) its code |
| `/home/ubuntu/daylight/` | Daylight, the predecessor, and the source of evidence |
| `/home/ubuntu/daylight/docs/knowledge/` | Daylight knowledge tree |
| `/home/ubuntu/daylight/data/apple-api/` | Machine-readable Apple API catalogue |
| `/home/ubuntu/macos-re/` | Raw reverse-engineering evidence (read-only) |
| `/home/ubuntu/knowledge-work/` | Worklog, research notes, evidence index, next tasks |

## 3. Working with the evidence

| Question | Where to look |
|---|---|
| Does API X exist on macOS 26.6 / 27? | `data/apple-api/symbols/*.jsonl.gz` |
| What does Daylight implement today? | `docs/knowledge/DAYLIGHT_IMPLEMENTATION_STATUS.md` |
| What is broken? | `docs/knowledge/KNOWN_ISSUES.md` |
| Why was it built this way? | `docs/knowledge/ENGINEERING_DECISIONS.md` |
| How is the Apple API shaped? | `docs/knowledge/apple-internals/` |
| What sub-system does X belong to? | `data/apple-api/mig-routines.json` |
| Which MIG handlers exist? | `docs/knowledge/api/mig-interface-catalog.md` |

## 4. Build and test (when code exists)

Not applicable yet — see `ROADMAP.md` for the milestone progression.

## 5. Commits

| Rule | Detail |
|---|---|
| Granularity | Small, one subject per commit |
| Message | `area: imperative subject` (e.g. `protocol: implement CopySpacesForWindows`) |
| Body | what, why, and the evidence class of the claim |
| Frequency | Local commits as soon as a step works |

## 6. Sub-agents

Use sub-agents for **independent** document batches only. Give each one:

* an exact list of files to write,
* the facts it may use,
* the evidence-class rules,
* a line-length target.

Then verify the output yourself before committing: check line references with
`grep -n`, and check that no constant was invented.

## 7. What not to do

| Anti-pattern | Why |
|---|---|
| Claiming a private API signature from memory | It will be wrong. Read the source or mark `UNKNOWN` |
| Reporting a catalog number as "the Apple API" | The catalog is scoped — see `apple-internals/COVERAGE_REPORT.md` |
| Reinstalling the wiped x86 guest | Deliberately out of scope |
| Long AI handoff documents | The user prefers short ones; the models already know this stuff |
