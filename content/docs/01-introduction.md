---
title: Introduction
summary: What DaySetta is, what it is not, and where it stands today.
---
DaySetta runs **Apple's own macOS 27 userland** (WindowServer, AppKit, the launchd daemons) unmodified on a plain ARM Linux box, and fakes everything *below* it. There is no virtual machine and no hypervisor: macOS code runs as ordinary Linux processes on the same kernel.

It is the follow-up to **Daylight**, an older x86 experiment that tried to reimplement WindowServer. Daylight taught the lesson DaySetta is built on: the protocol is the hard part, so let Apple's code speak it.

## The name

**Day** comes from the Daylight / Darling family of projects that run macOS software on Linux. **Setta** comes from Rosetta: the whole project is a translation layer between two worlds.

## What it is not

- **Not a VM.** No hypervisor, no guest kernel. The "hardware" macOS sees is answered in userspace.
- **Not a Hackintosh.** Linux stays Linux. Nothing boots macOS or replaces your OS.
- **Not a reimplementation.** WindowServer, SkyLight, AppKit and the daemons are Apple's binaries, taken from your own IPSW.

## Status (November 2026)

| | |
|---|---|
| WindowServer 27 | boots and stays up |
| Metal | the driver comes up, command buffers execute, swaps reach the display |
| Login | completes; Dock, MenuBarAgent, SystemUIServer, WallpaperAgent and WindowManager run |
| Pixels | composited frames are still **black** |
| Boot | about half the boots hang before login |
| Input | keyboard / mouse (HID) is half done |

The black screen is traced down to a single link: the text draws reach the GPU with a fill colour whose alpha is already zero. The GPU path itself is proven faithful. See the [blog](../../blog/) for the full story.

> Honest guess from the repo: a usable desktop is still days to weeks out, not hours.
