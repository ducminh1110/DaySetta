# ESC-D8-1: Apple libdispatch 27 needs a real task Mach voucher (and cfprefsd for plutil -p)

kevent workqueue + workloop emulation works: Apple's cache libdispatch runs dispatch_async + dispatch_after on global queues (probe `dq`, rc=0).
`plutil -p t.plist` and the AppKit probe stop when libdispatch starts the first Mach IPC (XPC to com.apple.cfprefsd.daemon):
  mach trap 70 = host_create_mach_voucher_trap: "Unimplemented mach trap (70)" (before this change), now returns KERN_FAILURE:
  ds-exec: guest brk #0x1 pc=0x180707490 (libdispatch+0x3c490) x0=0x5   -> "BUG IN CLIENT OF LIBDISPATCH: Could not create task mach voucher"
A pretend send right instead of failure gets further and stops at "BUG IN CLIENT OF LIBDISPATCH: Voucher port corruption" (libdispatch+0x20cd8).
Missing kernel feature (xnu-12377 ipc/ipc_voucher.c, bank, importance): host_create_mach_voucher with BANK/IMPORTANCE/ATM attribute managers, voucher ports as
real kernel objects, mach_voucher_extract_attr_recipe / attr_command traps (71..73), voucher adoption in mach_msg (msgh_voucher_port).
darlingserver duct-tape has ipc_voucher but no attribute managers (owner's libdispatch_apple avoided it with VOUCHER_USE_EMPTY_MACH_BASE_VOUCHER; the cache one cannot).
Also needed for plutil -p: the genuine 27 /usr/sbin/cfprefsd (extracted this run from the 27 system DMG into store-27/root/usr/sbin + LaunchDaemons plist) loaded as a launchd job
with DARLING_DYLINKER env (job plist in ~/.darling/Library/LaunchDaemons, root-owned; loads: `launchctl list` shows com.apple.cfprefsd.xpc.daemon).
plutil -convert xml1 -o - t.plist works without cfprefsd/vouchers (prints the XML).
