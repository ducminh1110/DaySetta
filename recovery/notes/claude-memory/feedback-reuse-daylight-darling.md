---
name: feedback-reuse-daylight-darling
description: "Every DaySetta subagent brief must point at the matching Daylight docs/code, dyl4 (~/dyld-1378) and the owner's Darling fork before any new implementation"
metadata:
  node_type: memory
  type: feedback
  originSessionId: 0b182f4a-fdf5-457c-bf1f-27afc98a81e7
  modified: 2026-11-06T10:03:30.679Z
---

Owner (2026-11-06) was annoyed that I briefed R3 (Darwin layer + dyld for /bin/echo) as if from scratch. The owner wrote dyl4 (~/dyld-1378, Apple dyld ported for Daylight), and the Darling fork (~/darling, xnu-12377 port) already ran Apple /bin/echo long ago.

**Why:** DaySetta is the continuation of Daylight; rediscovering solved problems wastes tokens and time.
**How to apply:** In every brief, list the relevant ~/daylight/docs (basesystem-apps.md fix list, docs/knowledge/*), ~/DARLING-MACOS26-HANDOFF.md, ~/darling, ~/darling-port-patches and ~/dyld-1378 paths. Also point at existing disassembly/RE output (~/macos-re/re, ~/macos-re/dyldinfo-27, ~/macos-re/traces, daylight API_INDEX/internals) and tell agents to use the REA MCP tools (ToolSearch "+rea"), objdump/llvm-objdump or Ghidra instead of hand-decoding (owner repeated 2026-11-06: every agent must be told this); always include the ponytail block. Require a "Reuse" table in the subagent's report, and reject new code that comes without a reason it couldn't reuse. Links: [[daysetta-owner-decisions]], [[darling-apple-userland]].
