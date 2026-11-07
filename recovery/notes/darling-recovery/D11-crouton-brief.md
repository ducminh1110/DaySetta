# Task D11 (crouton-alpha): exact xnu kqworkloop thread-request/ownership model in Darling's workloop emulation

## Goal
Run genuine macOS 27 `plutil -p t.plist` 5 times in a row inside ONE Darling container. All 5 runs must exit with rc 0 and print the whole plist. Today only run 1 works: from run 2 on, cfprefsd receives the check-in but never processes it.

## Read first
- `/home/ubuntu/darling-recovery/ESC-D10-1.md`. Read the whole file, especially the section "Lead (Opus) investigation". It holds the verified facts and the root-cause area.
- `/home/ubuntu/darling-recovery/KEVENT_WORKQ.md` and `D10-report.md`.
- The emulation code:
  - `src/external/xnu/darling/src/libsystem_kernel/emulation/src/xnu_syscall/bsd/impl/kqueue/wl_kevent.c` (workloop model; a dispatcher thread feeds `evq`; `wl_thread_return` re-enters the thread)
  - `.../bsdthread/workq_kernreturn.c` (`WQOPS_THREAD_WORKLOOP_RETURN`, thread requests, parking)
- Ground truth is xnu-12377 in `/home/ubuntu/darling/src/external/xnu/bsd/kern/kern_event.c`. Study:
  - `kqworkloop_begin_processing`, `kqworkloop_end_processing`, `kqworkloop_update_threads_qos`
  - `filt_wlattach`, `filt_wltouch`, `filt_wlupdate`
  - `NOTE_WL_THREAD_REQUEST`, `NOTE_WL_SYNC_WAIT/WAKE/IPC`, `NOTE_WL_END_OWNERSHIP`, `NOTE_WL_DISCOVER_OWNER`, `NOTE_WL_UPDATE_QOS`
  - `KQ_WL_PROCESSING` / `KQ_R2K_ARMED`
  - and in `bsd/pthread/workqueue.c`: `workq_kern_threadreq_*` and `WORKLOOP_RETURN` handling.

## Verified facts (do not re-investigate)
- The libxpc handler is never called for the 2nd check-in. libdispatch's `_dispatch_kevent_mach_msg_drain` does re-receive the message (`_mach_msg2`, rc 0).
- The delivered event is correct: the right listener port and udata, and the owning dispatch_mach object is not cancelled.
- The difference from run 1 is ordering:
  1. A `NOTE_WL_THREAD_REQUEST` for the listener workloop arrives before the event.
  2. A thread serves it with 0 events.
  3. The event arrives while that thread runs and is handed back via `WORKLOOP_RETURN` re-entry.
  4. After that, libdispatch issues no more changes: no re-arm, no new thread request.
- Experiment: serving pending events with a fresh thread instead of re-entry still hangs, though occasionally it works. This points to a race or semantic gap in the thread-request/ownership state machine, which the current emulation does not model ("no owner/turnstile tracking").

## Approach
1. List exactly what libdispatch expects from the kernel for workloops, comparing xnu code against what `wl_kevent.c` does. Cover:
   - when a thread request is considered consumed;
   - what happens to `NOTE_WL_THREAD_REQUEST` knotes on `EV_DELETE` / `NOTE_WL_END_OWNERSHIP`;
   - re-delivery when processing ends with pending events or a still-armed request (`kqworkloop_end_processing`: if the thread request is still present, or events are pending, the kernel must re-run the workloop);
   - the qos of the thread;
   - what `kevent_id` returns in the out events and `data_out`.
2. Fix the emulation to match xnu semantics. Keep the code small and comment each rule with the xnu function it mirrors.
3. Use the trace while debugging: `touch /tmp/darling-wltrace` writes `WLT` lines to the daemon's STDOUT (`~/.darling/private/tmp/cfprefsd.out`). os_log of cfprefsd goes to `/tmp/cfprefsd.log`. Remove `/tmp/darling-wltrace` when done.

## Build and install
```
cd ~/darling/build-aarch64 && ninja system_kernel system_kernel_static64
R=/usr/local/libexec/darling
sudo cp src/external/xnu/darling/src/libsystem_kernel/libsystem_kernel.dylib $R/usr/lib/system/libsystem_kernel.dylib.new
sudo mv -f $R/usr/lib/system/libsystem_kernel.dylib.new $R/usr/lib/system/libsystem_kernel.dylib
```
Back up the installed file first.

## Clean container between experiments (important; stale processes cause false hangs)
1. Kill `plutil` and `darling shell` processes by PID, with `sudo kill -9`.
2. Run `darling shutdown </dev/null`.
3. Kill any remaining `mldr` and `darlingserver`: `pgrep -x mldr | xargs -r sudo kill -9` (and the same for `darlingserver`). Use exact-name pgrep only; never `pgrep -f`.

## Test
```
cd /home/ubuntu/darltest
for i in 1 2 3 4 5; do timeout 60 darling shell env DARLING_DYLINKER=/usr/lib/dyl4 DARLING_DS_CACHE=/home/ubuntu/macos-re/cache-arm64e/dyld_shared_cache_arm64e /Volumes/SystemRoot/home/ubuntu/darltest/plutil.s1 -p /Volumes/SystemRoot/home/ubuntu/darltest/t.plist </dev/null; echo rc=$?; done
```
`timeout` cannot kill the setuid `darling shell`, so kill hung runs by PID.

## Regressions
- container boots
- echo.s1 prints hi
- plutil.s1 -help gives rc 0
- `/home/ubuntu/daysetta/build/daysetta_tests` all pass
- `/home/ubuntu/daysetta/tools/bench/run.sh`: no big slowdown

## Hard rules
- No `git reset --hard`, no `git clean`, no deleting files.
- Never commit Apple binaries.
- Never stub success; implement real xnu semantics or report.
- Reverse engineering only with the REA MCP tools (`rea_*`) or objdump/llvm-objdump; never hand-decode.
- Commit locally on branch `daysetta/unified` (xnu submodule plus a superproject bump). Never push.
- Write `/home/ubuntu/darling-recovery/D11-report.md`, ≤30 lines: changes, verbatim 5-run output, regressions, what is not done.
- If the same blocker hits twice, stop and report.
