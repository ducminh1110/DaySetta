---
title: Glossary
summary: The words that show up in the devlog.
---
| Term | Meaning |
|---|---|
| **arm64e** | Apple's ARM64 ABI with pointer authentication. macOS 27 userland is built for it |
| **PAC** | Pointer Authentication Codes: signed pointers checked by special instructions. Emulated on ARMv8.0 |
| **LSE** | Large System Extensions: ARMv8.1 atomic instructions. Emulated on ARMv8.0 |
| **IPSW** | Apple's firmware/restore image; the source of every Apple binary DaySetta uses |
| **dyld shared cache** | One big file holding most system frameworks, pre-linked |
| **Darling** | A Darwin/macOS compatibility layer for Linux; DaySetta's kernel floor |
| **darlingserver** | Darling's userspace XNU: Mach ports, IPC and threads live here |
| **XNU** | The macOS kernel. DaySetta targets xnu-12377 (macOS 27) |
| **Mach port** | A kernel endpoint for messages, with send/receive rights |
| **MIG** | Mach Interface Generator: turns interface definitions into message stubs |
| **XPC** | Apple's higher-level IPC on top of Mach, used by most daemons |
| **launchd** | The process that starts and looks up every service |
| **libdispatch** | Grand Central Dispatch: queues, workloops and event sources |
| **kevent / workloop** | The kernel event mechanism libdispatch waits on |
| **WindowServer** | The macOS compositor and window manager process |
| **SkyLight** | The private framework behind the WindowServer protocol |
| **CoreAnimation** | The layer tree and renderer apps and WindowServer use to draw |
| **IOSurface** | Shared GPU-backed image memory between processes |
| **IOMobileFramebuffer** | The display driver interface WindowServer presents frames through |
| **AppleParavirtGPU** | Apple's paravirtual GPU, the device a macOS VM sees; DaySetta impersonates it |
| **iokitd** | DaySetta's userspace stand-in for the kexts |
| **reims-vgpu** | The paravirtual GPU device that runs the guest's Metal work on Vulkan |
| **metal2vulkan** | The translator from Metal shaders and commands to Vulkan |
| **llvmpipe** | Mesa's CPU Vulkan driver, used when there is no GPU |
