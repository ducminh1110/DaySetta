"""Copy a Mach-O executable, pointing its LC_LOAD_DYLINKER at /usr/lib/dyl4 (same length)."""
import sys
data = bytearray(open(sys.argv[1], 'rb').read())
old, new = b'/usr/lib/dyld\0', b'/usr/lib/dyl4\0'
n = data.count(old)
assert n >= 1, 'no LC_LOAD_DYLINKER path found'
data = data.replace(old, new)
open(sys.argv[2], 'wb').write(data)
print(f'patched {n} occurrence(s)')
