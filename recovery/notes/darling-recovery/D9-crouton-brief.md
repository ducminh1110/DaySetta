# Task D9 (crouton-alpha): Mach vouchers for Apple libdispatch on Darling aarch64

## Goal
Genuine macOS 27 `plutil -p t.plist` (Apple binary) must print the plist instead of crashing with
"BUG IN CLIENT OF LIBDISPATCH: Could not create task mach voucher".

## Context
- **Darling fork:** `/home/ubuntu/darling`, branch `daysetta/unified`. Builds in `/home/ubuntu/darling/build-aarch64` (ninja) and installs to `/usr/local` with sudo.
- **Kernel shims:**
  - `src/external/xnu`, branch `daysetta/unified`: the libsystem_kernel / libsyscall Mach traps live under `darling/src/libsystem_kernel`.
  - `src/external/darlingserver`: a userspace Mach IPC server; it uses xnu-12377 "duct-tape" code in `duct-tape/`.
- **Apple's libdispatch** (from the macOS 27 shared cache) calls `host_create_mach_voucher` (mach trap 70). Today trap 70 returns KERN_FAILURE, so libdispatch aborts.
  - Returning a fake send right fails later with "Voucher port corruption".
  - Read `/home/ubuntu/darling-recovery/ESC-D8-1.md` and `D8-HANDOFF.md` first.
- **What real XNU provides** (xnu-12377: `osfmk/ipc/ipc_voucher.c`, `osfmk/bank/`, `osfmk/ipc/ipc_importance.c`, `osfmk/atm/`):
  - voucher ports as kernel objects (IKOT_VOUCHER);
  - attribute managers for BANK / IMPORTANCE / ATM / USER_DATA / PTHPRIORITY;
  - the `mach_voucher_extract_attr_recipe` / `mach_voucher_attr_command` traps (71–73 area);
  - the `msgh_voucher_port` in `mach_msg`.

  darlingserver already has `ipc_voucher` code but no attribute managers.
- **Owner's earlier work:** the owner's x86 tree avoided this with `VOUCHER_USE_EMPTY_MACH_BASE_VOUCHER` in a self-built libdispatch. We cannot do that: Apple's libdispatch from the cache must be used unchanged.

## What to do
1. **Read first.** Read darlingserver's duct-tape voucher code and the xnu-12377 sources in `src/external/xnu/osfmk`.
2. **Implement trap 70 properly.** Prefer compiling the real xnu voucher and attribute-manager code into duct-tape over writing new code. Otherwise write a minimal faithful version: create a real voucher kobject port, honour the recipes libdispatch sends (BANK / IMPORTANCE / PTHPRIORITY / USER_DATA), and support the extract and command traps that libdispatch uses.
3. **Rebuild and install.** Rebuild darlingserver and libsystem_kernel (`ninja -C build-aarch64 <targets>`), then `sudo ninja -C build-aarch64 install`. Before installing, back up `/usr/local/libexec/darling/usr/lib/system/libsystem_kernel.dylib` and the darlingserver binary. Restart with `sudo darling shutdown`.
4. **Test.** Run inside `/home/ubuntu/darltest`:

   ```
   darling shell env DARLING_DYLINKER=/usr/lib/dyl4 DARLING_DS_CACHE=/home/ubuntu/macos-re/cache-arm64e/dyld_shared_cache_arm64e /Volumes/SystemRoot/home/ubuntu/darltest/plutil.s1 -p /Volumes/SystemRoot/home/ubuntu/darltest/t.plist </dev/null
   ```

   Create `t.plist` if missing. Always redirect stdin from `/dev/null`.
5. **Check for regressions.** Same env, run:
   - `echo.s1 hi` (must print `hi`);
   - `plutil.s1 -help` (rc 0);
   - `/home/ubuntu/daysetta/build/daysetta_tests` (all pass).
6. **Commit locally** in each changed submodule on branch `daysetta/unified`, then bump the superproject. Never push.
7. **Write `/home/ubuntu/darling-recovery/D9-report.md`** (≤30 lines): what changed (files and commits), test output verbatim, and what is not done.

## Hard rules
- No `git reset --hard`, no `git clean`, no deleting files.
- Do not touch `/home/ubuntu/daylight`, `/home/ubuntu/macos-re/cache-arm64e`, or any IPSW.
- Never use `pkill -f` / `pgrep -f`; kill by PID.
- Never commit Apple binaries.
- Do not stub success. If libdispatch needs a feature, implement real semantics or stop and explain in the report.
- If the same blocker hits twice, stop and write it in the report.

## Reverse engineering (owner rule)
For any disassembly or reverse engineering, use the REA MCP tools (`rea_*`: open_binary, procedure_assembly, procedure_pseudo_code, xrefs, inspect_with_objdump) or `objdump`/`llvm-objdump`. Never hand-decode instruction words or write ad-hoc decoders.
