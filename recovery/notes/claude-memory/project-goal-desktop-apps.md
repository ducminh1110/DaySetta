---
name: project-goal-desktop-apps
description: "Real goal of the Darling+Daylight work: run full macOS desktop apps and third-party apps (Office, Chrome); BaseSystem apps are only a stepping stone"
metadata:
  node_type: memory
  type: project
  originSessionId: 2b408220-078e-4ac0-8ea9-b8330b14818c
  modified: 2026-10-31T16:28:28.385Z
---

2026-10-31: the user clarified that running the BaseSystem.dmg apps is a means, not the end. The goal is to later run the apps of a full macOS desktop plus some third-party apps (Microsoft Office, Chrome, ...). Apps that are too niche or give little value ("cheap") are to be skipped.

**Why:** effort should go into capabilities that real desktop apps need, not into recovery-only apps.

**How to apply:** prioritize BaseSystem apps that exercise shared capabilities (Web Browser = WebKit + multi-process XPC like Chrome; Terminal; DiskImageMounter = opening .dmg to install apps; Installer / Install macOS = .pkg installs; TextInputSwitcher / Language Chooser = input methods, keyboard layouts) and shared Daylight/Darling work (menu bar, window move/resize, key window, animations, pasteboard, open/save panels, XPC services, GPU/Metal fallback, networking, LaunchServices app launching). Skip recovery-only apps (Boot Picker, Boot Recovery Assistant, Recovery Springboard, macEACS, Lock, KeyRecoveryAssistant, Startup Security Utility, Recovery Diagnose, Time Machine System Restore, Share Disk, FindMyMacMessenger, macOS Update Assistant, Device Recovery Assistant) unless they come for free. Third-party apps will need the full macOS 26 system (frameworks missing from BaseSystem), so moving to a full-OS shared cache is a coming step. Related: [[daylight-windowserver]], [[darling-apple-userland]].

Version policy (user, 2026-10-31): never follow minor macOS updates (26.x → 26.y); only move to new major versions (e.g. 26 → 27), each time with a fresh wsprobe trace from the user's Mac.
