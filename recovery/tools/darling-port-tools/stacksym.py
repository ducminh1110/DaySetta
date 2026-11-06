#!/usr/bin/env python3
"""Symbolize the stack of a blocked Darling thread (run with sudo): stack words that point into file-backed
executable mappings are mapped to <file>+offset and the nearest symbol (llvm-nm on that Mach-O file).
Shared-cache addresses (no file) are printed raw for ~/macos-re/tools/dsym.py.
  stacksym.py <tid> [words]"""
import bisect, subprocess, sys

tid = int(sys.argv[1])
nwords = int(sys.argv[2]) if len(sys.argv) > 2 else 2048
sc = open(f'/proc/{tid}/syscall').read().split()
sp, pc = int(sc[-2], 16), int(sc[-1], 16)
maps = []
for l in open(f'/proc/{tid}/maps'):
    f = l.split()
    lo, hi = (int(x, 16) for x in f[0].split('-'))
    maps.append((lo, hi, f[1], int(f[2], 16), f[5] if len(f) > 5 else ''))
symcache = {}

def sym(path, off):
    if path not in symcache:
        out = subprocess.run(['llvm-nm', '-n', '--defined-only', '--arch=x86_64', path], capture_output=True, text=True).stdout
        s = [(int(p[0], 16), p[2]) for p in (l.split() for l in out.splitlines()) if len(p) >= 3]
        symcache[path] = s
    s = symcache[path]
    i = bisect.bisect_right([a for a, _ in s], off) - 1
    return '%s+0x%x' % (s[i][1], off - s[i][0]) if i >= 0 else hex(off)

def describe(v):
    for lo, hi, perm, foff, path in maps:
        if lo <= v < hi and 'x' in perm:
            if path.startswith('/'):
                return '%s %s' % (path.split('/')[-1], sym(path, v - lo + foff))
            return 'anon/cache 0x%x' % v
    return None

print('syscall', sc[0], 'pc', describe(pc) or hex(pc))
mem = open(f'/proc/{tid}/mem', 'rb')
data = b''
for off in range(0, nwords * 8, 0x1000):
    try:
        mem.seek(sp + off)
        data += mem.read(0x1000 - ((sp + off) & 0xfff) if off == 0 else 0x1000)
    except OSError:
        break
for i in range(0, min(len(data), nwords * 8) - 7, 8):
    d = describe(int.from_bytes(data[i:i + 8], 'little'))
    if d:
        print(d)
