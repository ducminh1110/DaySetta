#!/usr/bin/env python3
"""WP1b: scan SkyLight (macOS 27.0.1 / 26A434 arm64e cache) for MIG subsystem descriptors.

Layout (confirmed against /home/ubuntu/daylight/tools/mig_scan.py lines 47-58, struct '<QiiIIQ'
then '<QQIIQII' per routine): header = server ptr@0, start i32@8, end i32@12, maxsize u32@16,
pad@20, reserved ptr@24; routines at +32, 40 bytes each: impl@0, stub@8, argc@16,
descr_count@20, arg_descr@24, max_reply_msg@32.
Pointers are decoded with fixups.Fixups.resolve (slide info v5), PAC bits never guessed.
Usage: python3 -I tools/macos27/scan_mig.py [--text-bounds]
"""
import json
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import fixups as F

ROOT = os.path.dirname(os.path.dirname(HERE))
OUT = os.path.join(ROOT, "artifacts", "macos-27", "mig-routines.json")
BASE26 = "/home/ubuntu/daylight/protocol/macos-26.6-x86_64.json"
SKYLIGHT, SKY_UUID = 0x187093000, "8A3B348E4637368592D06CBC2F36A234"
DATA_SEGS = ("__DATA_CONST", "__AUTH_CONST", "__DATA")


def load_sky(fx):
    sky = fx.image(SKYLIGHT)
    if sky is None or sky.uuid != SKY_UUID:
        sys.exit("SkyLight not found at 0x%x or UUID mismatch (%s)" % (SKYLIGHT, sky and sky.uuid))
    text = next(s for s in sky.segments if s.segname == "__TEXT")
    return sky, text.vmaddr, text.vmaddr + text.vmsize


def main(argv):
    fx = F.Fixups()
    sky, t0, t1 = load_sky(fx)
    if "--text-bounds" in argv:
        print("0x%x 0x%x" % (t0, t1))
        return 0
    in_text = lambda a: a is not None and t0 <= a < t1
    rej = {}
    bump = lambda k: rej.__setitem__(k, rej.get(k, 0) + 1)
    found, scanned = [], []
    for sec in sky.sections:
        if sec.segname not in DATA_SEGS or sec.size < 72:
            continue
        data = fx.sc.read_address(sec.addr, sec.size)
        scanned.append("%s,%s" % (sec.segname, sec.sectname))
        for o in range(0, len(data) - 72 + 1, 8):
            sraw, start, end, maxsize, _p, resv = struct.unpack_from("<QiiIIQ", data, o)
            n = end - start
            if sraw == 0 or resv != 0 or not (0 < n <= 4096) or not (0 < start < 0x7FFFFFFF) or maxsize == 0:
                continue  # cheap pre-filter, not counted as a candidate
            bump("candidates_after_prefilter")
            if not in_text(fx.resolve(sec.addr + o)):
                bump("server_ptr_not_in_text")
                continue
            if o + 32 + n * 40 > len(data):
                bump("routines_past_section_end")
                continue
            routines, why = [], None
            for i in range(n):
                ro = o + 32 + i * 40
                impl, stub, argc, dc, adescr, maxrep = struct.unpack_from("<QQIIQI", data, ro)
                va = sec.addr + ro
                st = fx.resolve(va + 8) if stub else 0
                im = fx.resolve(va) if impl else 0
                if adescr != 0:
                    why = "arg_descr_nonzero"
                elif (stub and not in_text(st)) or (stub == 0) != (st == 0):
                    why = "stub_not_in_text"
                elif (impl and not in_text(im)) or (impl == 0) != (im == 0):
                    why = "impl_not_in_text"
                elif argc > 64:
                    why = "argc_gt_64"
                if why:
                    break
                routines.append((start + i, st, im, argc, maxrep))
            if why is None and not any(r[1] for r in routines):
                why = "no_live_stub"
            if why:
                bump(why)
                continue
            found.append({"va": sec.addr + o, "start": start, "end": end,
                          "server": fx.resolve(sec.addr + o), "routines": routines})
    base = json.load(open(BASE26))
    sub26 = {}
    for r in base:
        sub26.setdefault(r["subsystem"], {})[r["id"]] = r["routine"]
    starts = {}
    for k, v in sub26.items():
        starts.setdefault(min(v), []).append(k)
    by_start = {k: v[0] for k, v in starts.items() if len(v) == 1}  # ambiguous start ids stay "?"
    h = lambda v: "0x%x" % v
    subs, diff = [], {}
    for s in sorted(found, key=lambda s: s["start"]):
        nm = by_start.get(s["start"])
        amb = starts.get(s["start"], [])
        known = sub26.get(nm, {}) if nm else ({} if len(amb) < 2 else
                {i: None for k in amb for i in sub26[k]})
        rl = []
        for rid, st, im, argc, mrep in s["routines"]:
            if st == 0:
                continue
            n = known.get(rid) if nm else None
            rl.append({"id": rid, "stub_vmaddr": h(st), "impl_vmaddr": h(im), "argc": argc,
                       "max_reply_msg": mrep, "name": n or "?",
                       "name_status": "inferred-from-26.6" if n else "unknown"})
        ids = {r["id"] for r in rl}
        subs.append({"name": nm or "?", "name_status": "inferred-from-26.6" if nm else "unknown",
                     "descriptor_vmaddr": h(s["va"]), "server_vmaddr": h(s["server"]),
                     "start": s["start"], "end": s["end"], "routine_count": len(rl),
                     "routines": rl})
        key = nm or "start_%d@%s" % (s["start"], h(s["va"]))
        diff[key] = {
            "added_ids": sorted(ids - set(known)), "removed_ids": sorted(set(known) - ids),
            "count_26_6": len(known) if (nm or not amb) else None, "count_27": len(rl)}
        if not nm and amb:
            diff[key]["note"] = "start id shared by 26.6 subsystems %s; not attributable" % amb
    for nm, v in sub26.items():
        if nm not in diff and len(starts[min(v)]) == 1:
            diff[nm] = {"added_ids": [], "removed_ids": sorted(v), "count_26_6": len(v), "count_27": 0}
    out = {"schema_version": 1,
           "source": {"cache": F.CACHE, "build": "26A434", "skylight_uuid": SKY_UUID,
                      "skylight_vmaddr": h(SKYLIGHT), "text_start": h(t0), "text_end": h(t1),
                      "baseline": BASE26, "sections_scanned": scanned, "rejected": rej},
           "subsystems": subs, "diff_vs_26_6": diff}
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w") as f:
        json.dump(out, f, indent=1)
    print("subsystems:", len(subs), "routines:", sum(s["routine_count"] for s in subs), "rejected:", rej)
    for s in subs:
        print(s["name"], s["start"], s["end"], s["routine_count"], s["descriptor_vmaddr"])
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
