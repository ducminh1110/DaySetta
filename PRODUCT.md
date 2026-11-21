# PRODUCT — DaySetta website (WEB task)

Inferred from briefs/web-brief.txt (no interactive interview possible: unattended brief run; assumptions labeled).

## What it is
DaySetta runs the real macOS 27 arm64e desktop userland (Apple's own WindowServer, AppKit, daemons, unmodified,
from the user's own IPSW) on an ARM Linux box. No VM, no hypervisor. Everything below the userland is faked:
Darling userspace XNU, iokitd playing the paravirt kexts, reims-vgpu translating Metal command streams to Vulkan.
Source of truth: ~/daysetta/README.md:3-31.

## Audience (assumption from brief)
Systems hackers, macOS RE people, Darling/Linux contributors, curious developers who saw the project mentioned.
They arrive skeptical ("another macOS-on-Linux promise?") and want proof, mechanism, and honesty.

## What the site must do
- Explain the mechanism (the layer stack) at a glance.
- Show honest current status: login completes, desktop agents run, composited frames still black, no desktop
  screenshot exists (brief lines 18-21; LOGINWIN.md:1464).
- Point to the devlog and the code. NO SaaS furniture: no pricing, no signup, no fake metrics/testimonials.
- Legal: no Apple artwork; trademark disclaimer in footer.

## Constraints (from brief, binding)
Static HTML+CSS+vanilla JS, no build step, works from file:// and GitHub Pages. Google Fonts ok.
Responsive 360px→wide. Light+dark. Accessible (4.5:1, focus, reduced motion). Fast, no big images.
Style: pangram.com layout/type/rhythm only — own palette, own art, own text. Papercut accents on 2-4 spots only.
Illustrations: hand-built in code (SVG/canvas), gouache/risograph quality, iterate with renders.
