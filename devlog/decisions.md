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
- **fp16 trampolines.** rewrote build_inline_fp16's encodings (verified against llvm-mc).
  arithmetic in S precision to match both the host (no FRINT/TCG private instrs) and
  the trap path's half conversions. NaN results need a canonical 0x7e00 fixup
  after s2h or results drifted. everything else cleared the validation tests.
- **host gpu module lives in daysetta-isa/tools/rvg-host.** first brick proven: llvmpipe device + cold frame black png; module owns the device lifecycle. C API only, no QEMU/Apple involvement.
- **nsscreen empty = 27 display-state layout.** the 26.6 blob (438b) misparses under
  skylight 27 (header gained a flag+bool+count shift), so screens was 0 and windows
  collapsed to 1x28. swapped in a 27 blob (one 1920x1080 display, trimmed from the
  recorded reply): screens=1, visibleframe right, hello2 window drawn at 420x268.
- **eor3 sigill = host has no FEAT_SHA3.** vimage/libcache keccak hits eor3 -> native
  sigill on armv8.0 hosts. added sha3 eor3/bcax/rax1/xar to the trap path (same shape
  as the fp16 work, encodings from gnu as + real cache words).
- **m5a input stalls inside skylight 27, not our encoder.** ws posts + fetches are
  byte-identical to daylight and skylight fetches our datagrams (logged), yet nothing
  reaches appkit: no mouse action, no field insertion. our records decode fine via
  skylight's own codec (unicode "a" extracted), but skylight's encoder refuses both
  our key and mouse records on re-encode (null), so the records are missing fields
  27's dispatcher requires. next: diff our records field-by-field against a real ws
  record (REA on the field descriptors / block_invoke encoders), not more fifo tests.
- **m5a 30578 canned + real front psn.** skylight on 27 calls setfrontprocesswithinfo at
  mig id 30578 (was 30118/30577): hello2 calls it, old code answered default-success
  (ran fine but dispatch still dead). recorded 12-byte reply added as canned entry for
  30578, auto-activate fronts with real psn (pid + 0x10000000). insertion still fails:
  events are fetched by skylight but dropped inside dispatch. next: the connection-id
  lookup/key in event_datagram_handler vs newconnectionport, or a real-mac ws record diff.
- **m5a wire check.** hello still rejects the port stream even with the mac-shaped reply; input continues to be queued but snarf/dispatch remains the cutoff. stopped with a field-ready hello2 before the click/keys sample.
