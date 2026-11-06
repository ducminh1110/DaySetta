#!/usr/bin/env python3
"""WP1a self-test.  Run: python3 -I tools/macos27/test_fixups.py  (exit != 0 on failure)."""
import gzip
import json
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fixups as F

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(ROOT, "artifacts", "macos-27", "fixups-selftest.json")
SYMS = "/home/ubuntu/daylight/data/apple-api/symbols/symbols-macos27.0.1-arm64e.jsonl.gz"
SKYLIGHT, SKY_UUID = 0x187093000, "8A3B348E4637368592D06CBC2F36A234"
fails = []


def check(cond, msg):
    if not cond:
        fails.append(msg)
        print("FAIL:", msg)


def pack(fields):  # fields: [(value, lowbit, width)]
    r = 0
    for v, lo, w in fields:
        assert v < (1 << w)
        r |= v << lo
    return r


def t1():
    # fmt 1 (fixup-chains.h:113-158)
    d = F.decode_chained_ptr(pack([(0x123456789, 0, 43), (0xAB, 43, 8), (3, 51, 11)]), 1)
    check(d["kind"] == "rebase" and d["target"] == (0x123456789 | 0xAB << 56) and d["next"] == 24, "fmt1 rebase")
    d = F.decode_chained_ptr(pack([(0x1000, 0, 32), (0xBEEF, 32, 16), (1, 48, 1), (2, 49, 2), (5, 51, 11), (1, 63, 1)]), 1, 0x180000000)
    check(d["auth"] and d["target"] == 0x180001000 and d["diversity"] == 0xBEEF and d["addrDiv"] == 1
          and d["key"] == 2 and d["next"] == 40, "fmt1 auth rebase")
    d = F.decode_chained_ptr(pack([(0x42, 0, 16), ((1 << 19) - 2, 32, 19), (1, 62, 1)]), 1)
    check(d["kind"] == "bind" and d["ordinal"] == 0x42 and d["addend"] == -2 and d["target"] is None, "fmt1 bind")
    d = F.decode_chained_ptr(pack([(7, 0, 16), (0x99, 32, 16), (1, 48, 1), (1, 49, 2), (1, 62, 1), (1, 63, 1)]), 1)
    check(d["kind"] == "bind" and d["auth"] and d["ordinal"] == 7 and d["diversity"] == 0x99, "fmt1 auth bind")
    # fmt 2 / 6 (fixup-chains.h:161-170, 218-226)
    raw = pack([(0x1_2345_6789, 0, 36), (0x7F, 36, 8), (9, 51, 12)])
    d = F.decode_chained_ptr(raw, 2)
    check(d["target"] == (0x123456789 | 0x7F << 56) and d["next"] == 36, "fmt2 rebase")
    d = F.decode_chained_ptr(raw, 6, 0x1000)
    check(d["target"] == (0x123456789 + 0x1000 | 0x7F << 56), "fmt6 rebase offset+base")
    d = F.decode_chained_ptr(pack([(0xABCDEF, 0, 24), (200, 24, 8), (1, 63, 1), (2, 51, 12)]), 2)
    check(d["kind"] == "bind" and d["ordinal"] == 0xABCDEF and d["addend"] == 200 and d["next"] == 8, "fmt2 bind")
    # fmt 7 = ARM64E_KERNEL: arm64e layout, stride 4, offsets (fixup-chains.h:104)
    d = F.decode_chained_ptr(pack([(0x2000, 0, 43), (2, 51, 11)]), 7, 0x100000)
    check(d["target"] == 0x102000 and d["next"] == 8, "fmt7 rebase")
    d = F.decode_chained_ptr(pack([(0x2000, 0, 32), (1, 63, 1), (2, 51, 11)]), 7, 0x100000)
    check(d["target"] == 0x102000 and d["auth"], "fmt7 auth rebase")
    # fmt 13 (fixup-chains.h:276-296)
    d = F.decode_chained_ptr(pack([(0x677BD6D8, 0, 34), (1, 52, 11)]), 13, 0x180000000)
    check(d["target"] == 0x1E77BD6D8 and d["next"] == 8, "fmt13 rebase (0x100000677bd6d8)")
    d = F.decode_chained_ptr(pack([(0x10, 0, 34), (0x1234, 34, 16), (1, 50, 1), (1, 51, 1), (1, 63, 1)]), 13, 0x180000000)
    check(d["auth"] and d["diversity"] == 0x1234 and d["addrDiv"] == 1 and d["keyIsData"] == 1, "fmt13 auth")
    try:
        F.decode_chained_ptr(0, 99)
        check(False, "unknown fmt must raise")
    except ValueError:
        pass


def lc_chained_fixups(fx):
    """True/False: LC_DYLD_CHAINED_FIXUPS (0x80000034) among SkyLight's load commands."""
    located = fx.sc.address_to_file(SKYLIGHT)
    with open(located[0], "rb") as f:
        f.seek(located[1])
        ncmds, sizeofcmds = struct.unpack("<II", f.read(32)[16:24])
        f.seek(located[1] + 32)
        blob = f.read(sizeofcmds)
    off, cmds = 0, []
    for _ in range(ncmds):
        cmd, size = struct.unpack_from("<II", blob, off)
        cmds.append(cmd)
        off += size
    return 0x80000034 in cmds, ncmds


def t2():
    fx = F.Fixups()
    sky = fx.image(SKYLIGHT)
    check(sky is not None and sky.uuid == SKY_UUID, "SkyLight uuid")
    has_lc, ncmds = lc_chained_fixups(fx)
    res = {"slide_info_versions": {"v%d" % k: v for k, v in fx.versions.items()},
           "skylight_has_LC_DYLD_CHAINED_FIXUPS": has_lc, "skylight_load_commands": ncmds,
           "cache_base": hex(F.CACHE_BASE)}
    got = [s for s in sky.sections if s.sectname in ("__auth_got", "__got")]
    allslots = [s.addr + 8 * i for s in got for i in range(s.size // 8)]
    # Zero slots are unbound imports: the cache stores 0 there (no chain element).
    slots = [a for a in allslots if struct.unpack("<Q", fx.sc.read_address(a, 8))[0] != 0]
    res["got_slots_total"] = len(allslots)
    res["got_slots_zero_unbound"] = len(allslots) - len(slots)
    dec = {a: fx.decode(a) for a in slots}
    ok = {a: d for a, d in dec.items() if d is not None and fx.in_cache(d["target"] & ((1 << 56) - 1))}
    res["got_slots"] = len(slots)
    res["got_decoded"] = sum(d is not None for d in dec.values())
    res["got_target_in_mapping"] = len(ok)
    check(slots and len(ok) >= 0.95 * len(slots), "got in-mapping ratio %d/%d" % (len(ok), len(slots)))
    # exported-symbol match
    byaddr = {im.path: im.address for im in fx.sc.images}
    want = {d["target"] for d in ok.values()}
    hits = {}
    with gzip.open(SYMS, "rt") as f:
        for line in f:
            r = json.loads(line)
            a = byaddr.get(r["image"])
            if a is not None and a + r["offset"] in want:
                hits.setdefault(a + r["offset"], r["symbol"])
    res["got_targets_equal_exported_symbol"] = sum(1 for d in ok.values() if d["target"] in hits)
    res["distinct_exported_symbol_targets"] = len(hits)
    check(res["got_targets_equal_exported_symbol"] >= 20, "exported-symbol matches %d" % res["got_targets_equal_exported_symbol"])
    # objc_classlist vs D-21 formula (observation only)
    cl = [s for s in sky.sections if s.sectname == "__objc_classlist"]
    n = same = dn = 0
    for s in cl:
        for i in range(s.size // 8):
            a = s.addr + 8 * i
            raw = struct.unpack("<Q", fx.sc.read_address(a, 8))[0]
            n += 1
            d = fx.decode(a)
            if d is not None:
                dn += 1
                same += d["target"] == F.CACHE_BASE + (raw & 0xFFFFFFFF)
    res["objc_classlist"] = {"entries": n, "decoded_by_slide_info": dn, "equal_D21_formula": same}
    res["samples"] = [{"slot": hex(a), "raw": hex(d["raw"]), "decoded": hex(d["target"]), "auth": d["auth"]}
                      for a, d in list(ok.items())[:10]]
    res["evidence"] = "OBSERVED: counts from /home/ubuntu/macos-re/cache-arm64e (26A434); not a macOS 27 compatibility claim"
    res["failures"] = list(fails)
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w") as f:
        json.dump(res, f, indent=1)
        f.write("\n")
    print(json.dumps({k: v for k, v in res.items() if k != "samples"}))


t1()
t2()
print("FAILED" if fails else "OK")
sys.exit(1 if fails else 0)
