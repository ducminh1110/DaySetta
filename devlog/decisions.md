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
- **switch to real windowserver (owner call).** fake ws input on 27 stuck in skylight dispatch for days, real ws gets the protocol for free + reims-vgpu covers metal. spike: ws 27 runs under darling, needed sandbox op 4 allowed in mac_syscall, now dies at "IOMainPort failed" cuz aarch64 darling builds only cli (no iokitd). next is iokitd on arm + fake iomfb/hid user clients, stela's dcp model is the reference for what iomfb publishes.
- **iokitd on arm.** turned on the iokitd component (wraps x11/egl/gl only, no full gui) + killed dupe stubs from the merge. prefix had OpenGL Libraries symlinked to apple's dir so darling libGL was invisible, made it a real dir. ws now gets past IOMainPort, dies at "IOHIDSystem not found", so next = hid service in iokitd.
- **ws past hid.** iokitd now has IOHIDSystem (stub user clients) and IOProviderClass matches by class conformance like real iokit, not literal string. ws then enumerates iomfb (FB Count = 0, no /arm-io), smc fallback ok, launchservicesd job added. now it dies randomly in libdispatch on arm darling: event_loop_merge stack probe blowup (looks like bogus nevents into a workloop thread) and a jump into unremapped cache code in mach_reply_unregister. that's wl_kevent/ds-exec land, fix that before iomfb or nothing is testable.
- **libdispatch crashes were 2 bugs, not dispatch.** (1) darling workloop emu had no owner tracking so a DISCOVER_OWNER thread request kept spawning threads that couldnt drain (lock owned by the sync waiter) -> wake/rearm spin 170/s. now owner is read from dq_state low bits, no thread while owned, END_OWNERSHIP restarts. (2) ds-exec turned xpac into an in-place `and`, wrong for bit-55 values, dispatch's -2 sentinel got mangled -> its own autda check brk'd. xpac goes thru an island now (tbnz 55 / and / orr, no temps no flags). MLDR_DSEXEC_NO_STRIP=1 was the quick proof.
- **real ws display = vm paravirt path.** IOMobileFramebuffer userland has a vm branch: /arm-io child with compatible "paravirtualizedgraphics,gpu" -> AppleParavirtGPU DisplayPortCount -> match IOMobileFramebuffer (AppleParavirtDisplay). iokitd fakes exactly that, mode/timing dicts shaped like stela's dcp modes.rs. tried the dcp/arm-io real-hw path first in my head, way more stuff, vm path is tiny.
- **two darling bugs on the way.** kevent_qos with ERROR_EVENTS still let libkqueue poll real events into the spare slots then threw them away (EV_DISPATCH knote disarmed forever) -> every darling-native mach source fired once then died. cap nevents to receipts. tiny kqtest repro proved it (2000 round trips now). then ulock_wait always returned 1 ("slow path hack"), apple pthread_dependency_wait_np traps on anything but 0, so count waiters per addr. now ws stops at "No suitable Metal devices present after 30 seconds" -> metal via pvg + reims is next.
- **metal = apple's own paravirt plugin.** AppleParavirtGPU in iokitd is the IOAccelerator with MetalPluginName/ClassName (same as the vm kext personality), genuine Metal loads AppleParavirtGPUMetalIOGPUFamily.bundle (copied into the prefix, a symlink made bundleForClass resolve to /Volumes/SystemRoot and the cache-only compiler plugin path broke). device info 292b = deviceInfo ivar layout (from the objc type string) + 56b features, only max shader version 3.1 set, rest left "undefined" so plugin defaults win. this is the pvg path reims eats, so it lines up with the display side.
- **arm64 iokit traps never left the process.** IOKit's iokit_user_client_trap is a raw `svc` and ds-exec leaves svc native (linux syscall from x8 = garbage). dyld now rewrites the stub to adrp/ldr/br into darling_iokit_user_client_trap, written via /proc/self/mem so lazy ds-exec lowering + page prot stay as they were. tried a mach trap table entry first, useless since svc never gets emulated, reverted. ws now gets a Metal device, makes cmd + notif queues and keeps running. no pixels yet: command buffers go nowhere until reims executes them.
- **iogpu outputs one assert at a time.** each IOGPU assert names the field: sel 6 = trace id group {first,end}, sel 14 = device shmem {addr, u32 size, u32 id} (size first, swapped it after a resource list size<=0 assert), sel 9 resource {gpuva, data, client shared, id, size}. ws now builds command buffers and only stops in colorsync's sync xpc (MACH_RCV_TIMED_OUT).
- **mldr picks arm64e out of fat files.** MTLCompilerService ships an arm64 subtype 12 slice too and mldr just took the last arm64 one, sigill on stuff v8.0 cant do. only all/v8/arm64e now.
- **darlingserver thread_unblock kills the wait timer like xnu.** fake MACH_RCV_TIMED_OUT on untimed receives (colorsync sync xpc, VirtualDisplayListener) was a leftover timer from an earlier timed wait firing later. was hunting it in libsystem_kernel mach_msg2 first, nope.
- **gpu dirty ring gets a length (4096) and trap 258 drains it.** zeroed ring = length 0 = assert on first texture upload. agent tried length 0xffffffff to dodge the flush, dropped that.
- **iosurface textures need features+0x1e/+0x32.** sel 265 output = deviceInfo [0,0xec) + features [0xec,0x124). descriptor2 alone hits the v1 usage-truncation assert, swizzled too makes everything go v2. agent poked out[0x15] which is deviceInfo, wrong block.
 - **memset fix in IOMobileFramebuffer sel 265.** replaced out[0xec + 0x1e] = 1; out[0xec + 0x32] = 1; with memset(out + 0xec, 0xff, 0x38); to fully populate features block. WS progresses past Metal texture creation to swap path. Evidence: RWS5.log no texture errors, 90s runtime; metalprobe prints default=Apple Paravirtual device.
- **first real ws swap.** ws does swapbegin/swapend twice then swapwaits forever (no swap-complete notif from us). swapend struct +0x9c = iosurface id (+0x48 looked like it, isnt). presented surface is id 2 1440x900 bgra, blank since nothing runs the metal cmds. dump hack not committed.
- **reverted agent's iomfb swap callback.** it fed sel 72 scalarIn[1] as the callback into the async msg, IODispatchCalloutFromCFMessage jumped there -> segv. swap-complete notif still todo, needs the real async ref (wake port + ref array) not scalars.
- **reims path = iokitd plays the paravirt kext.** host spike drives libreims_vgpu.so straight (memfd guest ram, root fifo, getdeviceinfo, definetask2) on llvmpipe, all ok. plan: wrap_elf the .so into iokitd like EGL, guest phys = iokitd's own pages. dropped the darlingserver memfd rpc idea for now, only needed if reims wants page views of resources.
- **iokitd now does a real IODataQueue for GPU completion.** selector 16 returns addr+id as scalars (client never sends a struct out), trap 0 appends scheduled/completed entries and sends the bare 24-byte mach message. spec said advance head but libIOKit dequeues at head, so producer must advance tail; head version called a null block and segv'd. WS now has 2 submits (was 1) then sits at the known swap wait.
- **exec via objlist + ExecIndirect3 straight off the core shmem.** kernel cmds name the resource holding each segment, so one 67 per cmd; iogpu res id != object ref (sel9 +0x5c), mixed them up at first. reims now chokes on textures 8/9/17, records next
- **iosurface mapper via reims' own mapper device.** reims took linux 4k pages for it and the c config had no memory files so nothing ever mapped; patched our reims fork (guest 16k pages + memory_files), mapper made after the gpu device. tried 4k first form, died on the 16k mapping assert
- **ws goals 1-3 are done; pixels are a reims bug.** current build runs ws 60s with 0 asserts, swaps reach iomfb sel 5 and the swapped surface dumps to a png (black). reims creates the 324b CA pipeline fine then refuses the draw with Draw(AttachmentFormat(Color(0))) even though the blob carries BGRA8Unorm, so the format is lost in deserializing - not an iokitd field to fill. tried reading it as a selector problem first, it isnt.
- **didnt commit the pvg work.** the arena/task/objects/exec/present code is 647 uncommitted lines with /tmp dumps in it and night agents are still editing those same files, so committing would only bake in probes.
- **libnotify 348 boot-death: it was our xpc stub logging.** asl_open -> notify_register_check -> libnotify's introspection dispatch_once -> xpc_copy_entitlement_for_token -> xpc_stub() -> os_log -> asl_open again -> same dispatch_once -> libdispatch brk. dropped the log in that one stub, not a notify hack.
- **state set/get is per-name and dies with the last registration.** notifyutil -s then a fresh -g reads 0 on the old pair too, so not our bug. with a live watcher holding the name, -s 42 / -g / post all line up.
- **the >5000 ws lines goal died before this port.** 11-17 16:17+ runs (ws127-131) are all ~180 lines with the old pair; ws122/124 were 6k-20k earlier that day. new pair 175 vs old control 187, same output, so no regression, just no display traffic anymore.
