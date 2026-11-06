# R3 requirements record: what genuine macOS 27 dyld + libSystem init touch

Status: R3 NOT reached. `ds-run .../bin/echo hello from macOS 27` exits 133 after a genuine
`brk #0xb001` in libsystem_platform (see "Blocker"). This file is the deliverable: everything the
genuine 27 userland asked of the kernel/host layer, for whichever Darwin runtime (Darling aarch64)
has to provide it. All entries OBSERVED by `DS_TRACE=1` (log: artifacts/exec/r3-echo.txt) unless a
SOURCE cite is given. xnu cites are the Darling xnu snapshot
(`/home/ubuntu/darling/src/external/xnu`, NOT xnu-12377; numbers above its table were never seen).
`bsd/kern/syscalls.master` = SM, `osfmk/kern/syscall_sw.c` = SW.

Binary facts (OBSERVED, llvm-otool): store `usr/lib/dyld` and `bin/echo` are fat: x86_64,
arm64e (cpusubtype 0x80000002, the one used), and a third slice with cpusubtype 0x8000000C.
Genuine dyld (arm64e): `MH_DYLINKER`, preferred base 0, `LC_UNIXTHREAD` pc 0x4bf0 (= `__dyld_start`,
dyld-1378 dyld/dyldStartup.s:77), no LC_MAIN; it self-relocates (chained fixups with
`__AUTH_CONST`/`__TPRO_CONST`), so the loader must NOT apply fixups. echo: `LC_MAIN` entryoff
1264, `__PAGEZERO` 4 GiB, link base 0x100000000, `LC_LOAD_DYLINKER /usr/lib/dyld`.

## 1. Entry contract (kernel -> dyld)

| Item | Requirement | Source |
|---|---|---|
| Registers | only sp/pc set; `__dyld_start` does `mov x0, sp` | dyld-1378 dyld/dyldStartup.s:77-95 |
| Stack | `sp-> mh, argc, argv[], 0, envp[], 0, apple[], 0, strings` | dyldStartup.s header comment |
| apple[] keys used | `executable_path=`, `ptr_munge=0x..`, `stack_guard=0x..` (byte 1 zeroed), `malloc_entropy=0x..,0x..`, `main_stack=` (4 values) | xnu bsd/kern/kern_exec.c:6936-6949, 6984, 7134-7150 |
| apple[] keys NOT given | `ptrauth_disabled=1` (exists in xnu, deliberately omitted: owner rule), `pfz=`, `th_port=`, `executable_file=`/`dyld_file=` (fsid/objid), `executable_cdhash=`, `dyld_flags=`, `executable_boothash=`, `security_config=`, `HardenedRuntime=` (kern_exec.c:6990-7010) | not yet needed |
| dyld mapping | on-disk dyld is loaded at an arbitrary 16 KiB-aligned slide; it then calls `restartWithDyldInCache` (`br x3`, dyldStartup.s:~120) and the cache copy runs (OBSERVED: second `shared_region_check_np`, `/usr/lib/dyld` vs `dyld` names in log) | dyld-1378 |

## 2. Shared region (cache already mapped by the host)

| Item | Requirement | Evidence |
|---|---|---|
| `shared_region_check_np(uint64_t*)` (294, SM:445) | return 0 and write the address of the `dyld_cache_header`; dyld validates magic, reads `dynamicRegion()` | SM:445; dyld-1378 dyld/SharedCacheRuntime.cpp:1030-1050 |
| second call with x0 = `0xffffffffffffffff` (OBSERVED once, at dyld+0x644c after libsystem_kernel init began) | must return EFAULT (14), not crash the host | OBSERVED |
| Dynamic config page | 16 KiB at `header.dynamicDataOffset` (header+0x1f0; 27 cache: 0x1b9b10000, max 0x4000): `"dyld_data    v3"`, FileIdTuple {fsid.val[0]=st_dev,val[1]=0, fsobjid=st_ino}, osCryptexPathOffset, cachePathOffset(=80), pad, two u128 variant-flag words, path string | common/DyldSharedCache.h:191-241, .cpp:2134-2310 |
| Without nonzero FileIdTuple | dyld halts "dyld shared region dynamic config data was not set" | dyld/DyldProcessConfig.cpp:1520-1535 (OBSERVED before the fix) |
| `fsgetpath(buf,size,fsid_t*,objid)` (427, SM:645) | must resolve the FileIdTuple to the cache file path; return length incl. NUL (OBSERVED: `= 60`) | xnu bsd/vfs/vfs_syscalls.c:14351-14410 (build_path length); dyld FileIdTuple::getPath .cpp:2305 |
| Executable cache pages outside every image `__TEXT` section (branch islands, e.g. exec at 0x1980f6d30) | exist and are executed; they need the same instruction lowering | OBSERVED (SIGSEGV exec fault, fixed in cache_map.cpp lower_page) |
| `map_with_linking_np` (550, SM:866) | dyld calls it for page-in linking of the main exe; ENOSYS makes dyld "do the fixups itself" (OBSERVED continuing). Darling's source forces `pageInLinkingMode=0` instead | dyld/Loader.cpp:2143-2200; DyldProcessConfig.cpp:512-545 |

## 3. BSD syscalls touched (x16 > 0; success: carry clear, x0 = value; error: carry set, x0 = errno)

| # | name (SM line) | Args seen / expectation | Our answer |
|---|---|---|---|
| 38 | sys_crossarch_trap (SM:92) | x0 = small int; x6 calls. Probably "am I translated" probe | ENOSYS (UNIMPLEMENTED) - dyld continues |
| 294 | shared_region_check_np (445) | see section 2 | implemented |
| 427 | fsgetpath (645) | see section 2 | implemented (registered ids only) |
| 483 | csrctl (743) | op 0 (CSR_CHECK, size 4) and op 1; xnu bsd/kern/kern_csr.c:345-405, bsd/sys/csr.h:93-96 | SIP enabled: CHECK(mask!=0) = EPERM, config 0 |
| 381 | __mac_syscall (563) | policy ptr, call 2 (x4) and 0x5a; dyld reads sandbox/AMFI answers | ENOSYS (explicit stub) |
| 202 | sysctl (298) | `{0,3}` name2oid (kern_newsysctl.c) for `kern.bootargs` -> `{1,100}` (our dynamic OID) then read; `security.mac.lockdown_mode_state` name2oid -> ENOENT (dyld tolerates). dyld aborts "failed to read boot args: 78" if the read fails | kern.bootargs = "" only |
| 336 | proc_info (513) | once, after libSystem init started | UNIMPLEMENTED (ENOSYS) |
| 20 | getpid | dyld + libsystem_kernel | host pid |
| 372 | thread_selfid (553) | first call at dyld+0x4d40 | gettid |
| 5 / 463 | open / openat (50, 717) | `open(exe)`, `open("/", O_DIRECTORY\|O_NOFOLLOW_ANY=0x20100000)`, then `openat(fd,"System/Cryptexes/OS")`, `.../System/Library/dyld/` and 5 absolute cryptex/preboot/Rosetta cache-dir candidates, all ENOENT (dyld tolerates). O_NOFOLLOW_ANY needs openat2(RESOLVE_NO_SYMLINKS) | implemented |
| 338 | stat64 (519) | 45 calls: the main exe, then every cache dylib path (`/usr/lib/libSystem.B.dylib` ...): override probing. ENOENT is the correct answer (cache dylibs are not on disk) | implemented (struct stat64 144 bytes, birthtime := ctime) |
| 92 | fcntl (150) | `F_GETPATH` (0x32=50) on the exe fd | implemented (/proc/self/fd) |
| 197 | mmap (293) | `mmap(0, 0x28a50, PROT_READ, MAP_PRIVATE\|MAP_FILE 0x40002.. )` of the exe header, then munmap | implemented, 16 KiB alignment re-imposed on Linux |
| 74 | mprotect (132) | `mprotect(0x100004000, 0x4000, PROT_READ)` (exe `__DATA_CONST`) | implemented |
| 6, 73 | close, munmap | | implemented |
| 169 | csops (265) | `csops(0, CS_OPS_STATUS=0, buf, 4)` | CS_VALID only (same as Darling emulation csops.c) |
| 33 | access (87) | `/AppleInternal/XBS/.isChrooted` (libsystem_kernel init) | ENOENT |
| 48, 329 | sigprocmask, __pthread_sigmask | how in {1,2,3}, 32-bit sigset | implemented with Darwin->Linux signal map; SIGSEGV/SIGTRAP/SIGILL/SIGBUS never blockable (engine owns them) |
| 1, 521 | exit, abort_with_payload (46, ...) | dyld uses 521 on `halt()`: namespace 6/35 (OS_REASON_DYLD) + message string | implemented |
| 4 | write (49) | dyld `console()` to fd 2 | implemented |

Callable but not yet seen: read, pread, writev, lseek, getuid family, issetugid, getentropy, bsdthread_register,
workq_kernreturn (ENOTSUP stub), lstat64, fstat64, fstatat64, madvise.

## 4. Mach traps touched (x16 < 0, trap = -x16; result in x0, flags untouched)

| trap | name (SW line) | Observed args / expectation | Our answer |
|---|---|---|---|
| 28 | task_self_trap (134) | first call of dyld | opaque name 0x103 |
| 26 | mach_reply_port (132) | dyld + libsystem_kernel | fresh opaque names 0x1103, 0x1203.. |
| 14 | _kernelrpc_mach_vm_protect_trap (120) | 46 calls, `(self, addr, size, set_max=0, prot)` on dyld's own `__TPRO_CONST` (RW/RO toggling around fixups) | mprotect |
| 10 / 12 | mach_vm_allocate / deallocate (116, 118) | `(self,&addr,0x20000,flags 0x3c000001 = ANYWHERE|tag)`, alignment-trim pattern | mmap/munmap, 16 KiB aligned |
| -3 | MACH_ARM_TRAP_ABSTIME | many | CLOCK_MONOTONIC ns (xnu osfmk/mach/arm/traps.h:36) |
| 15 | _kernelrpc_mach_vm_map_trap (121) | 5 calls (dyld x3, libsystem_kernel x2): `(target, *address, size, mask, flags, cur_protection)` (osfmk/mach/mach_traps.h:191) | UNIMPLEMENTED (KERN_FAILURE) = **first blocker** |
| 19 | _kernelrpc_mach_port_mod_refs_trap (125) | once | UNIMPLEMENTED |
| machdep | x16 = 0x80000000, x3 = 2 (set TSD base), 3 (get) | `x0` = pthread TSD pointer (OBSERVED 0x...c1f20 inside dyld data) | stored per host thread, served to `mrs tpidrro_el0` (xnu osfmk/arm64/proc_reg.h:2885, machine_routines.c platform_syscall) |

Not seen yet but expected next: mach_msg/mach_msg2 (bootstrap), semaphore, mk_timer, port_allocate/construct.

## 5. Commpage / TSD / sysctl-by-field

* Commpage was mapped (RW at 0xFFFFFC000, RO at 0xFFFFF4000, 16 KiB each; xnu osfmk/arm/cpu_capabilities.h:166-167,
  fields :294-342, fill code osfmk/arm/commpage/commpage.c:115-177; snapshot layout version 1). The genuine
  libSystem has NOT yet been observed reading it (no fault, no way to see reads): which fields 27 reads is UNKNOWN.
  Fields filled: signature "commpage 64-bit", caps64/caps32, version 3, ncpus, cache line 64, cluster count,
  page shifts 14 (RW+RO), memory size, cpufamily 0 (unknown SoC), user timebase 0 (forces the -3 trap).
  Deliberately not advertised: kHasFeatPAuth, kHasFeatLRCPC/LRCPC2 (emulated), LSE only when the host has it.
* Darwin TSD: `mrs xN, tpidrro_el0` occurs 37 times in dyld alone; Linux returns 0 silently (no trap), so it must
  be lowered. Done as BRK trap (Op::Mrs, encoding `0xd53bd060|Rt`); value = machdep set_tsd_base.
* sysctl values asked so far: only `kern.bootargs` (string) and the failed `security.mac.lockdown_mode_state`.
* File-system view: dyld addresses absolute Darwin paths (`/usr/lib/...`, `/System/Cryptexes/...`); ds-run has NO
  chroot/overlay yet (host paths). Darling's vchroot is the intended mechanism.

## 6. Lowering findings (genuine 27 arm64e code, OBSERVED)

| Instruction | Count | Handling |
|---|---|---|
| PAC sign/auth/strip/branch/ret (dyld) | 3609 / 1372 / 761 / 819 / 1771 | software PAC, BRK trap |
| LSE CAS / RMW (dyld) | 35 / 25 | emulated by LL/SC trap |
| `svc #0x80` (dyld) | 167 | trap -> Darwin dispatcher |
| `mrs xN, tpidrro_el0` (dyld) | 37 | trap (new Op::Mrs) |
| LDAPR/LDAPRB/LDAPRH (FEAT_LRCPC) (dyld) | 12 | rewritten in place to LDAR/LDARB/LDARH (acquire RCsc >= RCpc: strengthening, counted, never silent) |
| LDAPUR* (FEAT_LRCPC2) | 0 in dyld/echo; decoder + emulation (acquire load) unit-tested | trap |
| Cache pages with code outside `__TEXT` sections (branch islands) | present | whole executable page lowered |
| echo (233 words) | pac-sign 3, br-auth 8, ret-auth 1 | |

Host: ARMv8.0, no PAC/LSE/LRCPC. `ldapr->ldar` happened at dyld load time (eager); pages of the cache are lowered
lazily on first fetch; dylibs dyld itself maps from disk later and `mprotect(PROT_EXEC)` of fresh code are NOT lowered
yet (no echo path needs it; any Darling runtime that loads non-cache dylibs must hook this).

## 7. Blocker (exact)

```
UNIMPLEMENTED mach 15 _kernelrpc_mach_vm_map_trap at /usr/lib/system/libsystem_kernel.dylib+0xab4
ds-exec: unhandled BRK #45057 at pc=0x180897d40 (/usr/lib/system/libsystem_platform.dylib+0x3d40): not a lowered instruction
```
`brk #0xb001` is genuine Apple code (a crash/assert trap after the failed vm_map in libSystem initialization; its
exact caller was not decoded). Next requirements for a runtime that wants echo: mach_vm_map trap (anonymous),
mach_port_mod_refs, proc_info, and then mach_msg/bootstrap behaviour of libSystem init.
