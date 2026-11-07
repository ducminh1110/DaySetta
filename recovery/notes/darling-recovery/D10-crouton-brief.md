# Task D10 (crouton-alpha): genuine macOS 27 `plutil -p` must finish (cfprefsd / XPC path)

## Goal
`plutil.s1 -p t.plist` (the Apple 27 binary) prints the full plist and exits with rc 0. Today it prints `{` and then hangs.

## State
D9 is done. Read `/home/ubuntu/darling-recovery/D9-report.md`.
- Mach vouchers are real.
- BANK `SEND_PREPROCESS` in `mach_msg` works.

Where the hang is: libdispatch / libxpc talking to the 27 `cfprefsd`, or the reply path. Possible causes:
- the voucher on the reply;
- `MACH_RCV_VOUCHER`;
- the dispatch_mach reply port;
- workloop delivery of the reply. Workloop knotes are known not to be delivered to the workloop thread yet; see `KEVENT_WORKQ.md`.

The 27 `cfprefsd` is in store-27 at `/usr/sbin/cfprefsd`. A launchd job plist is at `~/.darling/Library/LaunchDaemons/com.apple.cfprefsd.xpc.daemon.plist`, root-owned. Check whether launchd actually started it and whether it answers.

## Reuse first (the owner's earlier x86 work, already merged in branch daysetta/unified)
- launchd XPC v5 pipe protocol in `src/external/launchd` / `xpc_pipe_v5.c`:
  - routine 718 `xpc_main`
  - 206 check-in
  - 301 vproc
  - lookup 804 → "w00t" check-in
- `mach_port_is_connection_for_service`.
- `~/darling-port-tools`: `mkdaemons.py`, `lastmsg.py`, `fpall.sh`, `dsh.sh`.
- Debug aids:
  - host file `/tmp/darling-machtrace`: logs every `mach_msg`.
  - `/var/log/xpc-v5.log` inside the prefix: `~/.darling/private/var/log`.
  - `DL4_ENV="ACTIVITY_LOG_STDERR=1"`.
- Docs:
  - `~/DARLING-MACOS26-HANDOFF.md`, the XPC and cfprefsd parts.
  - `/home/ubuntu/darling-recovery/KEVENT_WORKQ.md`, `D8-HANDOFF.md`, `D9-report.md`.

## Steps
1. Find exactly where `plutil -p` blocks. Use `fpall.sh` / gdb thread backtraces, symbolized against the 27 cache, and the mach trace. Find which message is sent, to which port, and which reply is awaited.
2. Fix the root cause in the emulation layer only: libsystem_kernel, darlingserver, launchd, mldr/ds-exec, dyl4. Follow xnu-12377 semantics. Apple images must stay unchanged, and they must come from the macOS 27 cache/store only. Do not swap in Darling-built libraries.
3. Test, with stdin from `/dev/null`:

   ```
   cd /home/ubuntu/darltest
   darling shell env DARLING_DYLINKER=/usr/lib/dyl4 DARLING_DS_CACHE=/home/ubuntu/macos-re/cache-arm64e/dyld_shared_cache_arm64e /Volumes/SystemRoot/home/ubuntu/darltest/plutil.s1 -p /Volumes/SystemRoot/home/ubuntu/darltest/t.plist </dev/null
   ```

4. If `plutil -p` works, also try the AppKit probe `/home/ubuntu/darltest/probe` (`NSApplicationLoad`) and record its result.
5. Regression checks: `echo.s1 hi` prints `hi`, `plutil.s1 -help` returns rc 0, and `/home/ubuntu/daysetta/build/daysetta_tests` passes all tests.
6. Remove all temporary debug prints. Commit locally in each changed repo on branch `daysetta/unified`, then bump the superproject. Never push.
7. Write `/home/ubuntu/darling-recovery/D10-report.md`, at most 30 lines: what you changed (files, commits), verbatim test output, what is not done, and the next blocker.

## Hard rules
- No `git reset --hard`, no `git clean`, no deleting files.
- Never `pkill -f` or `pgrep -f`. Kill by PID, using sudo for darling processes.
- Back up installed binaries before replacing them. `ninja install` of everything fails on MetalKit, so use targeted install targets.
- Never commit Apple binaries.
- Never stub success: implement real semantics or report.
- For any reverse engineering or disassembly, use the REA MCP tools (`rea_*`) or `objdump` / `llvm-objdump`. Never hand-decode.
- If the same blocker hits twice, stop and write it in the report.
