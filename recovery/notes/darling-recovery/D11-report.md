# D11 report: 5× plutil -p in one container, all rc 0
Result: genuine macOS 27 `plutil -p t.plist` 5/5 in ONE container print the whole plist, rc 0 (was: run 2 hung).

## Root cause / fix (xnu a6dcfe12, superproject bump f058eaf18)
Darling consumed NOTE_WL_THREAD_REQUEST silently at dispatch (empty thread) and never delivered it. xnu `filt_wlprocess` delivers the thread-request knote to the bound thread; `EV_CLEAR` (set in `filt_wlattach`) consumes it on delivery. WLT trace: run 2 thread-request arrived before the machport event and was served with 0 events, then the machport event was dropped by libdispatch (no handler, no re-arm, no kevent_id calls). Fix in `wl_kevent.c`: store the thread-request kevent as a template (refreshed per touch, `filt_wlremember_last_update`), prepend it in `wl_fill` and clear `requested` on delivery; thread QoS = max bucket of request + queued events (`kqworkloop_override`); `wl_thread_return` re-runs while a request is still armed (`kqworkloop_end_processing`). `EV_DELETE` drops the knote with no delivery (`filt_wlallow_drop`).

## Verbatim 5-run output (clean build, trace code removed)
All 5 runs: `{ "list" => [ 0 => "a" 1 => 1 ] "n" => 42 "name" => "daysetta" }` (8 lines) + `rc=0` (each run individually verified; v1..v5).

## Regressions
container boots; echo.s1 rc 0; plutil.s1 -help rc 0; daysetta_tests 52/52; bench --no-cold: echo 0.77 s, plutil -help 1.2 s, plutil -p 1.87 s (was 2.67 s in D10), micro dispatch_async x1e6 0.328 s — no slowdown.

## Not done
- Owner/turnstile tracking still unmodelled (no NOTE_WL_END_OWNERSHIP/DISCOVER_OWNER/SYNC_IPC effects); synthetic thread-request ext[] copied from the change kevent, not from a real knote; sys_kevent_id still never returns queued events/data_out (not needed by current libdispatch path).
- x86_64 build not verified. No Apple binaries committed; nothing pushed.
- Provenance: xnu-12377 kern_event.c + black-box WLT trace only; no restricted material.
