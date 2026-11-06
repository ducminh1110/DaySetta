# ESC-D4-1: dyl4 (dyld-1378, 26.5 source) vs macOS 27 libdyld/libobjc interface drift
Reached (aarch64 ARMv8.0, Darling): mldr maps 27 cache (slide v5, soft-PAC signed) -> dyl4 (arm64e) loads genuine libSystem
and runs libSystem/libpthread init under lazy cache lowering. Verbatim stop:

  ds-exec: PAC authentication failure at pc=0x180465e10 (?) word=0xd71f0830: poisoned 0x20000000000038
  ds-exec: SIGSEGV addr=0x38 pc=0x38 () lr=0x180405928 (/usr/lib/libobjc.A.dylib+0x1928) x0=0x2000ebdb0

x0 is dyl4's dyld4::APIs object; genuine libobjc/libdyld (27) call it through the vtable. dyld-1378's APIs virtual
order/signatures differ from 27 (same class of drift as LibSystemHelpers::setUpThreadLocals, mach_o::Error ABI
(27: CStringBuilder-based, 0xA8 bytes), exports-trie uint32 offsets, already fixed). No macOS 27 dyld source is published
(apple-oss-distributions/dyld tags stop at dyld-1378).
Needed: recover the 27 dyld4::APIs vtable order+signatures from the genuine 27 dyld (vtable for dyld4::APIs at 0xc4d28 in the
arm64e slice; entries are chained fixups, my vt.py decode needs the right fixup format) and reorder DyldAPIs.h to match;
repeat for any other dyld<->libdyld interface (RuntimeState-facing structs, dyld_all_image_infos version, objc notifiers).
Not darlingserver related. Handoff: no darlingserver change needed so far (ESC-D4-darlingserver not written).
