# D2 fixes needed on THIS host (aarch64, ARMv8.0: no PAC/LSE; 4K pages, 48-bit VA; clang 21.1.8 not 19; cmake 4.2.3)
Recipe: `cmake -G Ninja -DCOMPONENTS=cli,cli_dev_gui_stubs -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_POLICY_VERSION_MINIMUM=3.5 ..` (+ `ninja -j8`, `sudo ninja install` to /usr/local; sudo -n works). `cli` alone fails linking secd (-lCoreData, stubs only built with cli_dev_gui_stubs: upstream gating gap, src/external/security/keychain/securityd/CMakeLists.txt:57, src/frameworks/CMakeLists.txt:156).
Logs: D2-ninja-build-oldxnu.log (baseline on upstream xnu: only 4 failures, all kdebug_private.h/sys/kdebug.h: owner's macOS-26 SDK links into 12377 xnu), D2-ninja-build{2..21}.log (iterations on daysetta/arm64), D2-install{,2..5}.log.
| # | file | why |
|---|---|---|
|1| superproject Developer/.../MacOSX.sdk/usr/include/{arm/_endian.h, libkern/arm/_OSByteOrder.h, mach/mach_eventlink_types.h}, +17 links via tools/sync-xnu-sdk-headers.py e7fca411 | headers 12377 includes that the SDK lacked |
|2| git rm 18 SDK mach MIG headers (task.h, mach_host.h, ...) | stale old-xnu MIG output (task_create_suid_cred, io_master_t); build overlay regenerates from 12377 defs |
|3| bootstrap_cmds/CMakeLists.txt (+_ARM_MACHTYPES_H_), darling/include/arm/* links | host migcom typedef clash with glibc on arm |
|4| src/startup/mldr/include/mach/machine/vm_types.h (+arm/vm_types.h) | was a symlink to i386 vm_types.h: integer_t undefined on arm |
|5| xnu osfmk/mach/arm/_structs.h: uint32_t -> __uint32_t | undeclared in sys/signal.h include chain |
|6| xnu libsyscall/CMakeLists.txt: LIBSYSCALL_INTERFACE for *_internal.h MIG; os/x18.c on aarch64 | 12377 mach_vm_internal.h clash; update_tpidr undefined |
|7| xnu libkern/libkern/arm/OSByteOrder.h: `#if 0` the _POSIX_C_SOURCE branch | libxpc plist.m needs OSReadSwapInt* (i386 header has them unconditionally) |
|8| xnu gen/bsdsyscalls/___vfork.S (+custom/__vfork.S): _vfork alias also on arm64 | launchctl/tcsh link |
|9| SDK bsm/audit_session.h: drop duplicate enum audit_session_flags | 12377 bsm/audit.h defines it |
|10| SDK corecrypto/cc_config.h: no host <endian.h> on __APPLE__ | /usr/include/endian.h -> bits/wordsize.h not found |
|11| src/CMakeLists.txt: `-mcpu=generic+crc+aes` for TARGET_ARM64 | apple-darwin default CPU emits LSE casal (SIGILL in dyld sys_thread_set_tsd_base on ARMv8.0); +crc+aes: bzip2 crc32vec.S needs pmull (host has aes/pmull/crc32) |
|12| mldr/commpage.c: map RO commpage 0xFFFFF4000 with page shifts; mldr.c: stack_top = commpage-0x8000 on aarch64 | 12377 reads USER_PAGE_SHIFT_64/KERNEL_PAGE_SHIFT from RO page: vm_page_size was 1 -> _simple buffer crash (brk) |
