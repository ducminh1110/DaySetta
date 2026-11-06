#!/usr/bin/env python3
"""shadowapp.py <app path inside ~/basesystem>: a copy of the app bundle for Darling's dyld port.

Apple's executables name /usr/lib/dyld as their loader; use_dyl4.py rewrites that in a copy. The copy must stay
inside a bundle with the app's Info.plist, nibs and resources or AppKit runs it without its bundle (no main menu,
no windows). The shadow bundle under ~/darltest/bundles links everything to the BaseSystem app (as seen inside the
prefix through /Volumes/SystemRoot) except Contents/MacOS/<executable>. Prints the executable's path inside Darling.
"""
import os, plistlib, shutil, subprocess, sys

H = os.path.expanduser('~')
ROOT = os.environ.get('SHADOW_ROOT', os.path.join(H, 'basesystem'))
OUTD = os.environ.get('SHADOW_OUT', os.path.join(H, 'darltest', 'bundles'))
src = os.path.join(ROOT, sys.argv[1].strip('/'))
name = os.path.basename(src.rstrip('/'))
dst = os.path.join(OUTD, name)
view = lambda p: '/Volumes/SystemRoot' + p
info = plistlib.load(open(os.path.join(src, 'Contents', 'Info.plist'), 'rb'))
exe = info['CFBundleExecutable']

shutil.rmtree(dst, ignore_errors=True)
os.makedirs(os.path.join(dst, 'Contents', 'MacOS'))
for entry in os.listdir(os.path.join(src, 'Contents')):
    if entry != 'MacOS':
        os.symlink(view(os.path.join(src, 'Contents', entry)), os.path.join(dst, 'Contents', entry))
for entry in os.listdir(os.path.join(src, 'Contents', 'MacOS')):
    if entry != exe:
        os.symlink(view(os.path.join(src, 'Contents', 'MacOS', entry)), os.path.join(dst, 'Contents', 'MacOS', entry))
out = os.path.join(dst, 'Contents', 'MacOS', exe)
subprocess.run([sys.executable, os.path.join(H, 'darling-port-tools', 'use_dyl4.py'),
                os.path.join(src, 'Contents', 'MacOS', exe), out], check=True, stdout=subprocess.DEVNULL)
os.chmod(out, 0o755)
print(view(out))
