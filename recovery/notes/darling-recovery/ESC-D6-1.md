# ESC-D6-1: genuine 27 plutil -help and AppKit probe stop in CoreFoundation (autdb on a cache slot)

Verbatim (MLDR_DSEXEC_LOG, MLDR_DSEXEC_REGS=1, MLDR_DSEXEC_PEEK=1e63d66f0; both plutil.s1 -help and the AppKit probe, same site):
  ds-exec: guest brk #0xc473 pc=0x1809e9488 (/System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation+0x11c488) lr=0x3e0001808d06cc () fp=0xffffedc20 x0=0x1e803ef70 x1=0x30
  ds-exec:   x2=0xcf0c5866 x8=0x1e63d64f8 x16=0x40000000000000 x17=0x0 x19=0x1 x20=0x30 x23=0x1e803ef70 x24=0x10
  ds-exec:   [x0]=0x40001e63d64f8 [x0+8]=0x280
  ds-exec:   [0x1e63d66f0]=0x20001fb9edc38
  ds-exec:   x0 in 1e7f50000-1ef548000 r--p 05dac000 08:02 784288 .../dyld_shared_cache_arm64e.25.dylddata

Code (cache .01, 0x1809e9428..): isa check of a constant CFString: `autda` of [x0] (ok, x8=0x1e63d64f8 = class) then
`ldr x16,[0x1e63d66f0]; autdb x16, (0x1e63d66f0 | 0xcb87<<48)` fails (brk #0xc473 = DB key). The slot holds 0x20001fb9edc38,
neither the class nor a DB-signed class. Slide-info v5 has only keyIsData (IA/DA), so a DB pointer in CF __DATA_DIRTY must be written
at run time (CF initializer, or dyld cache patching); either that write has not happened yet (initializer order) or the DB sign/auth
in ds-exec is inconsistent. Not investigated further. Next: REA on CF+0x11c488 callers / who writes 0x1e63d66f0 (watch with PEEK at
earlier points), check ds-exec DB key handling.
