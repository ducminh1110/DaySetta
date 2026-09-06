#!/usr/bin/env python3
"""Pointer decoder for the macOS 27.0.1 (26A434) arm64e dyld shared cache (WP1a).

Evidence (see artifacts/macos-27/fixups-selftest.json): every slid mapping of this cache
carries dyld_cache_slide_info version 5, so cache __DATA*/__AUTH* pointers are decoded
from slide info v5 (DYLD_CHAINED_PTR_ARM64E_SHARED_CACHE layout), not from per-image
LC_DYLD_CHAINED_FIXUPS.  Bit layouts come only from the public dyld-1378 headers:
  include/mach-o/fixup-chains.h       (formats 1, 2, 6, 7, 13)
  include/mach-o/dyld_cache_format.h  (dyld_cache_slide_info5, slide_pointer5)

API:  Fixups().resolve(vmaddr) -> int | None   (None: slot is not a chain element)
      decode_chained_ptr(raw, fmt, base=0)     (pure function)
Never executes arm64e code; PAC bits are only reported as metadata.
"""
import os
import struct
import sys

sys.path.insert(0, "/home/ubuntu/daylight/tools/apicatalog")
CACHE = "/home/ubuntu/macos-re/cache-arm64e/dyld_shared_cache_arm64e"
CACHE_BASE = 0x180000000  # first mapping address of the main cache file (observed)

F_ARM64E, F_64, F_64_OFFSET, F_ARM64E_KERNEL, F_ARM64E_SHARED_CACHE = 1, 2, 6, 7, 13


def _bits(v, lo, n):
    return (v >> lo) & ((1 << n) - 1)


def decode_chained_ptr(raw, fmt, base=0):
    """Decode one chained pointer.  `base` is added to formats whose target is an offset
    (6, 7, 13 and the auth-rebase of 1).  Returns a dict: kind, target (int|None for
    binds), next (in 4/8-byte units already scaled to bytes), plus auth/ordinal/addend."""
    if fmt in (F_ARM64E, F_ARM64E_KERNEL):  # fixup-chains.h:113-158; stride 4 (kernel) / 8
        stride = 4 if fmt == F_ARM64E_KERNEL else 8
        auth, bind, nxt = _bits(raw, 63, 1), _bits(raw, 62, 1), _bits(raw, 51, 11) * stride
        d = {"next": nxt, "auth": bool(auth)}
        if auth:
            d.update(key=_bits(raw, 49, 2), addrDiv=_bits(raw, 48, 1), diversity=_bits(raw, 32, 16))
        if bind:
            d.update(kind="bind", ordinal=_bits(raw, 0, 16), target=None,
                     addend=0 if auth else _bits(raw, 32, 19))
            if not auth and d["addend"] >> 18:  # signed 19-bit addend
                d["addend"] -= 1 << 19
        else:
            if auth:  # runtimeOffset, 32 bits
                t = _bits(raw, 0, 32) + base
            else:     # target 43 bits + high8; vmaddr for fmt 1, vm offset for fmt 7
                t = (_bits(raw, 0, 43) + (base if fmt == F_ARM64E_KERNEL else 0)) | (_bits(raw, 43, 8) << 56)
            d.update(kind="rebase", target=t)
        return d
    if fmt in (F_64, F_64_OFFSET):  # fixup-chains.h:161-170, 218-226; 4-byte stride
        nxt, bind = _bits(raw, 51, 12) * 4, _bits(raw, 63, 1)
        if bind:
            return {"kind": "bind", "target": None, "next": nxt, "ordinal": _bits(raw, 0, 24),
                    "addend": _bits(raw, 24, 8), "auth": False}
        t = _bits(raw, 0, 36) + (base if fmt == F_64_OFFSET else 0)
        return {"kind": "rebase", "target": t | (_bits(raw, 36, 8) << 56), "next": nxt, "auth": False}
    if fmt == F_ARM64E_SHARED_CACHE:  # fixup-chains.h:276-296 == dyld_cache_slide_pointer5
        d = {"kind": "rebase", "next": _bits(raw, 52, 11) * 8, "auth": bool(_bits(raw, 63, 1))}
        t = _bits(raw, 0, 34) + base
        if d["auth"]:
            d.update(diversity=_bits(raw, 34, 16), addrDiv=_bits(raw, 50, 1), keyIsData=_bits(raw, 51, 1))
        else:
            t |= _bits(raw, 34, 8) << 56
        d["target"] = t
        return d
    raise ValueError("unsupported chained pointer format %r" % fmt)


class Fixups:
    """Slide-info v5 walker over every sub-cache file of the split cache."""

    def __init__(self, path=CACHE):
        import machoinfo
        self.sc = machoinfo.SplitCache(path)
        self.slid = []  # (addr, size, file, file_off, slide_off, page_size, value_add, page_starts)
        self.versions = {}
        files = [(path, self.sc.main)] + list(self.sc.subcaches.items())
        for p, dc in files:
            for m in dc.mappings:
                if not m.slide_info_size:
                    continue
                with open(p, "rb") as f:
                    f.seek(m.slide_info_offset)
                    hdr = f.read(24)
                    ver = struct.unpack_from("<I", hdr)[0]
                    self.versions[ver] = self.versions.get(ver, 0) + 1
                    if ver != 5:
                        raise NotImplementedError("slide info v%d (only v5 seen in 26A434)" % ver)
                    _, psize, count, _pad, vadd = struct.unpack("<IIIIQ", hdr)  # dyld_cache_format.h:533
                    starts = struct.unpack("<%dH" % count, f.read(2 * count))
                self.slid.append((m.address, m.size, p, m.file_offset, psize, vadd, starts))
        self._pages = {}

    def _page(self, ent, idx):
        key = (ent[0], idx)
        if key in self._pages:
            return self._pages[key]
        addr, _size, p, foff, psize, vadd, starts = ent
        out = {}
        if starts[idx] != 0xFFFF:  # DYLD_CACHE_SLIDE_V5_PAGE_ATTR_NO_REBASE
            with open(p, "rb") as f:
                f.seek(foff + idx * psize)
                page = f.read(psize)
            off = starts[idx]
            while off + 8 <= len(page):
                raw = struct.unpack_from("<Q", page, off)[0]
                d = decode_chained_ptr(raw, F_ARM64E_SHARED_CACHE, vadd)
                d["raw"] = raw
                out[addr + idx * psize + off] = d
                if d["next"] == 0:
                    break
                off += d["next"]
        self._pages[key] = out
        return out

    def decode(self, vmaddr):
        """Full decoded dict for a slot (or None)."""
        for ent in self.slid:
            if ent[0] <= vmaddr < ent[0] + ent[1]:
                idx = (vmaddr - ent[0]) // ent[4]
                return self._page(ent, idx).get(vmaddr)
        return None

    def resolve(self, vmaddr):
        d = self.decode(vmaddr)
        return None if d is None else d["target"]

    def in_cache(self, addr):
        return self.sc.address_to_file(addr) is not None

    def image(self, vmaddr):
        for im in self.sc.images:
            if im.address == vmaddr:
                return self.sc.read_image(im)[0]
        return None


def main(argv):
    fx = Fixups()
    print("slide-info versions (mapping counts):", fx.versions)
    for a in argv[1:]:
        a = int(a, 0)
        d = fx.decode(a)
        print("0x%x -> %s" % (a, "None" if d is None else "0x%x raw=0x%x %s" % (d["target"], d["raw"], d)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
