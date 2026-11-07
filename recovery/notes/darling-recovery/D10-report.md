# D10 report: genuine plutil -p t.plist works (fresh container)
Result: `plutil.s1 -p t.plist` prints the whole plist, rc 0, 5/5 fresh containers:
`{ "list" => [ 0 => "a" 1 => 1 ] "n" => 42 "name" => "daysetta" }` (8 lines).

## Chain of blockers fixed
1. PAC-tagged `_libkernel_functions` calls (crouton, KEPT, verified): xnu 9038299f. Fault loop at libsystem_pthread+0x958 is gone
   (worker threads park normally). mldr elfcalls `dsexec_auth_ia` / `dsexec_is_arm64e` (superproject d135977ba). is_arm64e = main image
   arm64e OR DARLING_DS_CACHE set (cache libSystem is arm64e even for plain exes). Unit test exec_libkernel_function_ptr_auth_schema
   (daysetta, uncommitted-before, now committed): daysetta_tests 52/52.
2. Workloop knotes never delivered (the real hang): wl_kevent.c kept EVFILT_MACHPORT knotes of a workloop on a private kq nobody
   polled, so cfprefsd never saw plutil's request. Added a wl dispatcher pthread (one epoll over all workloop kqs) that drains ready
   events into wl->evq and requests a WQ_FLAG_THREAD_WORKLOOP thread (xnu 78f5b70f). Events arriving while the thread runs are
   re-handed on WQOPS_THREAD_WORKLOOP_RETURN.
3. darlingserver aborted on first received BANK voucher: `assert(ba_thread_group != NULL)` in bank_account_alloc_init because
   task_coalition_get_thread_group() returned NULL. Now one anonymous system group (darlingserver 14b9966).
Superproject bump: d135977ba. Installed: libsystem_kernel.dylib, /usr/local/bin/darlingserver (backups /tmp/claude-1001/bk).

## Regressions
container boots (`echo ok`, uname = Darwin 26.2.0, bash ok); echo.s1/echo.s2/echo-fat -> `hi`; `plutil.s1 -help` rc 0;
daysetta_tests 52/52; bench: plutil -help 1.2 s, echo 0.77 s, cold echo 15.2 s, plutil -p 2.67 s rc=0 (no slowdown vs D8).

## Not fixed / next blockers (verbatim evidence)
- plutil -p a SECOND time in the same container hangs after `{` (fresh container always OK; slower runs, e.g. DSERVER_LOG_LEVEL=debug,
  also hang the first time, so it is timing/state dependent). Strace of cfprefsd: run 2's `w00t` (id=1999646836 size=52) is received
  on the listener workloop (tid 43), the thread returns at once (no follow-up work, unlike run 1), plutil waits in
  `mach_msg rcv` on its reply port forever. Run 1 ends with a dead-name notification (id=70). Suspect: workloop thread-request /
  deferred-change state after the first connection (NOTE_WL_THREAD_REQUEST, rearm via WORKLOOP_RETURN changelist) or dead-name
  handling; not yet isolated. wl state at hang: all workloops requested=0 running=0 nev=0, knotes of kq 9/13 disabled.
- AppKit probe (`probe/probe`, dlopen AppKit + NSApplicationLoad): blocks (no output; stdout buffered) in
  `mach_msg(opt=700020E rcv=2403)` after launchd lookups `com.apple.tccd.system`, `com.apple.pasteboard.1`,
  `com.apple.distributed_notifications@Uv3` all "lookup fail" (launchd-lookups.log). Needs those services or a proper
  lookup-failure -> XPC invalid-connection path.
- pthread_canceled (darlingserver) is still `TODO ... -ENOSYS`; xnu returns EINVAL when nothing is pending. Harmless for now but
  called after every EINTR.
- Not verified: arm64 vs x86 builds of the touched files; wl_dispatcher with fork(); WL_EVQ=32 overflow path.
