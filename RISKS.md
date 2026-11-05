# Risks

> Status: `PLANNED`. Likelihood and impact are planning estimates, not measurements.
> Evidence citations describe Daylight, not DaySetta.

## 1. Register

| ID | Risk | Class | Likelihood | Impact | Mitigation | Early trigger |
|---|---|---|---|---|---|---|
| R1 | Scope expands to "run macOS" | `PLANNED` | High | Critical | Tier contract in `COMPATIBILITY_SCOPE.md`; every feature needs a tier and fixture | A request says "just add launchd" |
| R2 | Clean-room contamination | `PLANNED` | Medium | Critical | Provenance log; no disassembly copied; review checklist; two-source rule for protocol code | A patch contains Apple-derived pseudocode |
| R3 | macOS 27 protocol drift is unknown | `UNKNOWN` | High | High | Pin 26.6 baseline; M6 scan and diff; no 27 runtime claim | A 27 client fails at handshake |
| R4 | macOS 27 ObjC names unrecoverable offline | `VERIFIED` gap | Certain | High | Exclude 27 from runtime claims; invest in pointer decoding only as research | A 27 compatibility request arrives |
| R5 | Substrate ABI breadth is unbounded | `PLANNED` | High | Critical | Import census; E0/E1/E2 envelope; measured shims only | Shim count grows without fixtures |
| R6 | No XPC and no `launchd` | `VERIFIED` absence | Certain | High | Broker is not an XPC manager; exclude T3; state it everywhere | A client blocks on an XPC lookup |
| R7 | Glass/material fidelity falls short | `OBSERVED` gap | Medium | High | Compositor-level material graph; structural plus perceptual tests; published floor | Material test meets hash but looks wrong |
| R8 | CPU compositor misses 60 Hz | `PLANNED` | Medium | High | Damage tracking; occlusion; CPU baseline; Vulkan path; published SLOs | p99 frame exceeds budget for two releases |
| R9 | Text and fonts are unresolved | `UNKNOWN` | High | High | Font ADR in M0; client-supplied glyph path; no Apple font redistribution | E2 needs text before the ADR closes |
| R10 | Legal or IP challenge | `PLANNED` | Low | Critical | No Apple binaries; no Apple source copying; licence review of every dependency | A release artifact contains Apple bits |
| R11 | Evidence and hardware decay | `VERIFIED` loss | High | High | Checksummed artifacts; replay corpus; no dependency on a live guest for CI | A trace cannot be reproduced |
| R12 | Bus factor and undocumented decisions | `PLANNED` | Medium | High | ADRs; `AI_AGENT_HANDOFF.md`; ownership per component; review rotation | Only one person can explain a subsystem |
| R13 | IPC security defect | `PLANNED` | Medium | Critical | Capability handles; peer checks; fuzzing; no ambient authority; audit | A handle outlives its grant |
| R14 | GPU output is non-deterministic | `PLANNED` | High | Medium | CPU is the reference; tolerance policy; per-backend manifests | Golden test flakes on one GPU |
| R15 | Multi-display and hot-plug slip | `NOT IMPLEMENTED` in Daylight | High | Medium | Defer explicitly; design display ids for topology from M2 | A demo needs two displays |
| R16 | Documentation overclaims | `PLANNED` | Medium | High | Claim registry; audit file generated from tests; forbidden wording list | A release note says "supports macOS" |
| R17 | Dependency licence mismatch | `PLANNED` | Medium | High | Licence review at M0; prefer permissive licences; record obligations | A copied reader carries a incompatible licence |
| R18 | Input backend fragmentation | `UNKNOWN` | Medium | Medium | ADR with a device matrix; backend interface from M1 | Two backends diverge in event shape |

## 2. The five risks that decide the project

### R1 — Scope

The fastest way to fail is to accept "full macOS compatibility" as the goal. Daylight
already demonstrated the pull: each new application needed another daemon, another
private framework, another service. DaySetta's answer is structural: a tier must be
named before work starts, and a feature without a fixture is not scheduled.

### R5 — Substrate breadth

`ds-rt` is the largest unbounded surface in the plan. Every additional AppKit feature
can pull in more of `libSystem`, more of the Objective-C runtime, more of libdispatch.
The mitigation is measurement: an import census per fixture, a shim only when a fixture
needs it, and an E2 envelope that can be renegotiated with evidence.

### R7 — Materials

Daylight's evidence is precise about the failure: the Dock's floor is a `CASDFLayer`,
`renderInContext:` does not draw it, and the observed fallback is a solid 0.37-gray
fill (`OBSERVED`, D-13). A later compositor `glass_draw()` added blur, tint, highlight
and border (`OBSERVED`, handoff), but the project still rates glass `PARTIAL`. DaySetta
must not repeat the mistake of claiming Apple's material from an approximation. The
material graph is a new implementation with its own acceptance threshold.

### R11 — Evidence decay

The x86 guest is not currently installed, and the Apple Silicon host was leased for a
limited window (`VERIFIED`, `DAYLIGHT_IMPLEMENTATION_STATUS.md` §10 and the handoff).
DaySetta cannot assume it can re-run any experiment. Every protocol fact must be
backed by a checksummed artifact and a replayable fixture, or it is a rumor.

### R13 — IPC security

A window server is a privileged compositor. It receives surfaces, routes input and
holds the desktop's pixels. A handle confusion bug is not a rendering bug; it is an
information disclosure or an input-injection bug. Capability handles, peer checks and
fuzzing are M1 requirements, not hardening tasks.

## 3. Risk process

| Step | When | Output |
|---|---|---|
| Identify | Any design change | New row or update to this register |
| Classify | At entry | Evidence class and tier impact |
| Score | At entry and review | Likelihood, impact, trigger |
| Mitigate | Before the milestone gate | Concrete task with an owner |
| Review | Every milestone exit | Open risks accepted, deferred or closed |
| Publish | Every release | Risk summary in the audit file |

A risk without a trigger is not actionable. A risk without an owner is not tracked.

## 4. Accepted risks

These are accepted for the first release, not solved:

* macOS 27 is catalog-only.
* T3 services are out of scope.
* GPU output may differ from CPU output within tolerance.
* Multi-display may ship after 1.0.
* Text may be limited to client-supplied glyphs in E2.
* No sandbox or code-signing equivalence.

## 5. Open questions

| Question | Class | Owner |
|---|---|---|
| Can `ds-rt` stay under a defined shim budget for E2? | `UNKNOWN` | M4 review |
| Does the material threshold predict human judgment? | `UNKNOWN` | M5 study |
| Is a Mach-O-only loader safer than a general ELF/Mach-O loader? | `UNKNOWN` | M0 ADR |
| What is the minimum Linux kernel and graphics stack? | `UNKNOWN` | M1 deployment ADR |
| Can the 27 pointer salt be decoded without Apple tooling? | `UNKNOWN` | Research backlog |
