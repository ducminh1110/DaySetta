# tstack.py PID TID: syscall, pc and a raw stack scan of one Linux thread of a Darling process; each stack word that
# points into an executable mapping is printed with the mapping's file and offset. Run as root.
import sys, struct
pid, tid = sys.argv[1], sys.argv[2]
maps = []
for l in open(f"/proc/{pid}/maps"):
    p = l.split()
    a, b = (int(x, 16) for x in p[0].split("-"))
    maps.append((a, b, p[1], int(p[2], 16), p[5] if len(p) > 5 else ""))
def where(v):
    for a, b, perm, off, name in maps:
        if a <= v < b and "x" in perm:
            return f"{name or '[anon]'}+0x{v - a + off:x}"
    return None
sc = open(f"/proc/{pid}/task/{tid}/syscall").read().split()
print("syscall", sc)
sp, pc = int(sc[-2], 16), int(sc[-1], 16)
print("pc", hex(pc), where(pc))
mem = open(f"/proc/{pid}/mem", "rb")
mem.seek(sp)
data = mem.read(8 * 1500)
for i in range(0, len(data), 8):
    v = struct.unpack_from("<Q", data, i)[0]
    w = where(v)
    if w:
        print(f"sp+0x{i:x} {v:#x} {w}")
