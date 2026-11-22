---
title: Principles
summary: The rules every change follows.
---
## No Apple binaries in git

Everything Apple comes from your own IPSW. The repo only holds the translation layers and tooling.

## No app-specific hacks

If an app breaks, fix the general thing underneath it. A fix that only makes one app work hides the real bug and breaks the next app.

## Fake the floor, not the house

Apple's userland stays unmodified. Darling-built replacements are only allowed for kernel-ish shims; if Apple code breaks, the emulation underneath is what gets fixed.

## Evidence or UNKNOWN

Every claim in the notes has a log line, a `file:line` or a disassembly address next to it. When something is not proven, it is written down as **UNKNOWN**, together with how it could be checked. A short correct answer beats a long guessed one.

## Faithful rendering

The GPU layers draw exactly what the guest asks for. When the guest submits a zero colour, the host draws black; forcing white would only mask the real defect upstream.
