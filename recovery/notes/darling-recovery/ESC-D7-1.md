# ESC-D7-1: Apple's cache libdispatch needs the kevent workqueue + workloops (missing in libsystem_kernel/darlingserver)

Run: genuine 27 `plutil -p t.plist` and the AppKit probe, Apple libdispatch from the 27 cache (Darling libdispatch removed from the load path):
  ds-exec: guest brk #0x1 pc=0x180704d34 (/usr/lib/system/libdispatch.dylib+0x39d34) lr=0x1806df18c (/usr/lib/system/libdispatch.dylib+0x1418c) x0=0x4000001f
Crash string at 0x18070c486: "BUG IN LIBDISPATCH: Missing Kevent WORKQ support". x0 = 0x4000001f = what our __bsdthread_register returns
(DARLING_PTHREAD_FEATURES: no WORKQ_FEATURE_KEVENT 0x40 / WORKQ_FEATURE_WORKLOOP 0x80).
The cache libdispatch has no env switch to disable the kevent workqueue mode (strings checked).

Missing kernel features (xnu-12377 semantics), none exist in emulation/ or darlingserver (grep kevent_id/workloop: only workq_kernreturn stubs):
 1. __bsdthread_register must return KEVENT|WORKLOOP (0x40|0x80).
 2. kevent_id (SYS 375) and kevent_qos with KEVENT_FLAG_WORKQ / KEVENT_FLAG_WORKLOOP: per-process workq kqueue and workloops (wlh) on libkqueue.
 3. workq_kernreturn: WQOPS_THREAD_KEVENT_RETURN, WQOPS_QUEUE_REQTHREAD_FOR_KEVENT, workloop thread requests (WQ_FLAG_THREAD_KEVENT/WORKLOOP,
    keventlist/nevents passed to _pthread_wqthread), WQOPS_SETUP_DISPATCH, workloop thread-request/QoS override/`bsdthread_ctl` for workloops.
 4. An event-to-thread bridge: when a kevent on the workq kq/workloop fires, a worker thread must enter wqueue_entry_point with the events.
Experiment (reverted, not committed): advertising 0x40|0x80 alone gets past the crash; the next stop is
  ds-exec: PAC authentication failure at pc=0x180889c88 word=0xd63f093f: poisoned 0x2000050503f4c4 (call through a pointer into the disk-loaded libsystem_kernel)
i.e. libpthread then drives workq_kernreturn/kevent paths we do not implement. Not done: needs a design (libkqueue worker thread that
calls the registered kevent worker func) before code. Owner's 26.6 libdispatch_apple (kernel-workqueue mode) is NOT an option (macOS 27 rule).
