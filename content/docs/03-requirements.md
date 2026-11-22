---
title: Requirements
summary: What the project runs on today, and why there are no install steps yet.
---
DaySetta is a research project, not a packaged product. There is **no installer and no one-command setup yet**: most of the actual changes still live in a Darling tree (iokitd, darlingserver, xnu, libnotify, launchd) and are not in the public repo yet.

## What the reference setup uses

| Part | What |
|---|---|
| Host CPU | ARM **aarch64** Linux; the reference box is plain ARMv8.0 (PAC and LSE get emulated) |
| GPU | optional. Without one, Vulkan falls back to llvmpipe and it is slow |
| macOS | your own **macOS 27 IPSW**. The shared cache, frameworks and apps come from there |
| Kernel layer | Darling, ported to xnu-12377 |

## Apple binaries

No Apple binaries are committed to git, ever. Everything Apple comes from an IPSW you supply.

## x86 hosts

Later. The goal is running ARM Mac apps first; the ARM build has to mature before an x86_64 path (through an ARM64 emulator) makes sense.

Follow the [devlog](../../devlog/) for progress. When it becomes runnable, this page gets real install steps.
