---
name: feedback-pkill-own-session
description: Never pgrep/pkill -f with a pattern that also appears in the ssh command being run (it kills the session)
metadata:
  node_type: memory
  type: feedback
  originSessionId: 2b408220-078e-4ac0-8ea9-b8330b14818c
  modified: 2026-10-31T15:02:07.281Z
---

When killing or waiting on processes on the x86 box over `~/bin/x86 '...'`, never use `pkill -f`/`pgrep -f` with a pattern that also occurs in that same command line (e.g. `pgrep -f "sh inputtest.sh"`, `pkill -f "while pgrep"`): the remote `bash -c` carrying the command matches, so the loop never ends or the session kills itself. Happened three times on 2026-10-31.

**Why:** lost work and hung tool calls; one 600 s call had to be stopped.

**How to apply:** anchor patterns (`pgrep -f "^sh inputtest.sh"`, `"^/Volumes/SystemRoot.*App.app/Contents/MacOS"`), or kill by PID from `ps` output. Related: [[darling-apple-userland]].
