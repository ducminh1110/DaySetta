# dqdump.py PID: libdispatch's manager queue and anonymous timer heaps in a running Darling process (run as root).
import struct, subprocess, sys, time
pid = sys.argv[1]
F = '/usr/local/libexec/darling/usr/lib/darling/macos26/usr/lib/system/libdispatch.dylib'
syms = {}
for l in subprocess.run(['llvm-nm-18', F], capture_output=True, text=True).stdout.splitlines():
    p = l.split()
    if len(p) == 3:
        syms[p[2]] = int(p[0], 16)
base = None
for l in open(f'/proc/{pid}/maps'):
    if 'macos26/usr/lib/system/libdispatch.dylib' in l and ' r-xp ' in l:
        a = int(l.split('-')[0], 16); off = int(l.split()[2], 16)
        base = a - off
        break
mem = open(f'/proc/{pid}/mem', 'rb')
def rd(addr, n):
    mem.seek(addr); return mem.read(n)
now = time.monotonic_ns()
print('mach now', now)
q = rd(base + syms['__dispatch_mgr_q'], 0x80)
print('mgr_q', ' '.join('%016x' % v for v in struct.unpack('<16Q', q)))
# struct dispatch_timer_heap_s: u64 dth_target, dth_deadline; u32 dth_count; u8 segments, max_qos, dirty_bits,
# flags (armed, needs_program); ptr dth_min[2]; ptr dth_heap  (48 bytes)
h = rd(base + syms['__dispatch_timers_heap'], 48 * 9)
for i in range(9):
    tgt, dl, cnt, seg, mq, dirty, fl, m0, m1, hp = struct.unpack_from('<QQIBBBBQQQ', h, i * 48)
    if cnt or dirty or fl:
        print(f'heap {i}: target {tgt} ({(tgt - now) / 1e9:+.2f} s) deadline {(dl - now) / 1e9:+.2f} s count {cnt} '
              f'dirty {dirty:#x} flags {fl:#x} min {m0:#x}')
