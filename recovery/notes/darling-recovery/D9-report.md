# D9 report — Mach vouchers (trap 70)
## What changed
- darlingserver `deceaaf`: RPC wrappers for traps 70/72; compiled real xnu
  `bank.c`, `voucher/ipc_pthread_priority.c`, `kern/ledger.c` into duct-tape;
  `bank_init()`+`ipc_pthread_priority_init()` in `dtape_init`;
  `bank_zones_init()` (ZONE_DECLARE startup never runs in duct-tape);
  per-task `bank_task_initialize()` at task create (mirrors xnu
  `task_bank_init()`); `KHEAP_DEFAULT` for `KHEAP_TEMP`.
- xnu `ec5b678d`: traps 70/72 forward to darlingserver RPC (were KERN_FAILURE).
- Superproject `89ac983e5` bumps both submodules.

## Stubs (mirror xnu; voucher ops run real xnu code)
Single anonymous persona (id 0, `unique_persona=0`); ledger boundary = no pmap
ledgers (LEDGER_NULL => nothing billed, same as xnu for template-less tasks);
thread_group no-ops; `task_is_app`=FALSE; priority normalize identity.

## Test output (verbatim)
- `plutil.s1 -p t.plist`: before = `BUG IN CLIENT OF LIBDISPATCH: Could not
  create task mach voucher`. After: create returns code 0 + real IKOT_VOUCHER
  port; BANK SEND_PREPROCESS adoption in mach_msg succeeds (server log:
  `send_preprocessing kr=0 need=1`). It prints `{` then HANGS (no further
  output in 90s), no abort anymore.
- `echo.s1 hi` -> `hi`, RC=0. `plutil.s1 -help` -> usage, RC=0.
  `daysetta_tests` -> `51/51 tests passed`.

## Not done
plutil -p still hangs after `{`; remaining blocker is the XPC exchange with
the 27 cfprefsd daemon (mach_msg voucher reply path), not voucher creation.
Full `ninja install` blocked by pre-existing MetalKit link failure; used
targeted installs for the two components.
