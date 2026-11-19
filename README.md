# DaySetta

Running the real macOS 27 desktop on an ARM Linux box. No VM, no hypervisor.

DaySetta is the follow-up to [Daylight](MIGRATION_FROM_DAYLIGHT.md) (my older x86 window server experiment).
Daylight tried to reimplement WindowServer. DaySetta goes the other way: run Apple's own userland
(WindowServer, SkyLight, AppKit, the daemons) unmodified, and fake everything *below* it.

## how it works

```
 macOS 27 arm64e userland (real binaries from the IPSW)
   WindowServer · AppKit apps · launchd daemons
            │  Mach / XPC / IOKit
 ───────────┼──────────────────────────────────────────
 Darling     │  userspace XNU (darlingserver), dyld, libsystem_kernel
            │
 iokitd      │  plays the kexts in userspace:
            │   IOMobileFramebuffer, AppleParavirtGPU (IOGPU), IOSurface, IOHID
            │
 reims-vgpu  │  paravirt GPU device -> metal2vulkan -> Vulkan
 ───────────┼──────────────────────────────────────────
 Linux aarch64 (ARMv8.0, no PAC/LSE -> emulated in src/exec)
```

- **CPU:** macOS 27 is arm64e. The host is plain ARMv8.0, so PAC and LSE get emulated (`src/exec`).
- **Kernel:** Darling's userspace XNU, ported up to xnu-12377 (the macOS 27 one).
- **Kexts:** there are no kexts. `iokitd` answers their user clients instead, so WindowServer thinks
  it's talking to Apple's paravirtual display and GPU.
- **GPU:** the guest's command buffers go to [reims-vgpu](https://github.com/steelbrain/reims-vgpu)
  and get translated to Vulkan. On a box with no GPU that's llvmpipe, so it's slow.

## where it's at (nov 2026)

works:
- WindowServer 27 boots and stays up
- the Metal driver comes up, command buffers execute, swaps reach the display
- first real pixels through the whole GPU -> present -> readback path (a test pattern, not a window yet)
- an AppKit test app launches, connects to WindowServer and runs its whole script
- notifyd (ported Libnotify-348), runningboardd, ColorSync, launchservicesd come up

not yet:
- app windows still render black. current suspect is LaunchServices session state that never
  gets seeded, so the app's main runloop never gets to commit its layers
- keyboard / mouse (HID) is half done
- login, Dock, Finder, the default apps

Honest guess: a usable desktop is still days to weeks out, not hours.

## repo

| path | what |
|---|---|
| `src/exec` | arm64e on ARMv8.0: PAC, LSE, RCPC lowering |
| `src/darwin` | Mach-O loading, commpage, shared region, syscall table for the native runner |
| `src/windowserver` | the Daylight server bits carried over (see its PROVENANCE.md) |
| `src/metal` | offscreen Metal triangle test |
| `tools/macos27` | IPSW / shared cache tooling, the decoder census |
| `tools/colab` | Vulkan on a Colab T4, for comparing against llvmpipe |
| `devlog/` | raw test logs and short notes on why things were done a certain way |
| `docs/` | deeper notes (PVG surface, Metal stack, WindowServer 27) |

The top-level design docs (`VISION.md`, `ARCHITECTURE.md`, `ROADMAP.md`, ...) are from the first plan,
when DaySetta was going to be a from-scratch server with no Darling. That plan got dropped; the docs
are kept for history.

Most of the actual changes live in my Darling tree (iokitd, darlingserver, xnu, libnotify, launchd),
not in this repo yet.

## rules I stick to

- no Apple binaries in git. Everything Apple comes from your own IPSW.
- no app-specific hacks. If an app breaks, fix the general thing underneath.
- every claim in the notes has a log line or a file:line next to it.

## thanks

[Darling](https://github.com/darlinghq/darling), [reims-vgpu](https://github.com/steelbrain/reims-vgpu)
and metal2vulkan, which this is built on. Ideas (not code) from Henge, Stela, Snapintosh, Liberator
and [macPad](https://github.com/DCMMC/macPad).

Personal project, for fun. Not affiliated with Apple.
