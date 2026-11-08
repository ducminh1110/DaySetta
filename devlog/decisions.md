# why stuff is the way it is

short notes so i remember why i did things. newest at the bottom.

- **apple libs only from the 27 ipsw.** tried reusing my 26.6 set once, nope. mixing versions = weird crashes later.
- **no darling-built libdispatch/objc/cf.** only kernel-ish shims get replaced. if apple code breaks, fix the emulation not the lib.
- **ds-exec instead of a full cpu emulator.** host is armv8.0, apple code uses pac/lse/etc. patching just the missing instrs is way faster than qemu-user. qemu was the backup plan, never needed it.
- **udot/sdot as inline neon, not traps.** sqlite's checksum loop hammers it, trapping every time would be slow af. tried sysctl feature hiding first but the code doesnt even check, its unconditional.
- **real mach vouchers > the empty-voucher hack.** the hack worked for plutil but broke later stuff. did it properly once.
- **launchd session type per job.** opendirectoryd refuses to start unless it sees "System". darling only has one job manager so the plist loader stamps it.
- **tccd parked.** wasted a day making it run, then noticed daylight never needed it for windows. missing service = fast fail, crashing one = hang. so no tccd job until something actually needs it.
- **remap restore in ds-exec.** libobjc copies trampoline pages with vm_remap, copies carried our patched branches -> jump to nowhere. restore original words on the copy + relower at the new addr.
- **fp16 on the trap path first.** the inline version kept giving 1+1=1 lol. trap path is slow but correct, window first, speed later.
- **don't patch data that lives in __text.** lzfse keeps a mask table inside code, one entry looked like an fp16 instr and got patched -> black window. current fix is a narrow rule (all-h31 fma = data). proper fix = only patch reachable code, todo.
- **q8-q15 full save in the trampoline block.** c abi only keeps the low half, handler was eating the top half. found it while chasing lzfse.
- **reims-vgpu for gpu later, not indium.** reims runs apple's own paravirt metal driver stream on vulkan, so metal stays 100% apple. needed lse/rcpc fallbacks to even build on this cpu.
- **x86_64 later via an arm64 emulator.** the goal is running arm mac apps, a native x86 macos userland wouldnt do that. arm build has to mature first.
