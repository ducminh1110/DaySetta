# Lead advice for D10 (from Claude Opus, verified)

## Root cause (verified with ipsw a2s + source)
- `0x180887958` = libsystem_pthread `__pthread_exit_if_canceled`. Check: `ipsw dyld a2s <cache> 0x180887958`; libsystem_pthread `__TEXT` = 0x180887000.
- Darling's libsystem_kernel is **plain arm64**. Its `xnu/libsyscall/custom/errno.c:77` (cerror) calls `_pthread_exit_if_canceled(err)` on **every failing cancellable syscall**. That goes through `xnu/libsyscall/wrappers/_libc_funcptr.c:124`, which does `_libkernel_functions->_pthread_exit_if_canceled(error)`.
- `_libkernel_functions` is filled in by Apple's **arm64e** libSystem init. On arm64e, the struct fields are declared with `LIBKERNEL_FUNCTION_PTRAUTH(f)` (`xnu/libsyscall/wrappers/_libkernel_init.h:23-29`), i.e. `__ptrauth(ptrauth_key_function / IA, address-diversified, ptrauth_string_discriminator("f"))`.
- So the stored pointers are PAC-signed. The plain-arm64 shim does a raw `blr` on them, the tagged address faults, and that is the SIGSEGV loop. It only shows up on paths where a syscall fails, e.g. EINTR/ETIMEDOUT in XPC/dispatch_mach waits. That is why plutil and cfprefsd stall, while the `dq` probe works.
- The `console_open` RPC in the loop is the crash/log path re-entering. It is not a separate bug.

## The fix (explicit emulation, no blanket stripping)
1. Find every function-pointer table that Apple arm64e code hands to Darling's plain-arm64 libsystem_kernel. In `xnu/libsyscall/wrappers/` look at:
   - `_libkernel_init.c/.h`: `_libkernel_functions`, `_libkernel_string_functions`, `_libkernel_voucher_functions`
   - `_libc_funcptr.c`: every `_libkernel_functions->X(...)` call
   - also check `pthread`/`bsdthread_register` callbacks (`start_wqthread`, `thread_start`, `pthread_clear_qos_tsd`) and dyld/libdyld callbacks.

   Use `git grep -n "_libkernel_.*functions->" xnu/libsyscall`.
2. At each call site in the plain-arm64 build, authenticate the pointer with the **same schema Apple used**:
   - key IA;
   - discriminator = `ptrauth_blend_discriminator(&table->field, ptrauth_string_discriminator("field"))`;
   - for unqualified C function pointers: key IA with discriminator 0.

   Do this through ds-exec's software PAC, the same engine and keys the guest uses. ds-exec lives in `/home/ubuntu/daysetta/src/exec` (`pac.*`). mldr exposes ds-exec to guest code via elfcalls: see `src/startup/mldr/dsexec_glue.cpp` and `elfcalls.h`, e.g. the existing `dsexec_lower` elfcall. Add a tiny elfcall `dsexec_auth_ia(ptr, modifier) -> ptr` (or reuse one if present). On auth failure it must report like other PAC failures (no silent strip).
3. Implement it as one macro in `_libc_funcptr.c`, `#if defined(__aarch64__) && DARLING && !__PTRAUTH_INTRINSICS__`, e.g. `LIBKERNEL_AUTH(table, field)`, and use it at every call site. Do not patch Apple binaries.
4. The string discriminator must match clang's `ptrauth_string_discriminator` (SipHash-2-4 of the name with a fixed key, truncated to 16 bits).
   - If ds-exec already has it, use it.
   - Otherwise compute the constants with clang (`__builtin_ptrauth_string_discriminator("_pthread_exit_if_canceled")` in a tiny arm64e test compiled with `-target arm64e-apple-macos`) and hard-code them per field, with a comment showing how each was obtained.
5. D8's "strip PAC at shim jump sites" is a stopgap. Replace it with auth wherever you touch the same path.

## Verify
- Add a unit test in `/home/ubuntu/daysetta/tests/test_exec.cpp` that signs with IA plus a blended discriminator and authenticates through the new helper. It must also check that a wrong discriminator fails.
- Run `plutil -p t.plist`. It must print the whole plist with rc 0. Then run the AppKit probe.
- Regressions:
  - `echo.s1 hi`
  - `plutil -help`
  - `daysetta_tests`
- Rebuild only the changed targets and install with targeted install. Back up first.
- Commit locally and update `D10-report.md`.

## Tools
- `ipsw dyld a2s` / `ipsw dyld extract` (already used).
- `llvm-objdump` on `/home/ubuntu/macos-re/m27x/libsystem_pthread.dylib`. The extracted file keeps cache vmaddrs, so use `--start-address=0x180887958`, not `0x958`.
- REA tools.
