#!/bin/sh
# fpwalk.sh PID TID: frame-pointer backtrace of one thread (gdb attach), each return address with its mapping,
# and libsystem_kernel addresses symbolized from Darling's installed dylib.
P=$1; T=$2
out=$(sudo -n gdb -batch -nx -p $P \
	-ex "python
import gdb
for th in gdb.selected_inferior().threads():
    if th.ptid[1] == $T:
        th.switch()
pc = int(gdb.parse_and_eval('\$pc')); bp = int(gdb.parse_and_eval('\$rbp'))
print('FRAME %x' % pc)
inf = gdb.selected_inferior()
for i in range(80):
    if bp == 0: break
    try:
        m = inf.read_memory(bp, 16).tobytes()
    except Exception:
        break
    nbp = int.from_bytes(m[:8], 'little'); ra = int.from_bytes(m[8:], 'little')
    print('FRAME %x' % ra)
    if nbp <= bp: break
    bp = nbp
end" 2>/dev/null | grep '^FRAME' | cut -d' ' -f2)
sudo -n python3 - $P $out <<'PY'
import sys, bisect, subprocess
pid = sys.argv[1]; frames = [int(x, 16) for x in sys.argv[2:]]
maps = []
for l in open(f"/proc/{pid}/maps"):
    p = l.split(); a, b = (int(x, 16) for x in p[0].split("-"))
    maps.append((a, b, int(p[2], 16), p[5] if len(p) > 5 else ""))
K = "/usr/local/libexec/darling/usr/lib/system/libsystem_kernel.dylib"
S = []
for l in subprocess.run(["llvm-nm-18", "-n", "--defined-only", K], capture_output=True, text=True).stdout.splitlines():
    q = l.split()
    if len(q) == 3 and q[1] in "tT": S.append((int(q[0], 16), q[2]))
S.sort(); keys = [s[0] for s in S]
for f in frames:
    name = "?"; off = 0
    for a, b, o, n in maps:
        if a <= f < b: name, off = n, f - a + o
    sym = ""
    if name.endswith("libsystem_kernel.dylib"):
        i = bisect.bisect_right(keys, off) - 1
        sym = f"{S[i][1]}+0x{off - S[i][0]:x}"
    print(f"{f:#x} {name.split('/')[-1]}+{off:#x} {sym}")
PY
