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
