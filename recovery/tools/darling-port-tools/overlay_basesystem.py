#!/usr/bin/env python3
"""Make the user's own BaseSystem (macOS 26.6) resources visible inside the Darling prefix.

Apple's frameworks come from the dyld shared cache, but their bundles' resources (Info.plist, Assets.car, nibs,
strings), CoreServices bundles (SystemAppearance, CoreTypes, CoreGlyphs), fonts, certificates etc. are read from
/System/Library at run time. This links them into the prefix's upper layer (~/.darling) as symlinks into the
BaseSystem tree (seen from inside the prefix through /Volumes/SystemRoot). Nothing is copied or redistributed.

Darling's own pieces win where both exist and Darling needs its version: launchd job plists (Apple's daemons
cannot run here), SystemVersion.plist (the prefix reports Darling's version), and executables Darling ships
(launchservicesd stub, securityd...). For everything else inside bundles Apple's file wins.

  overlay_basesystem.py [--basesystem ~/basesystem] [--prefix ~/.darling] [--dry-run]
Run with the container stopped (darling shutdown). Re-running is idempotent; --undo removes the links it made.
"""
import argparse, os, subprocess, sys

TOPS = ['System/Library', 'Library/Apple', 'usr/share']
KEEP_DARLING = (  # path prefixes (relative to /) where Darling's file/dir is never replaced
    'System/Library/LaunchDaemons', 'System/Library/LaunchAgents', 'Library/LaunchDaemons',
    'Library/LaunchAgents', 'System/Library/CoreServices/SystemVersion.plist',
    'System/Library/CoreServices/launchservicesd', 'System/Library/CoreServices/coreservicesd',
    'System/Library/User Template', 'usr/share/zoneinfo', 'usr/share/terminfo', 'usr/share/locale',
    'usr/share/man',
)
MARK = '.darling-basesystem-links'
# Copied instead of linked: fontd only registers system fonts whose real path is under /System/Library/Fonts,
# and a symlink resolves to /Volumes/SystemRoot/... (92 MB, the user's own copy).
COPY_DIRS = ('System/Library/Fonts',)


def keep_darling(rel):
    return any(rel == k or rel.startswith(k + '/') for k in KEEP_DARLING)


def is_executable_file(p):
    return os.path.isfile(p) and os.access(p, os.X_OK) and '.' not in os.path.basename(p)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--basesystem', default=os.path.expanduser('~/basesystem'))
    ap.add_argument('--prefix', default=os.path.expanduser('~/.darling'))
    ap.add_argument('--lower', default='/usr/local/libexec/darling')
    ap.add_argument('--dry-run', action='store_true')
    ap.add_argument('--undo', action='store_true')
    a = ap.parse_args()
    marks = os.path.join(a.prefix, MARK)

    if a.undo:
        for line in open(marks):
            p = line.rstrip('\n')
            if os.path.islink(p):
                os.unlink(p)
            if os.path.lexists(p + '.darling-orig'):
                os.rename(p + '.darling-orig', p)
        os.unlink(marks)
        return

    made = []
    host_root = '/Volumes/SystemRoot' + os.path.abspath(a.basesystem)

    def exists_in_prefix(rel):
        return os.path.lexists(os.path.join(a.prefix, rel)) or os.path.lexists(os.path.join(a.lower, rel))

    def isdir_in_prefix(rel):
        up = os.path.join(a.prefix, rel)
        if os.path.lexists(up):
            return os.path.isdir(up) and not os.path.islink(up)
        return os.path.isdir(os.path.join(a.lower, rel)) and not os.path.islink(os.path.join(a.lower, rel))

    def link(rel):
        dst = os.path.join(a.prefix, rel)
        if rel in COPY_DIRS:
            if a.dry_run:
                made.append(dst)
                return
            if os.path.islink(dst):
                os.unlink(dst)
            subprocess.run(['cp', '-a', os.path.join(a.basesystem, rel) + '/.', dst + '/'] if os.path.isdir(dst) else
                           ['cp', '-a', os.path.join(a.basesystem, rel), dst], check=True)
            made.append(dst)
            return
        target = os.path.join(host_root, rel)
        if os.path.islink(dst) and os.readlink(dst) == target:
            return
        if a.dry_run:
            made.append(dst)
            return
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        if os.path.lexists(dst):
            os.rename(dst, dst + '.darling-orig')
        os.symlink(target, dst)
        made.append(dst)

    def walk(rel):
        src = os.path.join(a.basesystem, rel)
        for name in sorted(os.listdir(src)):
            r = os.path.join(rel, name)
            s = os.path.join(src, name)
            if keep_darling(r):
                continue
            if not exists_in_prefix(r):
                link(r)  # a whole subtree Darling does not have
            elif os.path.isdir(s) and not os.path.islink(s) and isdir_in_prefix(r):
                walk(r)
            elif os.path.isdir(s):
                continue  # dir vs file/link clash: leave Darling's
            elif is_executable_file(os.path.join(a.lower, r)) or is_executable_file(os.path.join(a.prefix, r)):
                continue  # Darling ships its own program here
            else:
                link(r)  # resource file inside a bundle etc.: Apple's version

    for top in TOPS:
        if os.path.isdir(os.path.join(a.basesystem, top)):
            walk(top)
    if not a.dry_run:
        with open(marks, 'a') as f:
            for m in made:
                f.write(m + '\n')
    print('%s %d links' % ('would make' if a.dry_run else 'made', len(made)))


if __name__ == '__main__':
    main()
