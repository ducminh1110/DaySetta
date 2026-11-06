"""Dump a stopped/blocked Linux thread's user stack words that point into executable mappings."""
import sys
pid = int(sys.argv[1])
sc = open(f'/proc/{pid}/syscall').read().split()
sp, pc = int(sc[-2], 16), int(sc[-1], 16)
maps = []
for l in open(f'/proc/{pid}/maps'):
    a, perms = l.split()[:2]
    lo, hi = (int(x, 16) for x in a.split('-'))
    maps.append((lo, hi, perms, l.split()[-1] if len(l.split()) > 5 else ''))
def ex(addr):
    return any(lo <= addr < hi and 'x' in p for lo, hi, p, _ in maps)
print(f'pc 0x{pc:x} sp 0x{sp:x}')
mem = open(f'/proc/{pid}/mem', 'rb')
data = b""
for off in range(0, 0x8000, 0x1000):  # stop at the end of the stack mapping
    try:
        mem.seek(sp + off); data += mem.read(0x1000 - ((sp + off) & 0xfff) if off == 0 else 0x1000)
    except OSError:
        break
for i in range(0, len(data), 8):
    v = int.from_bytes(data[i:i+8], 'little')
    if ex(v):
        print(f'0x{v:x}')
