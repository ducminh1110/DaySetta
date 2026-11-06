---
name: feedback-ponytail-mode
description: User wants Ponytail (lazy senior dev) mode on: smallest complete change, terse replies, to save tokens
metadata:
  type: feedback
---
Work in Ponytail mode (full level): smallest complete change, reuse what exists, no unasked abstractions, terse replies ending with one or two lines on what was skipped/unchecked and any risk. Never cut validation, security, data-loss handling.

**Why:** user installed Ponytail on 2026-11-06 to save tokens before handing out the next tasks.
**How to apply:** skills live in ~/.claude/skills/ponytail* (copied from github DietrichGebert/ponytail @96608e0, SKILL.md only, no hooks/band). Invoke /ponytail at session start if it isn't active. Fits [[darling-xnu-port]] work: short Vietnamese reports.
