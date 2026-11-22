---
title: Repo tour
summary: Where things live in the DaySetta repository.
---
| Path | What |
|---|---|
| `src/exec` | arm64e on ARMv8.0: PAC, LSE, RCPC lowering |
| `src/darwin` | Mach-O loading, commpage, shared region, syscall table for the native runner |
| `src/windowserver` | the Daylight server bits carried over (see its PROVENANCE.md) |
| `src/metal` | offscreen Metal triangle test |
| `tools/macos27` | IPSW / shared cache tooling, the decoder census |
| `tools/colab` | Vulkan on a Colab T4, for comparing against llvmpipe |
| `devlog/` | raw test logs and short notes on why things were done a certain way |
| `docs/` | deeper notes (PVG surface, Metal stack, WindowServer 27) |

## Old design docs

The top-level design docs (`VISION.md`, `ARCHITECTURE.md`, `ROADMAP.md`, …) are from the first plan, when DaySetta was going to be a from-scratch server with no Darling. That plan got dropped; the docs are kept for history. The live direction is what this site and the devlog describe.

## The devlog

`devlog/decisions.md` is the heart of the repo: one short note per decision, newest at the bottom, each with the reason and what was tried and dropped. The [devlog page](../../devlog/) on this site is built from it automatically.
