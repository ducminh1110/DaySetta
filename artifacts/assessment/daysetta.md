# DaySetta architecture audit (read-only) vs. vision "real macOS userland on Linux, not a VM"

Repo: /home/ubuntu/daysetta (git log: 6 commits, 2026-11-05..06, tip e35c379).
Code: ~6.5k lines C++20 in src/ (server 820, sdk 724, scene 441, broker 458, ipc 284, protocol 283, compositor 213 ...), 7 test files (~2.4k lines), 23/23 tests recorded passing (build/Testing/Temporary/LastTest.log:47; docs/IMPLEMENTATION_STATUS.md:15). I did NOT run anything; pass state is from recorded logs/docs.
Minor doc inconsistency: docs/IMPLEMENTATION_STATUS.md says "23/23" (line ~15) but lists "The 22 cases" (section 3).

## 1. Status per layer

### A. IPSW inspection/extraction -> component store: RESEARCH/DOC ONLY (ABSENT as product code)
- No IPSW/cryptex/img4/APFS/DMG code anywhere in src/ include/ (grep "ipsw|cryptex|shared.cache|dyld" over src, CMakeLists.txt: 0 hits).
- IPSW only appears as a recorded fact (size, SHA-256, manifest 27.0.1/26A434): docs/IMPLEMENTATION_STATUS.md section 5 "macOS 27 evidence".
- The split arm64e cache is consumed from OUTSIDE the repo at a hard-coded path: tools/macos27/fixups.py:19-20 (`/home/ubuntu/daylight/tools/apicatalog`, `/home/ubuntu/macos-re/cache-arm64e/dyld_shared_cache_arm64e`); scan_mig.py:22 reads `/home/ubuntu/daylight/protocol/macos-26.6-x86_64.json`. No component store, no manifest, no extraction.
- AGENTS.md "Do not commit Apple binaries, dylibs or cache files" - policy only; no store design in ARCHITECTURE/COMPONENT_MAP (COMPONENT_MAP has no ipsw/store component; grep IPSW in all top-level md: only ROADMAP.md North Star + REF_ARM_TRANSLATORS).

### B. Mach-O execution (arm64/arm64e, dyld cache, fixups, PAC, codesign): PARTIAL (static research tooling only, Python, offline)
- IMPLEMENTED+TESTED (offline): arm64e split-cache slide-info v5 pointer decoder tools/macos27/fixups.py (151 lines) with self-test tools/macos27/test_fixups.py (145), result artifacts/macos-27/fixups-selftest.json. Scope is limited: decodes chained pointers in cache mappings; "isa/superclass/ro encoding: UNKNOWN" (docs/IMPLEMENTATION_STATUS.md WP1a); 2940 __got/__auth_got slots unbound.
- Static MIG scan tools/macos27/scan_mig.py (139) -> artifacts/macos-27/mig-routines.json (6919 lines), MIG_27_REPORT.md; `bash tools/macos27/verify_wp1.sh` documented VERIFY PASS. Output = addresses; names are `inferred-from-26.6` for 724 of 744; 20 "?" (MIG_27_REPORT.md; IMPLEMENTATION_STATUS WP1b).
- ABSENT: runtime Mach-O loader, executing arm64 code, ObjC runtime, dyld-cache mapping at runtime, PAC emulation/validation, codesign checks. ADR-0001 line ~27: "No Mach-O loader, ObjC runtime or macOS 27 runtime path exists in src/ or include/". docs/HANDOFF_FULL.md:28-29 "no Mach-O loader, no ObjC runtime and no SkyLight dialect". `ds-loader`/`ds-rt` appear only in docs (COMPONENT_MAP.md:31-32 `PLANNED`).
- Host fact: arm64 Linux host lacks `pauth` (IMPLEMENTATION_STATUS section 5, "macOS 27 runtime": UNKNOWN).

### C. Darwin/XNU compat (Mach ports/traps/MIG, BSD syscalls, pthreads, libSystem, launchd): ABSENT
- grep -rn "mach_msg|mach_port|bootstrap|MIG|NDR|SkyLight|CGX" src include -> 0 relevant hits (only C++ `namespace`/`opacity` noise).
- Docs explicitly exclude it: README.md:73 "No Darling. No XNU port..."; ARCHITECTURE.md:107 "No Mach VM - no mach_vm_map, no memory-entry port"; ARCHITECTURE.md:45 broker "is a name service ... not a Mach bootstrap server"; PROTOCOL_STRATEGY.md:214-221 "no XPC ... no launchd"; COMPATIBILITY_SCOPE.md:16 T3 launchd/XPC services "Out of scope".
- Only Linux-native IPC exists: AF_UNIX SOCK_SEQPACKET + SCM_RIGHTS + SO_PEERCRED (src/ipc/ipc.cpp:62,84; adr/0003).

### D. Real Apple frameworks (libSystem, CF, Foundation, CG, CoreText, CA, AppKit, SkyLight): ABSENT
- Nothing loads or runs any Apple framework. Plan is the opposite: `ds-rt` "libSystem-shaped ABI" re-implementation (COMPONENT_MAP.md:32), "No libSystem clone ... grows by measured imports only" (COMPONENT_MAP.md:126); ARCHITECTURE.md:171 "no dyld shared cache at runtime"; VISION.md:56 forbids "reusing cache dylibs as runtime inputs". Foundation/CF/ObjC rows in COMPATIBILITY_SCOPE.md:62-64 list status "IMPLEMENTED" only for the *Daylight/Darling* column, not DaySetta.
- Pre-existing evidence (outside repo, not verified by me): /home/ubuntu/daylight, /home/ubuntu/macos-re (docs/HANDOFF_FULL.md:160-190).

### E. DaySetta window server / rendering: PARTIAL, native-only (IMPLEMENTED + TESTED for a custom protocol)
- Broker src/broker/broker.cpp (458) + broker_proto.cpp; server src/server/server.cpp (820); scene src/scene/scene.cpp (441); CPU compositor src/render/compositor.cpp (213); headless PPM/PAM/BGRA output src/render/output.cpp; event ring + eventfd src/event/event.cpp; SDK src/sdk/sdk.cpp; ctl/session binaries.
- Tests: tests/test_scene.cpp, test_compositor.cpp (10 cases), test_broker.cpp, test_sdk.cpp, test_event.cpp, test_e2e.cpp (two windows, memfd surfaces, capture, input ring). 
- PARTIAL gaps (docs/IMPLEMENTATION_STATUS.md section 3/6): no spaces/notes/damage scheduling; CPU single-thread; no materials (grep material/glass/blur in src include: 0 hits); no multi-client/resize/occlusion tests; session & ctl untested; Wayland/X11/Vulkan output PLANNED (ADR-0004; headers absent). The only real output is a file writer: nothing displays on a screen.
- SkyLight-shaped part: ABSENT in code. Only the 744-routine static MIG address table (see B) exists.

### F. Desktop/system apps (Finder, Dock, SystemUIServer, etc.): ABSENT
- No app/service runs. Native demo clients only. "ds-shell" is optional/planned (ROADMAP.md section 4 task list). T3 system services "Out of scope" (COMPATIBILITY_SCOPE.md:16,132). M5 aim is "one narrow AppKit-shaped app" (ROADMAP.md section 7).

## 2. Contradictions / under-service vs. the vision (all verified with grep -n)

1. Identity: "not a macOS compatibility layer".
   - AI_AGENT_HANDOFF.md:11-12: "Today it is a native `ds/1` prototype, **not** a macOS compatibility layer."
   - README.md:3-7: "clean-room macOS window-server ... **compatibility substrate**" as one of two profiles (native Linux is the other); VISION.md "One core, two profiles" (line ~59).
   - Vision: macOS userland IS the product; native Linux profile is a test aid. Docs invert priority.
2. "Clean-room, no Apple code at runtime" vs vision "use Apple's actual components extracted from IPSW".
   - README.md:70 "Clean-room with respect to Apple"; README.md:72 "ds-loader loads ELF and Mach-O images without dyld"; README.md:73 "No Darling ... no dylib extraction for execution"; VISION.md:37 "never Apple dyld"; VISION.md:56 "Linking dyld or reusing cache dylibs as runtime inputs" listed as FORBIDDEN; VISION.md:55 clean-room; ARCHITECTURE.md:171 "no dyld shared cache at runtime"; COMPONENT_MAP.md:126 "ds-rt: not a full libSystem clone". Core contradiction: the vision requires running real libSystem/CF/Foundation/AppKit/SkyLight from the shared cache. Docs forbid using the cache as runtime input and plan to rewrite libSystem ("ds-rt") instead.
   - RISKS.md:11 (R2) and R10 line 19 treat Apple binaries as legal/contamination risk; no IPSW-licensing/extraction-on-user-machine design.
3. M4 "Compatibility substrate" (ROADMAP.md:127) = "Load and run a non-GUI Mach-O fixture without Apple `dyld` and without Darling" (ROADMAP.md ~130-135), "Provide an Objective-C runtime", "Implement ds-rt libSystem-shaped calls from a measured import census", "Acceptance ... no Apple dyld is mapped; no Darling library is linked". Under-serves: Layer B/C/D are one milestone with a CLI fixture exit; no arm64e/PAC/codesign/dyld-cache mapping/Mach traps/MIG/launchd tasks; M4 depends on M1 not M3 (diagram), but M5 needs both. COMPATIBILITY_SCOPE.md:15-19 T2 = "Unmodified macOS 26.6 x86_64 binary" (README/ARCHITECTURE.md:11 too: "macOS 26.6 x86_64") while ADR-0001 and docs/HANDOFF_FULL.md:21 name arm64e/27 as target: inconsistent target arch across docs.
4. "Headless first": adr/0004-headless-first.md (title: "Headless PPM/PAM/BGRA output first, Wayland and X11 later"). Reasonable for CI, but the ADR makes Wayland/X11/Vulkan "follow-up, own ADR" and ROADMAP M2 task list puts Wayland/X11/headless in M2 whereas status shows none; with the vision (desktop visible on screen) a real display backend is on the critical path for E/F. Not itself a contradiction, but under-served.
5. ROADMAP.md:197 "## 9. North Star (not started)": "a desktop window app where the user drags a macOS IPSW into it, it extracts automatically, and the macOS desktop appears inside that window, used like a real macOS VM." Directly contradicts "NOT a VM". Same section lists "Apple's macOS license limits on where virtualization is allowed" and "bridge to host system libraries" (rebar-style) as open questions. Also docs/REF_ARM_TRANSLATORS.md:105-116 "implications for the North Star" builds on it. It is also in tension with README.md:73 (no dylib extraction). It is the only place IPSW extraction (layer A) is mentioned as a goal, and it frames it as a VM UI, not a component store.
6. Wording that downplays/forbids the vision: ADR-0001 "Claim boundary" (no "macOS 27 compatible"; fine as honesty) but "Decision 5: Native first" and AI_AGENT_HANDOFF.md:12; VISION.md:118 "Mid term: a narrow substrate for macOS 26.6 x86_64 clients"; COMPATIBILITY_SCOPE.md:19 "T2 is a research target, not a promise"; COMPATIBILITY_SCOPE.md:16 T3 (system services, launchd, XPC) out of scope and PROTOCOL_STRATEGY.md:214-221 "any client whose startup depends on an XPC service is outside the compatibility tiers" - real AppKit apps and system apps (layer F) depend on launchd/XPC/cfprefsd/etc., so scope excludes the goal.
7. Darling stance: GLOSSARY.md:12 "DaySetta does not run inside it"; MIGRATION_FROM_DAYLIGHT.md:74 "Darling libsystem_kernel/libdispatch overrides VERIFIED -> ds-rt PLANNED, replace with own ABI"; :99 "substrate is DaySetta-owned, not Darling-owned". Vision says "closer to Wine/Darling than a VM": Darling's approach (real Apple userland on a Mach/BSD shim) is the closest working precedent (memory: Darling xnu-12377 port runs BaseSystem 26.6 binaries) and the docs reject it as a dependency while rebuilding the same layers.
8. Evidence-flow: project outputs for 27 (MIG table, fixups) are Python tools reading absolute /home/ubuntu paths outside the repo (tools/macos27/fixups.py:19-20). No reproducibility for a user.
9. Roadmap order mismatch: vision order A->B->C->D->E->F; ROADMAP order is native server (M2) -> SkyLight dialect replay (M3, server side only) -> substrate (M4) -> AppKit (M5). Server-first is OK only if the SkyLight face is Mach/MIG-real; M3 "Recorded traces replay with typed outcomes" is replay, not a live Mach client.

## 3. What a real SkyLight client needs vs what the server has

Server ops (include/daysetta/protocol.hpp:28-45; src/server/server.cpp:296-311): hello, create/destroy_window, set_window_frame/visible/opacity/title, order_window, commit_surface, subscribe/ack_events, scene_dump, inject_input, capture, ping, status = 16 ops, vs 744 routines in 9 SkyLight subsystems (artifacts/macos-27/MIG_27_REPORT.md; IMPLEMENTATION_STATUS WP1b).

Relation of ds/1 to SkyLight per docs: ds/1 is a custom native protocol (32-byte DSM1 header, docs/DS1_WIRE.md:1-30; little-endian, SOCK_SEQPACKET, memfd + SCM_RIGHTS). PROTOCOL_STRATEGY.md:15 "One codec, two dialects: native ds/1 and skylight-26.6 share validation and framing" (line 25-26 diagram) - i.e. the planned SkyLight dialect would be carried in the same DSM1 framing, with Mach port descriptors "translated by ds-compat" (PROTOCOL_STRATEGY.md:71-72) and OOL -> memfd. A real client sends mach_msg with MIG ids 29000+/30000+/32000+, port descriptors and NDR records; DSM1 framing does not parse that (PROTOCOL_STRATEGY.md:62-64 admits compatibility must preserve Daylight's observed descriptor/NDR shapes; "ds-compat" is a planned component, no code). So: no relation in code today; in docs it is an adapter at the edge.

Missing for a real Apple client (all ABSENT in code):
- Mach transport: ports, send/receive rights, mach_msg/mach_msg2 traps, port sets, notifications (dead-name/no-senders), voucher/QoS, audit-token peer identity (only SO_PEERCRED uid exists).
- Bootstrap: bootstrap_look_up / com.apple.windowserver(.active), CARenderServer aliases (PROTOCOL_STRATEGY.md:149-151 plan only); broker is a name service (src/broker/broker.cpp), no launchd/XPC (PROTOCOL_STRATEGY.md:214).
- MIG dispatch: subsystem tables, id routing (29000/30000/32000/33000/34000/35000 ranges, PROTOCOL_STRATEGY.md:98-112), complex-message descriptor + NDR codec, reply message construction, simpleroutine vs routine semantics. Offline address inventory exists (artifacts/macos-27/mig-routines.json) but 724/744 names are inferred from 26.6 and arg descriptors are not decoded (scan accepts arg_descr 0; per scan_mig.py method line 4). Argument layouts/types: UNKNOWN for 27.
- SkyLight/CGS semantics: connection create (CGSNewConnection handshake constants "600.0", COMPATIBILITY_SCOPE.md:44), window ids/levels/tags/shapes/regions, spaces, display config, cursors, session notes (0x390/0x391/0x33b ROADMAP.md:112), event tap/spin/capture reply subsystems (server->client).
- Rendering handoff: CARenderServer/CA context + IOSurface/shared-memory surface sharing, backing-store commit (docs say "NSViewBackingLayerContents PARTIAL", COMPATIBILITY_SCOPE.md:61), no CA layer-tree/render-server protocol at all; materials/glass absent.
- Mach VM/memory entries for OOL surfaces (ARCHITECTURE.md:107 says none).
- Input: native event ring (40-byte events, src/event/event.cpp) vs CGEvent/ HID/ SLEvent delivery via Mach messages: not mapped.

## 4. Reusable as-is vs native-only

Reusable for real Apple clients (behind a Mach/MIG front-end; these are protocol-agnostic cores):
- src/scene/scene.cpp: window ordering/levels/opacity/frame/visibility, snapshots (tests/test_scene.cpp 8 cases) - semantics are generic but only subset of CGS window model (UNKNOWN fit).
- src/render/compositor.cpp + output.cpp + sha256.cpp: deterministic CPU compositing, golden-hash writer (10 tests). Pixel format BGRA premul; no color-management/IOSurface formats (UNKNOWN fit).
- src/ipc/ipc.cpp SCM_RIGHTS fd passing, memfd surface mapping, src/event/event.cpp ring+eventfd: usable as the Linux backend for translated OOL memory / events.
- src/broker/broker.cpp name registry: could back bootstrap_look_up emulation (needs Mach port identity layer).
- tools/macos27/fixups.py, scan_mig.py, test_fixups.py, artifacts/macos-27/*: directly reusable for layer B/C research (MIG tables, cache pointer decode) but need relocation off hard-coded paths.
- tests/test.hpp harness, ci/run.sh, ci/check_docs.py.

Native-only (would be bypassed or replaced for real clients):
- DSM1 header/ops (include/daysetta/protocol.hpp, src/protocol/protocol.cpp, docs/DS1_WIRE.md), broker DSK1 protocol, token-in-hello auth, src/sdk/*, daysetta-ctl/-session (session just forks the stack: src/session/main.cpp; not launchd), role enum (app/control/test).

## 5. Biggest gaps summary
- Layers A, C, D, F: no code. B: Python static research only. E: native toy path, no SkyLight face, no display backend.
- The docs' architecture is a clean-room reimplementation ("ds-rt") aimed at one narrow fixture; vision requires running Apple's own binaries. ADR/README/VISION/COMPONENT_MAP/MIGRATION need rewriting around: component store, real dyld cache, Mach/BSD kernel shim, Apple-shipped frameworks, SkyLight Mach front-end onto existing scene/compositor.

UNKNOWN: whether CGS window/space semantics fit scene API; arm64e PAC on this host (no pauth); MIG argument descriptors for 27; whether the 23/23 tests pass now (not run; logs only); GitHub Actions (.github/workflows/ci.yml) results.

## 6. Possible duplicates of Daylight (grep-checked; Daylight side not judged in depth)

| DaySetta | Daylight counterpart | Verdict |
|---|---|---|
| tools/macos27/fixups.py (slide-info v5 pointer decode, 151 lines) | /home/ubuntu/daylight/tools/apicatalog/machoinfo.py (940 lines): parses cache header slide_info_offset/size (lines 524-625) and sees LC_DYLD_CHAINED_FIXUPS (59, 360) but grep finds no pointer-resolve/decode function; D-21 in daylight/docs/knowledge/TECHNICAL_DISCOVERIES.md:281 records the salt as only partly decoded | Extension (genuinely new decoder), and it imports machoinfo (fixups.py:19,75) |
| tools/macos27/scan_mig.py (139) | /home/ubuntu/daylight/tools/mig_scan.py (71): same mig_subsystem layout (scan_mig.py header cites mig_scan.py:47-58), raw-pointer scan for 26.6 x86_64 | Extension: same algorithm, new arm64e/slide-info-aware pointer path. Partial duplicate of the scan logic (no shared code) |
| artifacts/macos-27/mig-routines.json | daylight/protocol/macos-26.6-x86_64.json, daylight/data/apple-api/mig-routines.json | Genuinely new data (27); names borrowed from 26.6 |
| src/server/server.cpp + scene + compositor + event | daylight/server/src/*.c (dispatch.c, window.c, input.c, session.c, bridge.c with dl_canvases_composite, shell.c, 14 C files), plus frontends/x11, ca/, bridge/ | Overlapping scope (window server, compositing, input). DaySetta is a from-scratch rewrite with a different protocol (ds/1); Daylight's dispatch.c is a real Mach/MIG front-end that DaySetta lacks. Duplicate in purpose, not in code |
| src/render/output.cpp (PPM/PAM) | daylight/tools/xwd2png.py, frontends/x11 (X11 output) | Different: Daylight has a real X11 display path; DaySetta has file output only |
| src/broker, src/session, src/ctl | daylight/tools/daylight-stack.sh, systemd/, tools/daylight-dock.sh | Overlapping (service start/stop); DaySetta's are C++ and untested for session/ctl |
| docs MIGRATION_FROM_DAYLIGHT.md, PROTOCOL_STRATEGY 5.2 | daylight/docs/knowledge/api/mig-interface-catalog.md | Docs re-cite; no re-implementation |
| ds-loader / ds-rt (planned) | daylight/server/src/main_darling.c (Darling memory entries) and Darling-based substrate (outside both repos) | Planned duplicate of Darling/Daylight-on-Darling work; UNKNOWN until built |

Risk: DaySetta throws away Daylight's working Mach/MIG path (dispatch.c, dl_routines.c 753 lines, dl_reply_sizes.c) and reimplements the window server natively; only the offline tools reuse Daylight code.
