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
- 2026-11-06: no 3D GPU on this VPS (virtio 2D only). Owner: Metal proceeds on software (llvmpipe/lavapipe) for now, always labelled SOFTWARE; hardware accel later on a GPU host.
- 2026-11-06 Metal device policy: runtime auto-select — HW/3D Vulkan (discrete>integrated>virtual) when present, fallback lavapipe/llvmpipe on 2D-only hosts; override DAYSETTA_METAL_DEVICE=hw|sw|<idx>; always log/label HW vs SOFTWARE.
- 2026-11-07: x86 box restored (remotemac.mooo.com, new host key SHA256:LxdOpR9H... accepted by owner). Owner darlingserver fixes exist there: darling port/macos26, darlingserver 9e945ed..7647e89. Backed up to DaySetta recovery/x86 (00d40f2). D6 merges port/macos26 + daysetta/arm64 into daysetta/unified.
- 2026-11-07 Colab GPU: colab CLI at ~/.local/bin/colab (uv tool), auth --auth oauth2 done by owner (token in keyring). HW Vulkan on T4 works via ~/daysetta/tools/colab/vulkan_setup.py (libEGL_nvidia ICD; libGLX fails). Colab is x86_64, no Darling; use for host-side indium/Vulkan HW tests. Stop sessions when idle (compute units).
- 2026-11-07 RULE: Apple frameworks/apps (AppKit and all core frameworks) come ONLY from macOS 27 (cache-arm64e, store-27). Never reuse the owner's macOS 26.6 set (x86 ~/basesystem, 26.6 caches/apps, macos26 overrides). Owner's Darling code fixes/tools are fine, retarget 26-specific values to 27. Tell every subagent.
- 2026-11-07 RULE: aim = Apple Silicon apps. macOS 27 path runs entirely on Apple's own arm64e userland (27 cache/store). No Darling-built replacements for Apple-shipped images (libdispatch, libobjc, CF, libxpc...). Only kernel-boundary shims allowed: libsystem_kernel, mldr, darlingserver, launchd/daemons lacking an Apple equivalent. Fix the emulation layer, never swap in Darling builds.
- 2026-11-07: libdispatch = Apple's from 27 cache (required, no Darling fallback); implement missing kernel features (workq/kevent workloops/QoS) in libsystem_kernel+darlingserver.
- 2026-11-07 RULE: performance is a requirement — CLI ~1s warm, GUI first window in seconds, near-native steady state, no per-instruction kernel traps on hot paths (user-space trampolines), bench in ~/daysetta/tools/bench with results in artifacts/bench.
- 2026-11-07 RULE: watch the 5-hour plan limit (get_usage). At ~95% tell running agents to wrap up (commit, handoff file, export) and respawn after reset.
- 2026-11-07 Owner chose REAL Mach vouchers (bank/importance/pthpriority attr managers in darlingserver) over the old VOUCHER_USE_EMPTY_MACH_BASE_VOUCHER libdispatch_apple workaround. Being done as D9 by crouton-alpha; Sonnet takes over if it stalls.
