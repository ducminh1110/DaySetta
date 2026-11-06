# DaySetta doc conflict register (read-only audit, 2026-11-06)
Authority: owner decisions > verified evidence > reports > docs. All file:line verified with grep -n / sed -n today.
Verdicts: OUT=OUTDATED, INC=INCORRECT, INCPL=INCOMPLETE, UNRES=GENUINELY UNRESOLVED (owner).
Prior audit (artifacts/assessment/daysetta.md) items re-verified: its line numbers for README:70/72/73, VISION:37/55/56, ARCH:171,
COMPONENT_MAP:126, ROADMAP:197, AI_AGENT_HANDOFF:11-12, COMPAT_SCOPE:15-19, PROTOCOL_STRATEGY:214-221 all still hold. VISION:55 is NOT clean-room (it is a table row; clean-room is VISION:55 "Clean-room" principle row, ok). Its "22 cases" nit: IMPLEMENTATION_STATUS.md:60 still says "The 22 cases" vs 23 (minor).

## A. Vision / identity
C-01 | README.md:3-5 | "clean-room macOS window-server and runtime layer that does **not** depend on Apple's `dyld`, does **not** run inside Darling" | vision | Product is an IPSW-driven userland compat platform; window server is one component; Darling is an open option. | INC+OUT
  Propose: "DaySetta continues Daylight as an IPSW-driven macOS userland compatibility platform on Linux (Wine/Darling-like, not a VM). It extracts real Apple components from the user's IPSW and runs them; the ds/1 window server is one component."
C-02 | README.md:3 | "planned successor to **Daylight**" | vision | Owner: continuation; Daylight foundations inherited, not rebuilt. | OUT
  Propose: "DaySetta is the continuation of Daylight; Daylight's evidence, Darwin-compat work and protocol knowledge are inherited."
C-03 | VISION.md:7-8 | "runs on Linux without Apple's loader and without Darling: small enough to audit" | vision | Contradicts runtime-Apple-binaries and Darling-option decisions; "small to audit" is not the goal. | INC
  Propose: "Run real macOS userland (extracted from the user's IPSW) on Linux with a Darwin/Mach compatibility layer and a SkyLight-compatible DaySetta window server, shown on a Linux display."
C-04 | VISION.md:118-120 (Horizon), VISION.md:63-77 (V1-V8) | "a narrow substrate for macOS 26.6 x86_64 clients"; V4 "without Apple dyld" | scope-tier | Success criteria stop at fixtures + custom ds-rt; omit real frameworks/apps/desktop. | INCPL
  Propose: "Success ladder: IPSW->component store; real Apple binary runs; real frameworks load; real GUI client talks SkyLight to DaySetta server; Linux display; real apps/desktop. Existing V1-V8 become server-component gates."
C-05 | VISION.md:103 | `Daylight deployment (Darling + dyld + bridge) -.-> "retired"` | other | Darling + Apple dyld port are Daylight's working foundation; not retired. | INC
  Propose: "Daylight's Darling+dyld-1378 path is the working baseline; whether Darwin layer stays Darling or becomes DaySetta-owned is an open owner decision."

## B. Runtime Apple binaries (core conflict)
C-06 | README.md:72-73 | "Own the loading path... `ds-loader` loads ELF and Mach-O without `dyld`." / "**No Darling.** ... no dylib extraction for execution." | runtime-Apple-binaries | Real dylibs/dyld cache from the IPSW must be extracted and executed. | INC
  Propose: "Tenet superseded: real Apple dylibs and shared caches extracted from the user's IPSW are runtime inputs (stored locally, never committed). Loader choice (Apple dyld port, ds-loader, Darling) is open."
C-07 | VISION.md:37,56 | "loads clients with `ds-loader`, never Apple `dyld`"; "Linking `dyld` or reusing cache dylibs as runtime inputs" (anti-pattern) | runtime-Apple-binaries | Anti-pattern is now the plan. | INC
  Propose: "Reusing extracted cache dylibs as runtime inputs is the design; the anti-pattern is redistributing them or committing them to the repo."
C-08 | ARCHITECTURE.md:171,203-204 | "no Apple `dyld`, no dyld shared cache at runtime, and no Darling library"; non-goals list dyld cache "as execution inputs" | runtime-Apple-binaries | Same. Also line 11-12 (substrate = ds-loader+ds-rt, x86_64 26.6). | INC
  Propose: "Substrate: Mach-O loaded against the real dyld shared cache from the component store; Darwin/Mach compat layer beneath. Superseded: ds-rt-as-libSystem-clone plan (kept in ADR history)."
C-09 | COMPONENT_MAP.md:31-32,91-92,126,128 | "`ds-rt` libSystem-shaped ABI"; "Runtime never links Darling or Apple `dyld`"; "not a full libSystem clone" | runtime-Apple-binaries | Real libSystem/CF/Foundation come from IPSW; ds-rt reduces to Darwin shim (syscalls/Mach). No component for IPSW extraction/component store. | OUT+INCPL
  Propose: add rows `ds-ipsw` (IPSW->component store), `ds-darwin` (Mach/BSD compat; Darling option), `ds-skylight` (SkyLight-compatible face); ds-rt "shim only, real libSystem from store".
C-10 | ROADMAP.md:30,74,129-145 (M4) | "Load and run a non-GUI Mach-O fixture without Apple `dyld` and without Darling"; "no Darling library is linked" | runtime-Apple-binaries | Acceptance forbids the owner's path; M4 also has no IPSW extraction, PAC/arm64e, Mach traps/MIG, dyld-cache mapping tasks. | INC+INCPL
  Propose: "M4: IPSW extraction to component store; run a real Apple CLI binary (dyld-1378 port or ds-loader) against the real shared cache; Mach/BSD compat. Darling-link scan becomes a licence-tracking check, not a ban."
C-11 | GLOSSARY.md:12-13,41; MIGRATION_FROM_DAYLIGHT.md:74,99,130 | "DaySetta does not run inside [Darling]"; "dyld shared cache... reads it offline only"; "replace with own ABI"; "substrate is DaySetta-owned, not Darling-owned" | runtime-Apple-binaries | Same; MIGRATION:74 discards Daylight's VERIFIED override table. | INC
  Propose: "Substrate ownership TBD: Darling remains an option. Shared cache is read offline for evidence AND mapped at runtime from the local component store."
C-12 | COMPATIBILITY_SCOPE.md:62-64 | Foundation/ObjC/libdispatch "IMPLEMENTED via Darling" only in Daylight columns; DaySetta target "T2 measured imports only"/"T2 ds-rt runtime" | runtime-Apple-binaries | Real Foundation/ObjC runtime come from IPSW. | INC
  Propose: "DaySetta target: real Apple framework from component store; gaps are Darwin-layer gaps, tracked by evidence."

## C. VM / identity
C-13 | ROADMAP.md:197-202 | "macOS desktop appears inside that window, used like a real macOS VM" | VM | Owner: NOT a VM. | INC
  Propose: "North Star: real macOS apps and desktop run as native Linux processes via the compat platform (Wine/Darling-like), drawn by the DaySetta window server on a Linux display. Not virtualization."
C-14 | ROADMAP.md:206-212 | questions: guest/host matrix, "Apple's macOS license limits on where virtualization is allowed", "bridge to host system libraries", AOT/JIT | VM | Premises are VM-shaped; "no macOS libs to bridge" is moot (libs come from IPSW). Matrix question remains valid for arch (arm64e on this host). | INC+UNRES
  Propose: keep IPSW handling, arch/translation (see O-2) and licence questions; delete "guest/virtualization/bridge to host libs"; move VM text to "Superseded decisions".
C-15 | docs/REF_ARM_TRANSLATORS.md:105-116 | "whole macOS desktop inside one window, used like a VM. That is a" | VM | Builds on C-13; research facts (rebar/translators; neither boots a guest) remain useful. | OUT (keep research)
  Propose: retitle section "Implications for the compat platform"; keep citations; drop "inside one window / VM" framing.
C-16 | VISION.md:77 ; ROADMAP.md:188 | "Deployment test in a clean VM" | VM | Only a CI/test environment, not product. | KEEP (clarify "test VM")

## D. Scope / tier / target
C-17 | AI_AGENT_HANDOFF.md:8-12 | "Linux-native window server that re-implements the shape of SkyLight... **not** a macOS compatibility layer" | vision | Direct inversion of owner decision. | INC
  Propose: see Section Owner-decisions below (rewrite to platform framing; ds/1 server = one component).
C-18 | docs/HANDOFF_FULL.md:21-28 | "T2 ... research target, narrow"; "no SkyLight dialect" | scope-tier | T2 is the product, not a footnote. Factual "no loader today" stays true. | OUT (keep fact)
  Propose: "Real-binary execution is the product goal; today none runs in this repo (Daylight/Darling evidence exists outside it)."
C-19 | COMPATIBILITY_SCOPE.md:15-19,16 | T2 = "Unmodified macOS 26.6 x86_64 binary"; "T2 is a research target, not a promise"; T3 launchd/XPC "Out of scope"; E4 "Not attempted" (:91,:96) | scope-tier | Real AppKit/desktop apps need launchd/XPC/cfprefsd-class services; scope excludes the goal. Tiers need re-cut. | INC+UNRES(tiers)
  Propose: "T3 services (launchd/XPC) are required for desktop apps; staged after T2, not excluded. Tier order and first target set by owner."
C-20 | PROTOCOL_STRATEGY.md:214-221 (also README.md:134, VISION.md:82, ARCHITECTURE.md:205, GLOSSARY.md:38-39, COMPONENT_MAP.md:125) | "Any client whose startup depends on an XPC service is outside the compatibility tiers" | scope-tier | Same. Daylight's "no XPC" is a Darling-guest fact, not a design rule. | INC
  Propose: "XPC/launchd: absent in Daylight guest (VERIFIED); to be provided by real launchd/xpc from IPSW or a shim; decision pending."
C-21 | PROTOCOL_STRATEGY.md:15,25-26,71-72 | "One codec, two dialects: ds/1 and skylight-26.6 share framing" | other | Real clients speak mach_msg MIG; SkyLight face must be Mach-native. Framing sharing is only an internal option. | INCPL
  Propose: "SkyLight face = real mach_msg/MIG over the Darwin layer; ds/1 is the native Linux protocol and internal bus."
C-22 | README.md:46-67 (profiles), ARCHITECTURE.md:11-12, VISION.md:59 | "Two profiles, one core": native Linux profile vs substrate | vision | Native profile becomes test aid/foundational infra; substrate is the product. | OUT
C-23 | ROADMAP.md:105-110 (M3), RENDERING_STRATEGY / M2 ordering | M3 "Recorded traces replay" | scope-tier | Replay is not a live Mach client. Add live-client gate. | INCPL
C-24 | ADR-0004 (title/Decision) and ROADMAP M2 | Headless first; Wayland/X11 "follow-up" | other | Linux display is on the owner flow's critical path (now only a later item). | INCPL (headless-for-CI is legitimate; KEEP)

## E. Target version / arch
C-25 | ARCHITECTURE.md:11, COMPATIBILITY_SCOPE.md:15,26, VISION.md:118 vs ADR-0001:1,12, AI_AGENT_HANDOFF.md:9-10, HANDOFF_FULL.md:21,26 | T2 = "macOS 26.6 x86_64"  vs  "first runtime target macOS 27.0.1 arm64e" | target-version | Docs disagree; Darling port/Daylight runs 26.6 x86_64 (x86 box); 27 arm64e cannot run natively on this ARMv8.0 host (no PAC/LSE) and ObjC names are unrecoverable offline. | UNRES (owner)
C-26 | adr/0001-target-macos-27.md:30-55,66-71 | Decision 1/5 "native first"; "runs ... without dyld and without Darling" ceiling (:69) | target-version | Hard-codes the no-Apple-dyld/no-Darling criterion and arm64e as THE target. Claim-boundary rules still valid. | OUT (partial) -> supersede, keep Decision 3 (claims)
  Propose: new ADR-0005 "Platform direction (owner decision 2026-11-06)" supersedes ADR-0001 :69 and ADR-0004/0002 scope notes; ADR-0001 keeps claim boundary.
C-27 | adr/0002-language-cpp20.md:55-67 | "No external libraries" (AGENTS.md:15 too) | other | Fine for ds/1 server; incompatible if Darling/dyld-port C code is linked. | UNRES (licence, see O-1)
C-28 | docs/IMPLEMENTATION_STATUS.md:87-91, :60; HANDOFF_FULL.md:157 | "22 cases" vs 23; "no wakeup fd" vs AI_AGENT_HANDOFF.md:43 "done" | process-rule | Stale internal inconsistencies. | OUT (fix numbers/wakeup)

## F. Process / stale facts
C-29 | AI_AGENT_HANDOFF.md:25,39-42 | "27 MIG table NOT SCANNED; artifacts/macos-27/ empty"; MIG scan "critical path" | process-rule | README.md:21-23,README table and IMPLEMENTATION_STATUS say scan done (744 routines, names inferred). | OUT
  Propose: "27 MIG scan done (static; 724/744 names inferred from 26.6). Critical path: IPSW->component store and running a real Apple binary."
C-30 | HANDOFF_FULL.md:156-166, 216-223; tools/macos27/fixups.py:19-20 | Evidence lives outside repo at hard-coded /home/ubuntu paths; IPSW "not part of repo" | other | Needs a component-store design (path config), not hard-coded. KEEP the not-in-repo rule. | INCPL
C-31 | AGENTS.md:1-36 | No "owner decisions" pointer; ownership rule "Do not edit outside work package" | process-rule | Add owner-decision-first rule; keep rest. | INCPL
C-32 | RISKS.md:11,19 (R2/R10) | Apple binaries = legal risk, "No Apple binaries" | process-rule | KEEP repo rule; add risk: local runtime use of IPSW-extracted binaries (EULA, redistribution, AEA keys). | INCPL
C-33 | Daylight side: /home/ubuntu/daylight/docs/knowledge/README.md:28,89; README.md:3,15,69,80 | Daylight README frames Darling as the Mach-O loader/xnu substrate, GPL-3.0 | other | Not contradicted by owner: DaySetta docs (C-01..C-11, MIGRATION:99, VISION:103) are the ones contradicting Daylight facts. Daylight README should add a pointer to DaySetta as its continuation. | INCPL

## G. KEEP (do not over-correct)
K-1 No Apple binaries/dylibs/caches/IPSW in repo or commits (README.md:128, AGENTS.md:26, HANDOFF_FULL.md:41,395, .gitignore *.ipsw). Reframe: "never committed/redistributed", not "never used".
K-2 No compatibility claims without evidence; "not macOS 27 compatible" phrasing (ADR-0001 Decision 3; HANDOFF_FULL.md:39; README.md:150-160). Evidence classes (README.md:110-119 vocabulary) and "Evidence or silence".
K-3 Never invent Apple constants/selectors/MIG ids/line numbers (AGENTS.md:31; HANDOFF_FULL.md:34-36); UNKNOWN for 27 ObjC names (D-20).
K-4 Local commits only, no remote, no push; preserve user work (HANDOFF_FULL.md:45,388-390).
K-5 Interface-file ownership, one test exe, -Werror (AGENTS.md:6-22). Headless CI path (ADR-0004), ds/1 wire spec (DS1_WIRE.md) = foundational infrastructure, keep.
K-6 Honest status tables (23/23, e2e, MIG scan "names inferred"), host facts (aarch64, no pauth, dev headers absent).
K-7 Report language rule (Vietnamese to user; docs English) - AI_AGENT_HANDOFF.md:56; HANDOFF_FULL.md:430.
K-8 Superseded technical discoveries stay as history: Daylight dyld-1378 override table, fixups.py findings, no-XPC finding, rebar/translator research.

## H. Owner-only questions
O-1 Licence: if Darling (GPLv3) code or the dyld-1378 port is linked/derived, is DaySetta GPLv3 (Daylight is GPL-3.0, README.md:80)? No LICENSE file exists in daysetta. Or keep Darling out-of-process (separate binaries)?
O-2 Target version/arch priority: macOS 26.6 x86_64 (works today under Darling on the x86 box) vs 27.0.1 arm64e (host is ARMv8.0 w/o PAC/LSE; needs emulation/translation or an arm64e-capable host). Which first, which host(s)?
O-3 Darwin layer: Darling-based, DaySetta-owned shim, or hybrid? Fixes ds-rt/ds-loader fate and ADR-0002 "no external libs".
O-4 Network: may tooling fetch Apple AEA/image keys (wrapped keys for IPSW system DMG) online, or must the user supply keys/DMGs? Where is the IPSW taken from (user-supplied only)?
O-5 EULA/legal stance for local extraction and execution; is "never redistribute" the whole policy, and may docs say "compatible" once evidence exists?
O-6 Tier re-cut: are launchd/XPC/cfprefsd/SystemUIServer in scope and in what order? Is real WindowServer client set (Finder/Dock) before third-party apps (Office/Chrome per memory goal)?
O-7 Is the PROTOCOL face real mach_msg only, or is the ds/1-framed SkyLight dialect (PROTOCOL_STRATEGY.md:15) still acceptable?

## I. Proposed "Owner decisions" section for AI_AGENT_HANDOFF.md (<= 12 lines)
```
## Owner decisions (read first; they override older docs)

* DaySetta continues Daylight: an IPSW-driven macOS userland compatibility platform on
  Linux (Wine/Darling-like). Not a VM, not a fake desktop, not just a window server.
* Flow: IPSW -> real Apple components -> component store -> run real binaries -> Darwin/Mach
  compat -> real frameworks -> SkyLight-compatible face -> DaySetta server -> Linux display.
* Real Apple dylibs/caches from the user's IPSW ARE runtime inputs. Never commit them.
* Inherit Daylight (/home/ubuntu/daylight); Darling stays an option (decision pending).
* ds/1 server = foundational component, not the product.
* Older docs saying "clean-room", "no dyld", "no Darling", "not a compat layer" or "VM"
  are superseded. Check this section before following them.
* Precedence: owner decisions > verified evidence > investigation reports > docs.
```
(12 lines incl. header+blank; keep file <= 60.)
Line budget for AI_AGENT_HANDOFF.md (now 56; +12 = 68; need -8 or more):
- Replace lines 6-12 ("What this project is", 7 lines) with a 3-line platform summary (-4).
- Drop line 25 (stale "27 MIG table NOT SCANNED") and line 26 (Wayland/X11/Vulkan row, already in IMPLEMENTATION_STATUS) (-2).
- Replace lines 39-42 (MIG scan task, stale) with 1 line pointing to IMPLEMENTATION_STATUS (-3).
- Drop line 43 (event-ring wakeup "done") (-1).
Result ~58 lines. Keep the "Rules that must not be broken" block but amend line "Do not add Apple binaries, dylibs or caches" to "Never commit Apple binaries/dylibs/caches (runtime use from the local component store is intended)" and drop "Do not call DaySetta macOS 27 compatible" only if evidence appears (keep now).

## Counts
Total 33 items: INCORRECT-or-INC+OUT ~20 (C-01,03,05..10,11,12,13,17,20,21?, etc.); OUTDATED 8 (C-02,15,18,22,26,28,29 + part); INCOMPLETE 8 (C-04,09,23,24,30,31,32,33); GENUINELY UNRESOLVED 4 (C-19 tiers, C-25 target, C-27 licence, C-14 matrix); KEEP 8 (K-1..K-8).
