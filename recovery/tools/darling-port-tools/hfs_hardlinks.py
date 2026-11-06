#!/usr/bin/env python3
# Lists HFS+ hard links that `7z x` extracted as empty files.
#   hfs_hardlinks.py <7z extraction dir> <same volume mounted read-only by Linux hfsplus>  > links.txt
# Each output line: "<iNode name>\t<relative path>". 7z writes the link targets to "[HFS+ Private Data]/iNode<N>"
# (decompressed, which Linux hfsplus cannot do) but every link itself as a 0-byte file; the kernel mount resolves
# links, and its inode number of a link is N.
import os, sys
src, mnt = sys.argv[1], sys.argv[2]
priv = os.path.join(src, '[HFS+ Private Data]')
for root, dirs, files in os.walk(src):
    if root == priv:
        continue
    for name in files:
        p = os.path.join(root, name)
        if os.path.islink(p) or os.path.getsize(p) != 0:
            continue
        rel = os.path.relpath(p, src)
        try:
            st = os.stat(os.path.join(mnt, rel), follow_symlinks=False)
        except OSError:
            continue
        node = 'iNode%d' % st.st_ino
        if st.st_nlink > 1 and os.path.exists(os.path.join(priv, node)):
            print('%s\t%s' % (node, rel))
