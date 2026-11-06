# ADR-0004 — Headless PPM/PAM/BGRA output first, Wayland and X11 later

| Field | Value |
|---|---|
| Status | Accepted |
| Date | 2026-11-06 |
| Related | `adr/0002-language-cpp20.md`, `RENDERING_STRATEGY.md`, `docs/IMPLEMENTATION_STATUS.md` |

## Context

The first output backend must work on the current host and in CI without a display,
GPU driver, compositor or window system. It must also produce an artifact that a test
can hash and a human can inspect.

| Fact | Class | Evidence |
|---|---|---|
| `pkg-config` is not installed on the host, so the requested development-package probe cannot run | `OBSERVED` | `pkg-config --modversion x11 wayland-client vulkan` → `pkg-config: command not found`, exit 127, 2026-11-06 |
| The X11 development header is absent | `OBSERVED` | `/usr/include/X11/Xlib.h` absent, 2026-11-06 |
| The Wayland client development header is absent | `OBSERVED` | `/usr/include/wayland-client.h` absent, 2026-11-06 |
| The Vulkan development header is absent | `OBSERVED` | `/usr/include/vulkan/vulkan.h` absent, 2026-11-06 |
| Runtime Wayland/Vulkan libraries exist, but headers and linkable development files do not | `OBSERVED` | `ldconfig -p` lists `libwayland-client.so.0` and `libvulkan_*.so`, 2026-11-06 |
| The render API declares PPM, PAM and raw BGRA writers | `IMPLEMENTED` | `include/daysetta/render.hpp:61-63` |
| The writer implementation is still a placeholder | `PLANNED` | `src/render/output.cpp:1` |
| Wayland and X11 output are not implemented | `PLANNED` | `COMPONENT_MAP.md` component inventory, `ROADMAP.md` M2 tasks |

The absence of `pkg-config` means the package probe is inconclusive by itself. The
header checks are the direct evidence: the three development headers are not present.
Runtime shared objects alone are not enough to compile a backend.

## Decision

1. **First backend.** Render to a CPU frame and write PPM (P6), PAM (P7) and raw BGRA
   files. These formats need no external library and no display server.
2. **Determinism.** Use the same frame bytes for all three writers. PPM/PAM are
   inspection formats; BGRA is the byte-exact comparison format. A golden test hashes
   the BGRA frame and records the SHA-256.
3. **CI.** The default CI path runs headless: no X11, Wayland or Vulkan package, no
   display, no GPU, no network download.
4. **Wayland and X11.** Treat both as follow-up backends with their own ADRs and
   dependency review. Do not claim either is implemented until a test with the real
   protocol library passes on a host that has the development package.
5. **Vulkan.** Not part of M1/M2. The CPU compositor is the reference; a GPU backend
   must match it within an explicit tolerance.

## Consequences

* The server can produce frames on the current aarch64 Linux host without a desktop.
* The first golden images are files in the repository's ignored `frame-*.ppm` /
  `frame-*.bgra` namespace, or test fixtures with provenance. No Apple screenshot is
  committed.
* A headless frame proves composition and encoding, not window-system integration. It
  cannot satisfy a Wayland/X11 exit criterion.
* If a future host has `pkg-config` and the three development packages, re-run the
  probe and record the versions before changing this ADR.

## Rejected alternatives

| Alternative | Reason rejected |
|---|---|
| Wayland first | No client development header on the host; adds a protocol dependency before the compositor is proven |
| X11 first | No Xlib header on the host; X11 is a legacy follow-up, not the primary target |
| Vulkan first | No Vulkan header; GPU results are not bit-reproducible without tolerances |
| PNG only | Requires a compression dependency or a larger writer; PPM/PAM/BGRA are simpler and exact |
