---
name: daysetta-owner-decisions
description: DaySetta owner decisions 2026-11-06 — product = IPSW-driven real-macOS userland on Linux; macOS 27 arm64e on ARMv8.0 via hybrid native + explicit PAC/LSE emulation
metadata:
  node_type: memory
  type: project
  originSessionId: 0b182f4a-fdf5-457c-bf1f-27afc98a81e7
  modified: 2026-11-06T09:38:03.290Z
---

DaySetta (~/daysetta) is the continuation of Daylight ([[daylight-windowserver]]): run REAL Apple userland/apps/desktop (Finder, Dock, Terminal… eventually Xcode, Metal) from the user's IPSW on Linux. Not a VM, not a fake/Qt/GTK/HTML desktop. Never commit Apple binaries.

Owner decisions (2026-11-06):
- Target macOS 27 arm64e on the ARMv8.0 host (no pauth, no LSE). Hybrid native execution + DBT; PAC and LSE via explicit ABI-aware emulation/lowering. NO blanket PAC stripping, no silent instruction substitution. Native preferred when host supports it. QEMU only as reference/test oracle.
- Darling = reference + optional backend, not auto-excluded ([[darling-xnu-port]]).
- AEA keys: use `ipsw` (3.1.739 installed) — embedded PEM -> --pem-db -> Apple wkms; never hardcode keys; verify DMG before APFS.
- Docs conflicting with owner direction are outdated (33-item register in ~/daysetta/artifacts/assessment/conflicts.md); owner > evidence > reports > docs.
- Wants frequent progress reports with REAL screenshots in artifacts/screenshots/; Vietnamese reports; Opus coordinates, Sonnet implements, Haiku for small tasks; save tokens ([[feedback-ponytail-mode]]).
- Reference project worth studying: Snapintosh (github.com/Snapintosh) — EL2 monitor booting stock macOS 27 on Snapdragon X2, AIR->ir3 shader compiler, MIT.

**Why:** earlier sessions drifted into a clean-room native window server; owner corrected this.
**How to apply:** every package must move toward a genuine Apple app running; reuse Daylight/fixups.py/scan_mig.py; no fake UI.
- 2026-11-06: maximize reuse of EVERYTHING existing (Apple components first, then Daylight server/WindowServer, daylight-ca, bridge, services, Darling fork, dyl4, RE outputs). Rewrite only when technically necessary and say why. Graphics slice (R5) must start from Daylight's server (Mach/MIG dispatch, X11 frontend, canvases) + daylight-ca, integrated with DaySetta, not from ds/1.
- 2026-11-06 FINAL: Darling (ported to aarch64) is the PRIMARY runtime foundation; GPLv3 accepted. R3 (ds-run own Darwin layer) is research only, retire once Darling covers it. ~/darling is an upstream darlinghq shallow clone; owner fixes survive only in ~/darling-port-patches (Oct 29) + handoff prose; recovery work in ~/darling-recovery/.
- 2026-11-06: owner is recovering the x86_64 machine data (original darlingserver fixes etc.). Do NOT re-implement darlingserver fixes; when work reaches darlingserver, STOP and tell the owner; wait for recovered data.
- 2026-11-06 PUSH POLICY: after every verified step, run ~/daysetta/tools/recovery/export.sh (bundles Darling fork + submodules, dyl4, notes, tools into recovery/), commit and push ~/daysetta to github.com/ducminh1110/DaySetta (gh logged in, repo+workflow). Goal: losing the ARM host loses nothing. Never Apple binaries/keys. Tell subagents to do the same at the end of their task.
- 2026-11-06: VPS provider will restore the x86_64 machine data on 2026-11-07; darlingserver owner fixes (fileport RPC, audit token pid version, restartable, shared remap, asid) are expected there. Current darlingserver = plain upstream 40275fb.
- 2026-11-06 Metal backend: owner chose Darling's indium (github.com/darlinghq/indium main, @68c2c18, iridium AIR->SPIR-V, 0BSD). metal2vulkan = reference only.
