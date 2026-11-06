# Replace the prefix's SystemAppearance.bundle symlink with a directory of links to the BaseSystem files plus
# extra .car files from ~/darltest/extra-macos27 (the user's own macOS). Run with the container stopped.
import os
H = os.path.expanduser('~')
BS = H + '/basesystem/System/Library/CoreServices/SystemAppearance.bundle'
DST = H + '/.darling/System/Library/CoreServices/SystemAppearance.bundle'
VIEW = '/Volumes/SystemRoot'
if os.path.islink(DST):
    os.unlink(DST)
for root, dirs, files in os.walk(BS):
    rel = os.path.relpath(root, BS)
    os.makedirs(os.path.join(DST, rel), exist_ok=True)
    for f in files:
        d = os.path.join(DST, rel, f)
        if not os.path.lexists(d):
            os.symlink(VIEW + os.path.join(root, f), d)
extra = H + '/darltest/extra-macos27'
for f in os.listdir(extra):
    if f.endswith('.car'):
        d = os.path.join(DST, 'Contents/Resources', f)
        if not os.path.lexists(d):
            os.symlink(VIEW + os.path.join(extra, f), d)
print(sorted(os.listdir(DST + '/Contents/Resources')))
