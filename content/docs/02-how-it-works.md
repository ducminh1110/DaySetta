---
title: How it works
summary: Five floors. Each one pretends to be what the floor above expects.
---
macOS userland expects Apple hardware and the XNU kernel. DaySetta keeps the userland real and replaces every layer underneath it.

```
 macOS 27 arm64e userland (real binaries from the IPSW)
   WindowServer · AppKit apps · launchd daemons
            │  Mach / XPC / IOKit
 Darling     │  userspace XNU (darlingserver), dyld, libsystem_kernel
 iokitd      │  plays the kexts in userspace
 reims-vgpu  │  paravirt GPU device -> metal2vulkan -> Vulkan
            │  syscalls
 Linux aarch64 (ARMv8.0, no PAC/LSE -> emulated in src/exec)
```

## 01 · Linux aarch64

The reference host is a plain **ARMv8.0** machine. macOS 27 is built for **arm64e**, which uses pointer authentication (PAC) and LSE atomics. The host has neither, so `src/exec` lowers those instructions at runtime instead of running a full CPU emulator.

## 02 · reims-vgpu

The guest's Metal command buffers go to [reims-vgpu](https://github.com/steelbrain/reims-vgpu), a paravirtual GPU device, and get translated to **Vulkan** through metal2vulkan. Metal itself stays Apple's own driver. On a box without a GPU, Vulkan falls back to llvmpipe, so it is slow.

## 03 · iokitd

There are no kexts. `iokitd` answers their **user clients** instead: IOMobileFramebuffer, AppleParavirtGPU (IOGPU), IOSurface and IOHID. WindowServer thinks it is talking to Apple's paravirtual display and GPU, the same path a macOS VM would see.

## 04 · Darling

[Darling](https://github.com/darlinghq/darling) provides the kernel that isn't: a userspace XNU (`darlingserver`), dyld and `libsystem_kernel`, ported up to **xnu-12377**, the macOS 27 kernel. It answers Mach traps and syscalls so Apple's libraries never notice they are on Linux.

## 05 · macOS 27 userland

Real arm64e binaries from your own macOS 27 IPSW: WindowServer, SkyLight, AppKit apps and the launchd daemons, unmodified.

## A boot, end to end

1. Darling starts `launchd`, which brings up the daemons (notifyd, cfprefsd, opendirectoryd, runningboardd, ColorSync, launchservicesd…).
2. WindowServer opens the fake display through `iokitd` and starts compositing through the Metal driver.
3. `loginwindow` walks its login states, authenticates, and launches the desktop agents.
4. Every frame WindowServer composites goes through reims-vgpu to Vulkan and back to the display.

Today step 3 completes, step 4 produces frames, and those frames are black. That is the current fight.
