#!/usr/bin/env python3
"""Annotate an objdump disassembly range with the real targets of cache stub calls.
  stubname.py <dylib> <start-hex> <len-hex>
Stub jmpq *GOT -> read GOT in the dyld cache files -> nearest exported symbol in the extracted dylibs."""
import bisect, os, re, struct, subprocess, sys

BASE = os.path.expanduser('~/macos-re/recovery/bs/System/Library/dyld/')
DSC = os.path.expanduser('~/macos-re/dsc-x86_64')
MAP = BASE + 'dyld_shared_cache_x86_64.map'

def read(addr, n):
    for f in ['dyld_shared_cache_x86_64', 'dyld_shared_cache_x86_64.01']:
        with open(BASE + f, 'rb') as d:
            h = d.read(0x200)
            mo, mc = struct.unpack_from('<II', h, 0x10)
            d.seek(mo)
            for _ in range(mc):
                a, s, fo, mp, ip = struct.unpack('<QQQII', d.read(32))
                if a <= addr < a + s:
                    d.seek(fo + addr - a)
                    return d.read(n)
    return None

images = []
cur = None
for line in open(MAP):
    if line.startswith('/'):
        cur = line.strip()
    m = re.match(r'\s+__TEXT\s+0x([0-9A-F]+) -> 0x([0-9A-F]+)', line)
    if m and cur:
        images.append((int(m.group(1), 16), int(m.group(2), 16), cur))
symcache = {}

def symbolize(addr):
    for lo, hi, path in images:
        if lo <= addr < hi:
            f = DSC + path
            if f not in symcache:
                out = subprocess.run(['llvm-nm', '--defined-only', f], capture_output=True, text=True).stdout
                syms = sorted((int(p[0], 16), p[2]) for p in (l.split() for l in out.splitlines()) if len(p) == 3 and p[0].strip('0123456789abcdef') == '')
                symcache[f] = syms
            syms = symcache[f]
            i = bisect.bisect_right([s[0] for s in syms], addr) - 1
            if i >= 0:
                return '%s!%s+0x%x' % (os.path.basename(path), syms[i][1], addr - syms[i][0])
    return hex(addr)

dylib, start, length = sys.argv[1], int(sys.argv[2], 16), int(sys.argv[3], 16)
dis = subprocess.run(['llvm-objdump', '-d', '--no-show-raw-insn', '--start-address=%d' % start,
                      '--stop-address=%d' % (start + length), dylib], capture_output=True, text=True).stdout
for line in dis.splitlines():
    m = re.search(r'callq\s+0x([0-9a-f]+)', line)
    if m:
        t = int(m.group(1), 16)
        sd = subprocess.run(['llvm-objdump', '-d', '--no-show-raw-insn', '--start-address=%d' % t, '--stop-address=%d' % (t + 6), dylib],
                            capture_output=True, text=True).stdout
        g = re.search(r'jmpq\s+\*0x[0-9a-f]+\(%rip\)\s+## 0x([0-9a-f]+)', sd)
        if g:
            v = read(int(g.group(1), 16), 8)
            if v:
                target = 0x7ff800000000 + (struct.unpack('<Q', v)[0] & 0xFFFFFFFFF)
                line = re.sub(r'<.*>', '<' + symbolize(target) + '>', line)
    print(line)
