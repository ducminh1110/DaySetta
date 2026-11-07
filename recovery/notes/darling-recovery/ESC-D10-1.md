# ESC-D10-1: second `plutil -p` in one container hangs (not solved)
Fresh container: always rc 0. Second run, same container (cfprefsd already up): plutil stops after `{`.
Tried 3 distinct hypotheses, all negative; stopping per rule.

## Verified facts (temp tracing, removed)
- Run 1 per connection: listener workloop gets the w00t event -> thread does is_connection_for_service (MIG 3240), logs
  "activating connection ... peer", re-enables the listener knote (EV_ENABLE|EV_DISPATCH), later no-senders (id 70) -> peer cancelled,
  thread request on the listener workloop (NOTE_WL_THREAD_REQUEST), served with nch=0.
- Run 2: kqchan event for the listener port arrives (wl_poll got=1, ext1=0x34), libdispatch receives w00t via the TOO_LARGE re-receive
  (mach_msg ok, id 0x77303074 size 52, 2 port descriptors, valid names, trailer fine), thread returns nch=0: NO MIG 3240, NO
  "activating connection" log, NO re-enable, NO thread request. libxpc silently drops the message ("Normal message received by listener
  connection. Ignoring."? or earlier gate in the listener check-in path; __xpc_connection_validate_check_in_is_for_service @0x180573b90
  was not reached/ called with early-true path).
- State at hang: all workloops requested=0 running=0 nev=0; cfprefsd threads parked normally; no SIGTERM, no listener cancel seen.
## Negative experiments
1. re-arm: change list on WORKLOOP_RETURN is applied (EV_ENABLE|EV_DISPATCH on the listener, n=0 err=0) - fine.
2. delivered event had stray EV_RECEIPT (0x40) from our change flag: stripped (kept, correct); no effect.
3. parked-thread reuse: forced workloop threads to exit instead of park: still hangs.
## Side finding fixed
`_dlsym` auth used &_dlsym instead of &fns->dlsym as modifier -> "PAC auth failure ... modifier=0x59fd..." in every process; fixed.
## Next idea
Disassemble the libxpc listener check-in path (callers of 0x180573b90 are via cache stubs; start from "Normal message received by
listener connection" / xpc listener dispatch_mach handler) to find which per-listener state flag (conn+0x18/+0xd0/+0x108) differs after
the first peer; compare with darlingserver port state (receive right of listener, no-senders/dead-name notification requests,
port-destroyed). Also consider launchd side: second lookup returns "lookup ok" vs first "checkin".

## Lead (Opus) investigation, 2026-11-07 (after Sonnet)
Tools added: env-gated WL trace (touch /tmp/darling-wltrace -> "WLT chg/ev" lines on the daemon's STDOUT, e.g. ~/.darling/private/tmp/cfprefsd.out).
Facts (libxpc/libdispatch 27 decompiled with REA; addresses from the 27 cache):
- libxpc __xpc_connection_mach_event (0x180556f94) is NOT reached for the 2nd w00t (gdb sw-bp and hw-bp both: no hit), so libxpc gates
  (format check 0x1805574b8.., check_peer_requirement, evaluate_check_in, "Bogus check-in") are not the cause.
- The w00t IS received by libdispatch: _dispatch_kevent_mach_msg_drain (0x1806f1798) re-receives with _mach_msg2 (opts 0x107000b0a, aux vector)
  and machtrace shows "done -> 0". gdb hit kev_drain once; later bps (recv 0x1806f1dec, merge 0x1806e6bbc, handle_or_push 0x1806e6e00,
  lane_push 0x1806d8684) were not observed (gdb loses new workq threads under Darling: sw-bp follow-ups and hw-bps unreliable).
- Delivered event for the 2nd client is correct: same ident (listener port), same udata (listener unote 0x501018A00) as run 1; owner dm
  (== workloop id) is NOT cancelled (dm+0x50 = 0x00400001, DSF_CANCELED clear); dm has no queued items afterwards.
- Ordering difference: run 1 = event first, then libdispatch thread request (NOTE_WL_THREAD_REQUEST) and later EV_ENABLE re-arm.
  run 2 = thread request for the listener workloop arrives FIRST (peer teardown work), the event arrives while that thread runs and is
  handed back on WQOPS_THREAD_WORKLOOP_RETURN (re-entry via wqueue_entry_point_asm_jump), then NO further changes (no re-arm, no request).
- Experiment: serving the pending event with a fresh workqueue thread instead of re-entry: still hangs (run2), but a later run succeeded
  -> timing/race in the workloop thread-request/ownership model (no owner/turnstile tracking; KEVENT_WORKQ.md says so).
Next: model xnu kqworkloop ownership + thread-request state exactly (kqworkloop_begin/end_processing, NOTE_WL_THREAD_REQUEST /
NOTE_WL_END_OWNERSHIP / NOTE_WL_SYNC_* semantics, KQ_WL_PROCESSING), and check what _mach_msg2 returns to userspace for the aux vector
(MACH64_RCV aux data, msgh size/trailer) on the re-receive path vs run 1.
