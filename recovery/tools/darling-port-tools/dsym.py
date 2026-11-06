#!/usr/bin/env python3
"""Symbolize 'Darling: ... 0x...' crash lines from a Darling run log.

Cache addresses resolve through the cache .map file plus symbols of the ipsw-extracted dylibs;
Darling images (dyld, libsystem_kernel) resolve through 'darling: image/dyld loaded at' lines and
local copies of those binaries.
usage: dsym.py LOG MAPFILE DSC_DIR [name=localpath ...]
"""
import bisect, os, re, subprocess, sys

log, mapfile, dsc = sys.argv[1:4]
local = dict(a.split('=', 1) for a in sys.argv[4:])

images = []  # (start, end, path)
cur = None
for line in open(mapfile):
    if line.startswith('/'):
        cur = line.strip()
    elif cur and '__TEXT' in line:
        a, b = re.findall(r'0x[0-9A-F]+', line)
        images.append((int(a, 16), int(b, 16), cur))
images.sort()

symcache = {}
def symbols(path, slide=0):
    key = (path, slide)
    if key not in symcache:
        out = subprocess.run(['llvm-nm', '-n', '--defined-only', path], capture_output=True, text=True).stdout
        syms = []
        for l in out.splitlines():
            parts = l.split(' ', 2)
            if len(parts) == 3 and parts[1] in 'tT':
                syms.append((int(parts[0], 16) + slide, parts[2]))
        symcache[key] = syms
    return symcache[key]

def nearest(syms, addr):
    i = bisect.bisect_right([s[0] for s in syms], addr) - 1
    return f'{syms[i][1]}+0x{addr - syms[i][0]:x}' if i >= 0 else '?'

text = open(log, errors='replace').read()
darling = []  # (base, path)
m = re.search(r'dyld loaded at (0x[0-9a-fA-F]+)', text)
if m:
    darling.append((int(m.group(1), 16), '/usr/lib/dyld'))
for base, path in re.findall(r'darling: image (0x[0-9a-fA-F]+) (\S+)', text):
    darling.append((int(base, 16), path))

def describe(addr):
    starts = [i[0] for i in images]
    i = bisect.bisect_right(starts, addr) - 1
    if i >= 0 and images[i][0] <= addr < images[i][1]:
        path = images[i][2]
        f = os.path.join(dsc, path.lstrip('/'))
        return f'{os.path.basename(path)}: ' + (nearest(symbols(f), addr) if os.path.exists(f) else '?')
    best = None
    for base, path in darling:
        if base <= addr and (best is None or base > best[0]):
            best = (base, path)
    if best:
        name = os.path.basename(best[1])
        lp = local.get(name)
        if lp:
            # Darling binaries: text vmaddr 0 for dyld, otherwise read from the binary
            return f'{name}: ' + nearest(symbols(lp, best[0]), addr)
        return f'{name}+0x{addr - best[0]:x}'
    return '?'

for line in text.splitlines():
    if line.startswith('Darling:'):
        addrs = re.findall(r'0x[0-9A-Fa-f]{6,}', line)
        print(line, '  <- ' + ' | '.join(describe(int(a, 16)) for a in addrs[:1]) if addrs else '')
