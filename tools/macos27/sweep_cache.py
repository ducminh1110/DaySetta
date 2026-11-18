#!/usr/bin/env python3
"""Whole-cache instruction census for the ds-exec lowering decoder (cache-sweep).

Walks __TEXT,__text of every image in the macOS 27 arm64e split cache (read-only,
via daylight/tools/apicatalog/machoinfo.py, same reader fixups.py uses), counts
every 32-bit word with the REAL ds::exec decoder (sweep_decode.cpp, linked against
src/exec/lowering.cpp) and classifies the words the decoder rejects with llvm-mc:

  gate: llvm-mc --disassemble --mcpu=generic --mattr=+aes,+sha2,+crc rejects the
  word -> it needs a feature beyond ARMv8.0+aes/sha2/crc (the lowering target, see
  src/exec/lowering.hpp "host is ARMv8.0" comments).  A second llvm-mc pass with
  --mattr=+all names the mnemonic; words even +all rejects are data/undefined.

Output: JSON census + a human summary.  Every count comes from actual cache bytes.
Usage: sweep_cache.py [--cache PATH] [--limit N] [--out census.json]
"""
import json
import os
import re
import subprocess
import sys
import time

import numpy as np

sys.path.insert(0, "/home/ubuntu/daylight/tools/apicatalog")
import machoinfo

CACHE = "/home/ubuntu/macos-re/cache-arm64e/dyld_shared_cache_arm64e"
BRIDGE = os.environ.get("SWEEP_BRIDGE", "/tmp/opencode/cache-sweep/sweep_decode")
LLVM_MC = "llvm-mc"
GATE = ["--triple=aarch64", "--mcpu=generic", "--mattr=+aes,+sha2,+crc"]
# only "invalid" lines emit no text; "potentially undefined" ones still disassemble
WARN = re.compile(r"<stdin>:(\d+):\d+: warning: invalid instruction encoding")


def byte_lines(words):
    b = np.asarray(words, dtype="<u4").tobytes()
    return "".join("0x%02x 0x%02x 0x%02x 0x%02x\n" % (b[i], b[i + 1], b[i + 2], b[i + 3])
                   for i in range(0, len(b), 4))


def llvm_lines(words, flags):
    """(ok_texts, bad_lines) from llvm-mc --disassemble; bad_lines are 0-based input indexes."""
    p = subprocess.run([LLVM_MC, "--disassemble"] + flags, input=byte_lines(words),
                       capture_output=True, text=True)
    bad = {}
    for m in WARN.finditer(p.stderr):
        bad[int(m.group(1)) - 1] = "invalid instruction encoding"
    texts = [l.strip() for l in p.stdout.splitlines() if l.strip()]
    if len(texts) != len(words) - len(bad):
        raise RuntimeError("llvm-mc line mismatch: %d texts, %d words, %d bad" % (len(texts), len(words), len(bad)))
    return texts, bad


def bridge(uniq, cnt):
    inp = "".join("%08x %d\n" % (w, c) for w, c in zip(uniq, cnt))
    p = subprocess.run([BRIDGE], input=inp, capture_output=True, text=True)
    if p.returncode != 0:
        raise RuntimeError("sweep_decode failed: " + p.stderr)
    out = {}
    for line in p.stdout.splitlines():
        w, c, op, detail = (line.split(" ", 3) + [""])[:4]
        out[int(w, 16)] = (op, detail.strip())
    if len(out) != len(uniq):
        raise RuntimeError("bridge line mismatch")
    return out


def gnu_names(words):
    """Fallback naming for words llvm-mc +all rejects: GNU binutils knows newer
    extensions (e.g. FEAT_THE tchangef).  {word: text} for the ones it decodes."""
    src = "".join(".inst 0x%08x\n" % w for w in words)
    d = "/tmp/cache-sweep-gnu"
    os.makedirs(d, exist_ok=True)
    with open(d + "/words.s", "w") as f:
        f.write(src)
    subprocess.run(["aarch64-linux-gnu-as", "-o", d + "/words.o", d + "/words.s"], check=True)
    p = subprocess.run(["aarch64-linux-gnu-objdump", "-d", d + "/words.o"], capture_output=True, text=True, check=True)
    out = {}
    for m in re.finditer(r"^\s*[0-9a-f]+:\t([0-9a-f]{8}) \t(\S+)\s*(.*)$", p.stdout, re.M):
        out[int(m.group(1), 16)] = (m.group(2) + " " + m.group(3)).strip()
    return out


def first_positions(words, wanted):
    """{word: index of first occurrence} for `wanted` words present in `words`."""
    pos = np.flatnonzero(np.isin(words, wanted))
    if not len(pos):
        return {}
    wds = words[pos]
    _, idx = np.unique(wds, return_index=True)
    return {int(wds[i]): int(pos[i]) for i in idx}


def main(argv):
    cache, out_path, limit = CACHE, "census.json", 0
    i = 1
    while i < len(argv):
        if argv[i] == "--cache": cache = argv[i + 1]; i += 2
        elif argv[i] == "--out": out_path = argv[i + 1]; i += 2
        elif argv[i] == "--limit": limit = int(argv[i + 1]); i += 2
        else: raise SystemExit("unknown arg " + argv[i])
    t0 = time.time()
    sc = machoinfo.SplitCache(cache)
    print("cache: %d images, %d files" % (len(sc.images), 1 + len(sc.subcaches)), file=sys.stderr)

    total_words = 0
    sections = 0
    handled = {}   # word -> [count, op, example]
    gaps = {}      # word -> [count, example]
    images_done = 0
    for im in sc.images:
        imgs = sc.read_image(im)
        if not imgs:
            print("no mach-o for %s" % im.path, file=sys.stderr)
            continue
        for seg in imgs[0].segments:
            if seg.segname != "__TEXT":
                continue
            for s in seg.sections:
                if s.sectname != "__text" or not s.size:
                    continue
                blob = sc.read_address(s.addr, s.size)
                if len(blob) != s.size:
                    raise RuntimeError("short read %s __text" % im.path)
                words = np.frombuffer(blob[: s.size // 4 * 4], dtype="<u4")
                total_words += len(words)
                sections += 1
                uniq, cnt = np.unique(words, return_counts=True)
                tags = bridge(uniq, cnt)
                none_w = []
                new_handled = []
                for w, c in zip(uniq, cnt):
                    w = int(w)
                    op, _ = tags[w]
                    if op != "none":
                        if w not in handled:
                            handled[w] = [0, op, None]
                            new_handled.append(w)
                        handled[w][0] += int(c)
                    else:
                        none_w.append(w)
                if new_handled:
                    for w, pos in first_positions(words, new_handled).items():
                        handled[w][2] = [s.addr + 4 * pos, im.path]
                if none_w:
                    nu = np.array(none_w, dtype="<u4")
                    _, bad = llvm_lines(nu, GATE)
                    new_gaps = []
                    cmap = {int(w): int(c) for w, c in zip(uniq, cnt)}
                    for li in bad:
                        w = int(nu[li])
                        if w not in gaps:
                            gaps[w] = [0, None]
                            new_gaps.append(w)
                        gaps[w][0] += cmap[w]
                    if new_gaps:
                        for w, pos in first_positions(words, new_gaps).items():
                            gaps[w][1] = [s.addr + 4 * pos, im.path]
        images_done += 1
        if images_done % 200 == 0:
            print("  %d/%d images, %d words, %d handled-unique, %d gap-unique, %.0fs"
                  % (images_done, len(sc.images), total_words, len(handled), len(gaps), time.time() - t0), file=sys.stderr)
        if limit and images_done >= limit:
            break

    # name every interesting word with the full-feature disassembler
    print("naming %d handled + %d gap words with llvm-mc +all" % (len(handled), len(gaps)), file=sys.stderr)
    all_words = sorted(set(handled) | set(gaps))
    names = {}
    CH = 400000
    for off in range(0, len(all_words), CH):
        chunk = all_words[off:off + CH]
        texts, bad = llvm_lines(chunk, ["--triple=aarch64", "--mattr=+all"])
        ti = 0
        for li, w in enumerate(chunk):
            if li in bad:
                names[w] = ("", bad[li])
            else:
                names[w] = (texts[ti], "")
                ti += 1

    rejected = [w for w in all_words if not names[w][0]]
    if rejected:
        gn = gnu_names(rejected)
        for w, text in gn.items():
            names[w] = (text, "")
        print("gnu objdump named %d of %d llvm-rejected words" % (len(gn), len(rejected)), file=sys.stderr)

    lowered = {}   # op -> {count, classes: {mnemonic: {count, examples}}}
    gap_classes = {}
    for w, (c, op, ex) in handled.items():
        text, _ = names[w]
        mn = text.split()[0] if text else "?"
        d = lowered.setdefault(op, {"count": 0, "classes": {}})
        d["count"] += c
        cl = d["classes"].setdefault(mn, {"count": 0, "examples": [], "words": {}})
        cl["count"] += c
        cl["words"]["%08x" % w] = c
        if ex and len(cl["examples"]) < 3:
            cl["examples"].append({"addr": "0x%x" % ex[0], "image": ex[1], "word": "%08x" % w, "asm": text})
    for w, (c, ex) in gaps.items():
        text, bad = names[w]
        mn = text.split()[0] if text else "undefined-data"
        cl = gap_classes.setdefault(mn, {"count": 0, "examples": [], "words": {}})
        cl["count"] += c
        cl["words"]["%08x" % w] = c
        if ex and len(cl["examples"]) < 3:
            cl["examples"].append({"addr": "0x%x" % ex[0], "image": ex[1], "word": "%08x" % w,
                                   "asm": text or "(+all rejects: %s)" % bad})

    census = {
        "cache": cache, "images": images_done, "text_sections": sections, "words": total_words,
        "handled_unique_words": len(handled), "gap_unique_words": len(gaps),
        "lowered": lowered, "gaps": gap_classes,
        "seconds": round(time.time() - t0, 1),
    }
    with open(out_path, "w") as f:
        json.dump(census, f, indent=1, sort_keys=True)

    must = sum(d["count"] for d in lowered.values()) + sum(c["count"] for c in gap_classes.values())
    print("images=%d __text sections=%d words=%d" % (images_done, sections, total_words))
    print("must-handle words=%d (lowered=%d, gaps=%d)" % (
        must, sum(d["count"] for d in lowered.values()), sum(c["count"] for c in gap_classes.values())))
    for op in sorted(lowered, key=lambda o: -lowered[o]["count"]):
        d = lowered[op]
        top = sorted(d["classes"].items(), key=lambda kv: -kv[1]["count"])[:12]
        print("  %-12s %12d  %s" % (op, d["count"], " ".join("%s=%d" % (m, c["count"]) for m, c in top)))
    print("gaps (decoder rejects, feature beyond baseline), by mnemonic:")
    for mn in sorted(gap_classes, key=lambda m: -gap_classes[m]["count"]):
        c = gap_classes[mn]
        ex = c["examples"][0] if c["examples"] else {}
        print("  %-16s %12d  e.g. %s %s %s" % (mn, c["count"], ex.get("addr", "?"), ex.get("word", ""), ex.get("image", "")))
    print("wrote %s (%.0fs)" % (out_path, time.time() - t0))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
