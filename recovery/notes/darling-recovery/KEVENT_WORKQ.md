# KEVENT_WORKQ: kevent workqueue mode for Apple's libdispatch (27 cache) on Darling aarch64

Why: Apple libdispatch 27 requires `_pthread_workqueue_init_with_workloop` with WORKQ_FEATURE_KEVENT (0x40), registers its
manager/sources on the process workq kqueue (`kevent_qos(.., KEVENT_FLAG_WORKQ)`) and expects ready events to arrive on workqueue
threads (`_pthread_wqthread(.., keventlist, flags|WQ_FLAG_THREAD_KEVENT, nkevents)` -> `kevent_func(&events,&n)` ->
`workq_kernreturn(WQOPS_THREAD_KEVENT_RETURN, events, n)`). xnu-12377 refs: bsd/kern/kern_event.c (kevent_qos_internal,
kqworkq_*), pthread/pthread_workqueue.c (workq_kernreturn, workq_thread_return), pthread/workqueue_internal.h (WQ_KEVENT_*).

Reuse (all in libsystem_kernel emulation, no darlingserver change):
 - libkqueue (in libsystem_kernel, Linux epoll) as the knote engine: the process workq kqueue is one hidden libkqueue kq.
 - existing sys_workq_kernreturn thread pool (parked threads, __darling_thread_create, wqueue_entry_point_asm_jump).
 - owner's merged bsdthread_register/ulock/timers unchanged.

Design
 1. `__bsdthread_register` returns PTHREAD_FEATURE_KEVENT (0x40) too. WORKLOOP (0x80) is NOT advertised: libdispatch then runs the
    non-workloop kevent-workq mode (it checks the feature bit), so EVFILT_WORKLOOP/kevent_id are out of scope for this step.
 2. `kevent_qos(.., KEVENT_FLAG_WORKQ)` ignores the kq argument (xnu), applies the changes to the hidden workq kq; with
    KEVENT_FLAG_ERROR_EVENTS the per-change errors are returned (EV_RECEIPT); never blocks; no events returned inline.
    The qos (incl. _PTHREAD_PRIORITY_EVENT_MANAGER_FLAG) of an added knote is remembered in a side table keyed (filter, ident).
 3. Delivery: a dispatcher pthread (created lazily with libpthread's pthread_create, so libkqueue/mutexes run on a real pthread)
    blocks in kevent64 on the workq kq. Ready events are copied (as kevent_qos_s, qos restored) into a pooled buffer
    (16 events + 32 KiB data, == WQ_KEVENT_LIST_LEN / WQ_KEVENT_DATA_SIZE) and a workq thread is requested with
    WQ_FLAG_THREAD_KEVENT (+ WQ_FLAG_THREAD_EVENT_MANAGER for manager-qos events) through the existing pool; the thread enters
    start_wqthread with (keventlist, flags, nkevents).
 4. `workq_kernreturn(WQOPS_THREAD_KEVENT_RETURN, events, n)`: apply the n returned changes to the workq kq (errors ignored, as xnu),
    free the buffer, poll the kq without blocking; if more events are ready the thread re-enters start_wqthread at once
    (WQ_FLAG_THREAD_REUSE|KEVENT) with them, else it parks like WQOPS_THREAD_RETURN.
 5. Cross-ABI rule (arm64e Apple code <-> plain arm64 shim), explicit and tested:
    - Apple -> shim: an unsigned code pointer into a registered plain-arm64 executable range passes `blraa/braa` (ds-exec
      `plain_range_add/contains`, dyl4 registers every non-arm64e disk-loaded image through elfcalls->dsexec_plain). Signed-but-wrong
      pointers and unsigned pointers outside the ranges still fault. Test: exec_cross_abi_unsigned_pointer_into_plain_range.
      (The 0x180889c88 failure: libkqueue called libpthread `pthread_once(&once, fn)` with a plain `fn`; libpthread does blraaz.)
    - shim -> Apple: the shim strips PAC (mask 48 bits) from pointers it jumps to (bsdthread_register thread_start / wqthread).
 6. Not covered: EVFILT_MACHPORT receive buffers (data_out), workloops (kevent_id, EVFILT_WORKLOOP, WQOPS_THREAD_WORKLOOP_RETURN),
    per-QoS thread-request accounting (one pool, like the existing emulation).
