# ADR-0001 — Target macOS 27 arm64e, keep macOS 26.6 as the protocol baseline

| Field | Value |
|---|---|
| Status | Accepted |
| Date | 2026-11-06 |
| Supersedes | The implicit macOS 26.6 x86_64 runtime target in `README.md` |
| Related | `adr/0002-language-cpp20.md`, `COMPATIBILITY_SCOPE.md`, `docs/IMPLEMENTATION_STATUS.md` |

## Context

The first runtime target is **macOS 27 arm64e**. macOS 26.6 x86_64 remains the
protocol and evidence baseline because it is the only release with a complete
observed window-server inventory.

| Fact | Class | Evidence |
|---|---|---|
| macOS 27.0.1 arm64e has 4 574 612 exported symbols and 4 099 catalogued images | `VERIFIED` | `data/apple-api/coverage.json:36-42` |
| macOS 26.6 x86_64 has 303 841 exported symbols, 24 473 ObjC class names and 344 184 selector names | `VERIFIED` | `data/apple-api/coverage.json:28-34` |
| The combined catalogue holds 4 878 453 exported symbols and 738 MIG routines | `VERIFIED` | `data/apple-api/coverage.json:45-50` |
| The 738-routine SkyLight/CGX inventory describes macOS 26.6, not macOS 27 | `VERIFIED` | `protocol/macos-26.6-x86_64.json`, `PROTOCOL_STRATEGY.md:100` |
| Daylight implemented 15 of the 738 routines and answered the rest from recorded replies | `IMPLEMENTED` | `docs/knowledge/DAYLIGHT_IMPLEMENTATION_STATUS.md` §1, `docs/knowledge/KNOWN_ISSUES.md` KI-03 |
| macOS 27 `__objc_classname` and `__objc_methname` sections have size 0 | `VERIFIED` | `docs/knowledge/TECHNICAL_DISCOVERIES.md:266-279` (D-20) |
| The macOS 27 cache pointer salt is decoded only for class-list entries; `isa`, `superclass` and `ro` use an undecoded encoding | `OBSERVED` | `docs/knowledge/TECHNICAL_DISCOVERIES.md:281-297` (D-21) |
| macOS 27 ObjC names are therefore unrecoverable offline; the protocol-id delta is also unknown | `UNKNOWN` | `docs/knowledge/KNOWN_ISSUES.md:85-103` (KI-09, KI-10) |

Native transport and codec code exists (`IMPLEMENTED`, `src/ipc/ipc.cpp:62`,
`src/protocol/protocol.cpp:35`). No Mach-O loader, ObjC runtime or macOS 27 runtime
path exists in `src/` or `include/` (`UNKNOWN`; there is nothing to exercise). The
target is a direction for compatibility claims and test fixtures, not a statement that
a macOS 27 process runs.

## Decision

1. **Target release.** Name macOS 27 arm64e as the first runtime target in plans,
   fixtures and compatibility reports.
2. **Protocol baseline.** Keep macOS 26.6 x86_64 as the only protocol/evidence
   baseline until a macOS 27 MIG inventory exists. Do not extrapolate ids, selectors or
   class layouts from 26.6 to 27.
3. **Claim boundary.** Runtime claims are limited to the protocol and catalogue subset
   that can be named and tested. No document may say "macOS 27 compatible", "runs
   macOS 27 apps" or equivalent. The allowed phrasing is "macOS 27 target, protocol
   evidence from 26.6".
4. **Blocker ownership.** Name recovery is a prerequisite for a macOS 27 class-level
   claim. It stays `UNKNOWN` until `__objc_classname`/`__objc_methname` or the salted
   `isa`/`superclass`/`ro` encoding is decoded and reproduced.
5. **Native first.** The native `ds/1` path does not depend on ObjC names, SkyLight ids
   or Apple metadata. It can progress while the macOS 27 blocker is open.

## Consequences

* `docs/IMPLEMENTATION_STATUS.md` separates **prototype** (native Linux `ds/1`) from
  **target** (macOS 27 arm64e substrate). A prototype row cannot satisfy a target row.
* `COMPATIBILITY_SCOPE.md` stays authoritative for the release matrix; it already says
  macOS 27 is catalogue-only with no runtime claim (`COMPATIBILITY_SCOPE.md:26-36`).
* Any test that requires macOS 27 names or protocol ids is marked blocked, not skipped
  silently. A blocked test is not a passing test.
* CI never downloads macOS 27 artifacts. Catalogue validation is limited to a local
  `artifacts/macos-27/mig-routines.json` when one is present.

## Revisit criteria

Revisit this ADR when all of the following exist:

1. A decoded macOS 27 class/selector-name source, or an exported-symbol fallback that
   is sufficient for the claimed class surface.
2. A macOS 27 MIG scan diffed against the 26.6 id space.
3. At least one macOS 27 fixture that runs under `ds-loader`/`ds-rt` without Apple
   `dyld` and without Darling.

Until then, this ADR is the ceiling on macOS 27 claims.
