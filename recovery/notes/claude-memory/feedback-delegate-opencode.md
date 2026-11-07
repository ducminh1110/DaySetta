---
name: feedback-delegate-opencode
description: Owner is cost-constrained — delegate routine coding to OpenCode models via the claudelegate MCP; Claude subagents only for hard tasks
metadata:
  type: feedback
---

Owner (2026-11-07) says budget is very tight. Use the `claudelegate` MCP (user-scope in ~/.claude.json, bunx @lehcode/claudelegate@0.3.1, opencode v2.2.5 at ~/.local/node/bin) for any coding task an external model can handle (boilerplate, scripts, tests, mechanical ports, docs). Claude Agent subagents (Sonnet/Opus) only for hard tasks: PAC/ds-exec, dyld, kernel/darlingserver semantics, tricky debugging.

**Why:** Claude subagent tokens are expensive for the owner.
**How to apply:** Call `delegate_async` / `task_*` from the main thread (no plugin routing hook installed, so main-thread calls are allowed). Write a self-contained brief, then review the git diff and run the tests yourself. Default model `kilo/stealth/crouton-alpha` (owner's choice; Kilo Gateway provider `kilo` in ~/.config/opencode/opencode.json, key in ~/.config/kilo.key chmod 600, verified OK 2026-11-07). Fallback: `deepseek-inferx/deepseek-v4.1-flash-1017-basic` (verified). github-copilot/* returned "Model unavailable"; beatapi and agentrouter models timed out. Output is capped at 8000 chars. The MCP loads only after a session restart. Rules from [[feedback-reuse-daylight-darling]] and [[daysetta-owner-decisions]] still go in every brief.

Freebuff (2026-11-07): `freebuff` 0.2.36 is installed and the owner is logged in (~/.config/manicode). It is an interactive TUI only, with no headless or API mode. Headless use through @codebuff/sdk fails in every variant tried: the store agent returns 402 Payment Required, then 410 Gone with costMode 'free', and the bundled `base2-free` agent is rejected as an invalid agent id. Free usage seems to work only inside its own (ad-supported) TUI session. Don't wire it into delegation; the owner can use it by hand. Test code is in ~/tools/freebuff-mcp.
REA MCP is also registered in OpenCode (~/.config/opencode/opencode.json mcp.rea) since 2026-11-07, so delegated OpenCode runs can use REA. Every OpenCode brief needs the REA/objdump line.

Escalation rule (owner, 2026-11-07): if crouton or another OpenCode model really can't do a step, or is going mostly in the wrong direction (stuck after lead advice, repeated wrong fixes, breaking the container), hand that step to a Claude Sonnet subagent. Give it a brief that carries over the crouton diff, its report and the lead's diagnosis.
