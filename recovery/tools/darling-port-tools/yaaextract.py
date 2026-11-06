#!/usr/bin/env python3
"""
Extract an Apple Archive (YAA1/AA01) stream from a sequence of macOS
`payloadv2` pbzx files, as found in the full InstallAssistant / OTA asset.

Usage:
    yaaextract.py --out DIR [--manifest FILE] payload.000 payload.001 ...

Each input file is a self-contained pbzx container:
    "pbzx" | u64 global | repeat: u64 uncompressed | u64 compressed | xz-data

The concatenation of all decompressed chunks is one continuous Apple Archive:
    magic(YAA1|AA01) | u16 header_size | fields... | blobs...
Fields use a 4-byte key whose last byte is the value type:
    1/2/4/8 -> uint of that many bytes
    P       -> 2-byte length + string
    F/G/H/I/J -> hash 4/20/32/48/64 bytes
    S/T     -> timespec 8/12 bytes
    A/B/C   -> blob size stored in 2/4/8 bytes (blob follows the header)
    *       -> flag, no value
"""
import argparse
import lzma
import os
import struct
import sys

MAGIC = (b"YAA1", b"AA01")

# last-byte -> (kind, size)
UINT = {"1": 1, "2": 2, "4": 4, "8": 8}
HASH = {"F": 4, "G": 20, "H": 32, "I": 48, "J": 64}
TIME = {"S": 8, "T": 12}
BLOB = {"A": 2, "B": 4, "C": 8}


def pbzx_chunks(path, bufsize=1 << 20):
    """Yield decompressed chunks from one pbzx file."""
    f = open(path, "rb")
    if f.read(4) != b"pbzx":
        raise RuntimeError("not pbzx: %s" % path)
    f.read(8)  # global header word
    while True:
        hdr = f.read(16)
        if not hdr:
            break
        if len(hdr) < 16:
            raise RuntimeError("truncated framing in %s" % path)
        uncomp, comp = struct.unpack(">QQ", hdr)
        raw = f.read(comp)
        if len(raw) != comp:
            raise RuntimeError("truncated payload data in %s" % path)
        if raw[:6] != b"\xfd7zXZ\x00":
            # Uncompressed (literal) block: the bytes *are* the data.
            if len(raw) != uncomp:
                sys.stderr.write("literal block %d != uncompressed %d in %s\n"
                                 % (len(raw), uncomp, path))
            yield raw
            continue
        d = lzma.LZMADecompressor(lzma.FORMAT_XZ)
        off = 0
        while not d.eof:
            work = raw[off:off + bufsize]
            if not work:
                break
            off += len(work)
            out = d.decompress(work)
            if out:
                yield out
    f.close()


class Reader:
    """Buffered reader over a sequence of chunk iterators (spans files)."""

    def __init__(self, it):
        self.it = it
        self.buf = b""
        self.pos = 0
        self.eof = False

    def _fill(self, n):
        want = n + self.pos
        while len(self.buf) < want and not self.eof:
            try:
                self.buf += next(self.it)
            except StopIteration:
                self.eof = True
        if self.pos > (1 << 22):
            self.buf = self.buf[self.pos:]
            self.pos = 0

    def read(self, n):
        self._fill(n)
        end = min(self.pos + n, len(self.buf))
        d = self.buf[self.pos:end]
        self.pos = end
        return d

    def peek(self, n):
        self._fill(n)
        return self.buf[self.pos:self.pos + n]


def parse_header(reader):
    """Return (fields, header_size, blob_total) or None at clean EOF."""
    magic = reader.peek(4)
    if not magic:
        return None
    if magic not in MAGIC:
        raise RuntimeError("bad magic %r" % magic)
    hdr = reader.read(6)
    header_size = int.from_bytes(hdr[4:6], "little")
    if header_size < 6:
        raise RuntimeError("bad header size %d" % header_size)
    body = reader.read(header_size - 6)
    fields = {}
    blobs = []
    p = 0
    end = len(body)
    while p < end:
        if p + 4 > end:
            raise RuntimeError("short field key")
        key = body[p:p + 4].decode("latin1")
        t = key[3]
        p += 4
        if t in UINT:
            n = UINT[t]
            fields[key] = body[p:p + n]
            p += n
        elif t in HASH:
            n = HASH[t]
            fields[key] = body[p:p + n]
            p += n
        elif t in TIME:
            n = TIME[t]
            fields[key] = body[p:p + n]
            p += n
        elif t == "P":
            n = int.from_bytes(body[p:p + 2], "little")
            p += 2
            fields[key] = body[p:p + n]
            p += n
        elif t in BLOB:
            n = BLOB[t]
            sz = int.from_bytes(body[p:p + n], "little")
            p += n
            fields[key] = sz
            blobs.append((key, sz))
        elif t == "*":
            fields[key] = None
        else:
            raise RuntimeError("unknown field type %r in %r" % (t, key))
    blob_total = sum(sz for _, sz in blobs)
    return fields, header_size, blobs, blob_total


def safe_join(root, rel):
    rel = rel.lstrip("/")
    dest = os.path.normpath(os.path.join(root, rel))
    if not (dest == root or dest.startswith(root + os.sep)):
        raise RuntimeError("unsafe path %r" % rel)
    return dest


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    ap.add_argument("--manifest")
    ap.add_argument("--progress", type=int, default=20000)
    ap.add_argument("--links-only", action="store_true",
                    help="create only symlinks (TYP1=L); files/dirs are skipped. Use to finish an "
                         "extraction whose symlinks were missed, without rewriting the data.")
    ap.add_argument("payloads", nargs="+")
    args = ap.parse_args()

    iters = [pbzx_chunks(p) for p in args.payloads]

    def chain():
        for it in iters:
            yield from it

    reader = Reader(chain())

    seen_keys = {}
    manifest = open(args.manifest, "w") if args.manifest else None
    dir_modes = []
    os.makedirs(args.out, exist_ok=True)

    n = 0
    nfiles = 0
    nlinks = 0
    bytes_written = 0
    while True:
        got = parse_header(reader)
        if got is None:
            break
        fields, header_size, blobs, blob_total = got
        for k in fields:
            seen_keys[k] = seen_keys.get(k, 0) + 1
        typ = fields.get("TYP1", b"?")
        typ = chr(typ[0]) if typ else "?"
        path = fields.get("PATP", b"").decode("utf-8", "surrogateescape")
        mode = int.from_bytes(fields.get("MOD2", b""), "little") & 0o7777
        # read blobs (in field order); first blob is normally DAT (file data)
        blob_data = {}
        for key, sz in blobs:
            blob_data[key] = reader.read(sz)
        dat = b""
        for key, sz in blobs:
            if key.startswith("DAT"):
                dat = blob_data[key]
                break
        # A symlink's target is the LNKP string field, not a data blob.
        link_target = fields.get("LNKP")
        if link_target is None and typ == "L":
            link_target = dat
        n += 1
        if n % args.progress == 0:
            sys.stderr.write("items=%d files=%d bytes=%d path=%s\n"
                             % (n, nfiles, bytes_written, path))
            sys.stderr.flush()
        if not path:
            continue
        dest = safe_join(args.out, path)
        try:
            if args.links_only:
                if typ == "L" and link_target is not None:
                    os.makedirs(os.path.dirname(dest), exist_ok=True)
                    if os.path.islink(dest) or os.path.exists(dest):
                        os.remove(dest)
                    os.symlink(link_target.decode("utf-8", "surrogateescape"), dest)
                    nlinks += 1
            elif typ == "D":
                os.makedirs(dest, exist_ok=True)
                # keep directories writable during extraction; real (possibly
                # read-only) modes are restored later so later entries inside
                # them can still be created.
                try:
                    os.chmod(dest, (mode | 0o700) if mode else 0o755)
                    dir_modes.append((dest, mode))
                except OSError:
                    pass
            elif typ == "F":
                os.makedirs(os.path.dirname(dest), exist_ok=True)
                with open(dest, "wb") as fh:
                    fh.write(dat)
                bytes_written += len(dat)
                nfiles += 1
                try:
                    os.chmod(dest, (mode | 0o400) if mode else 0o644)
                except OSError:
                    pass
            elif typ == "L":
                os.makedirs(os.path.dirname(dest), exist_ok=True)
                target = link_target.decode("utf-8", "surrogateescape") if link_target else ""
                if os.path.islink(dest) or os.path.exists(dest):
                    try:
                        os.remove(dest)
                    except OSError:
                        pass
                os.symlink(target, dest)
                nlinks += 1
            elif typ == "P":
                os.makedirs(os.path.dirname(dest), exist_ok=True)
                try:
                    os.mkfifo(dest, mode or 0o644)
                except FileExistsError:
                    pass
            else:
                # sockets, devices, whiteouts -> skip
                pass
        except OSError as e:
            sys.stderr.write("warn: %s: %s\n" % (path, e))
        if manifest:
            manifest.write("%s\t%s\t%s\t%d\n" % (typ, oct(mode), path, len(dat)))

    # restore the real directory modes now that every entry has been created
    # (deepest first so a parent going read-only cannot block its children).
    for dest, mode in reversed(dir_modes):
        if mode:
            try:
                os.chmod(dest, mode)
            except OSError:
                pass
    if manifest:
        manifest.close()
    sys.stderr.write("DONE items=%d files=%d links=%d bytes=%d dirs=%d\n"
                     % (n, nfiles, nlinks, bytes_written, len(dir_modes)))
    sys.stderr.write("field keys: %s\n" % sorted(seen_keys.items()))


if __name__ == "__main__":
    main()
