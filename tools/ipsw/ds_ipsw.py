#!/usr/bin/env python3
"""DaySetta IPSW component pipeline (T1). Stdlib only; wraps `ipsw` and `7z`.

Subcommands: inspect, keys, decrypt, extract, index.
Everything written goes to --store (default /home/ubuntu/macos-re/store-27), never into the repo.
Key material is written only to <store>/keys (0600) and is never printed or logged.
"""
import argparse
import ctypes
import hashlib
import json
import os
import plistlib
import re
import shutil
import struct
import subprocess
import sys
import time
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
MACOS_RE = "/home/ubuntu/macos-re"
DEF_IPSW = MACOS_RE + "/ipsw/UniversalMac_27.0.1_26A434_Restore.ipsw"
DEF_MANIFEST = MACOS_RE + "/ipsw/manifest/BuildManifest.plist"
DEF_STORE = MACOS_RE + "/store-27"
DEF_PRODUCT = "Mac14,3"
MIN_FREE = 15 << 30  # stop if less than this would remain
SECRET_RE = re.compile(r"[A-Za-z0-9+/=_-]{30,}")


class PipelineError(Exception):
    pass


# ---------------------------------------------------------------- manifest
def parse_manifest(data, product=DEF_PRODUCT):
    """BuildManifest bytes -> {version, build, device, variant, roles:{role:{path,aea}}}."""
    m = plistlib.loads(data)
    ids = [i for i in m["BuildIdentities"] if i.get("Ap,ProductType") == product]
    if not ids:
        raise PipelineError("no BuildIdentity for product type %s" % product)
    ident = ids[0]
    roles = {}
    for role, ent in ident["Manifest"].items():
        path = ent.get("Info", {}).get("Path")
        if isinstance(path, str) and (path.endswith(".dmg") or path.endswith(".dmg.aea")):
            roles[role] = {"path": path, "aea": path.endswith(".aea")}
    return {"version": m["ProductVersion"], "build": m["ProductBuildVersion"],
            "device": ident["Info"]["DeviceClass"], "variant": ident["Info"].get("Variant"),
            "product": product, "roles": roles}


def load_inspect(a):
    with open(a.manifest, "rb") as f:
        info = parse_manifest(f.read(), a.product)
    with zipfile.ZipFile(a.ipsw) as z:
        sizes = {i.filename: i.file_size for i in z.infolist()}
    for r in info["roles"].values():
        r["size"] = sizes.get(r["path"])
    return info


def resolve(info, sel):
    for role, r in info["roles"].items():
        if sel in (role, r["path"]):
            return role, r
    raise PipelineError("unknown role/member %r; have %s" % (sel, sorted(info["roles"])))


def image_name(r):
    return r["path"][:-4] if r["aea"] else r["path"]


# ---------------------------------------------------------------- helpers
def redact(s):
    return SECRET_RE.sub("<redacted>", s)


def ensure_free(path, need):
    free = shutil.disk_usage(path).free
    if free - need < MIN_FREE:
        raise PipelineError("disk: %d GB free, step needs ~%d GB, floor is %d GB; stop"
                            % (free >> 30, need >> 30, MIN_FREE >> 30))


def ipsw_bin():
    p = shutil.which("ipsw") or os.path.expanduser("~/.local/bin/ipsw")
    if not os.path.exists(p):
        raise PipelineError("ipsw CLI not found")
    return p


def run(cmd, **kw):
    return subprocess.run(cmd, capture_output=True, text=True, **kw)


def store_dir(a, *sub):
    d = os.path.join(a.store, *sub)
    os.makedirs(d, exist_ok=True)
    return d


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        while True:
            b = f.read(1 << 22)
            if not b:
                return h.hexdigest()
            h.update(b)


def copy_member(ipsw, member, dest, limit=None):
    with zipfile.ZipFile(ipsw) as z, z.open(member) as s, open(dest, "wb") as d:
        left = limit
        while left is None or left > 0:
            b = s.read(1 << 22 if left is None else min(1 << 22, left))
            if not b:
                break
            d.write(b)
            if left is not None:
                left -= len(b)


# ---------------------------------------------------------------- DMG check
def classify_dmg(head, tail, size):
    """head = first 4096+ bytes, tail = last 512 bytes. Returns kind or None.
    udif: 'koly' trailer; udif-header: 'koly' at start; apfs-raw: NXSB at 32 (decrypted AEA payload)."""
    if len(tail) >= 512 and tail[:4] == b"koly":
        return "udif-trailer"
    if head[:4] == b"koly":
        return "udif-header"
    if len(head) >= 36 and head[32:36] == b"NXSB":
        return "apfs-raw"
    return None


def verify_dmg(path):
    size = os.path.getsize(path)
    with open(path, "rb") as f:
        head = f.read(4096)
        f.seek(max(0, size - 512))
        tail = f.read(512)
    kind = classify_dmg(head, tail, size)
    if not kind:
        raise PipelineError("%s: neither UDIF 'koly' nor raw APFS NXSB" % path)
    return {"file": os.path.basename(path), "kind": kind, "size": size, "sha256": sha256_file(path)}


# ---------------------------------------------------------------- inspect
def cmd_inspect(a):
    info = load_inspect(a)
    if a.json:
        print(json.dumps(info, indent=1))
        return 0
    print("%s %s build %s device %s variant %s" % (info["product"], info["version"], info["build"],
                                                   info["device"], info["variant"]))
    for role, r in sorted(info["roles"].items()):
        print("%-22s %-26s %12s AEA=%s" % (role, r["path"], r["size"], "yes" if r["aea"] else "no"))
    return 0


# ---------------------------------------------------------------- keys
def keyfile(a, info, r):
    return os.path.join(store_dir(a, "keys"), "%s__%s.key" % (info["build"], image_name(r)))


def log_line(a, name, rec):
    rec["ts"] = time.strftime("%Y-%m-%dT%H:%M:%S")
    with open(os.path.join(store_dir(a, "logs"), name), "a") as f:
        f.write(json.dumps(rec) + "\n")


def get_key(aea_path, a):
    """Try sources in order. Returns (b64key|None, source, status)."""
    ipsw = ipsw_bin()
    tries = []
    if a.pem and os.path.exists(a.pem):
        tries.append(("local-pem", ["--pem", a.pem]))
    if a.pem_db and os.path.exists(a.pem_db):
        tries.append(("local-pem-db", ["--pem-db", a.pem_db]))
    # ipsw default chain: PEM embedded in the AEA / ipsw built-in, then Apple key service (wkms)
    tries.append(("ipsw-default", []))
    status = []
    for src, extra in tries:
        p = run([ipsw, "fw", "aea", "-k"] + extra + [aea_path], timeout=180)
        m = re.search(r"base64:(\S+)", p.stdout)
        if p.returncode == 0 and m:
            return m.group(1), src, "rc=0"
        status.append("%s rc=%d %s" % (src, p.returncode, redact((p.stdout + p.stderr)[-200:]).strip()))
    return None, tries[-1][0], "; ".join(status)


def cmd_keys(a):
    info = load_inspect(a)
    tmp = store_dir(a, "tmp")
    rc = 0
    for role, r in sorted(info["roles"].items()):
        if not r["aea"] or (a.image and a.image not in (role, r["path"])):
            continue
        kf = keyfile(a, info, r)
        head = os.path.join(tmp, "head.aea")
        copy_member(a.ipsw, r["path"], head, limit=32 << 20)  # AEA metadata is at the start
        try:
            key, src, st = get_key(head, a)
        finally:
            os.remove(head)
        if key:
            fd = os.open(kf, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
            with os.fdopen(fd, "w") as f:
                f.write(key + "\n")
            os.chmod(kf, 0o600)
        else:
            rc = 1
        log_line(a, "keys.log", {"role": role, "image": r["path"], "build": info["build"],
                                 "source": src, "ok": bool(key), "status": st})
        print("%-20s %-24s key=%s source=%s %s" % (role, r["path"], "stored" if key else "FAILED", src, st))
    return rc


# ---------------------------------------------------------------- decrypt
def cmd_decrypt(a):
    info = load_inspect(a)
    role, r = resolve(info, a.image)
    name = image_name(r)
    out_dir = store_dir(a, "dmg")
    final = os.path.join(out_dir, name)
    tmp = store_dir(a, "tmp")
    ensure_free(a.store, int((r["size"] or 0) * 2.3))  # aea + decrypted (~1.2x) transient
    member = os.path.join(tmp, os.path.basename(r["path"]))
    copy_member(a.ipsw, r["path"], member)
    try:
        if r["aea"]:
            kf = keyfile(a, info, r)
            if not os.path.exists(kf):
                raise PipelineError("no stored key for %s; run `keys` first" % r["path"])
            with open(kf) as f:
                key = f.read().strip()
            sub = os.path.join(tmp, "dec-" + name)
            os.makedirs(sub, exist_ok=True)
            p = run([ipsw_bin(), "fw", "aea", "-b", key, "-o", sub, member], timeout=3600)
            log_line(a, "decrypt.log", {"image": r["path"], "rc": p.returncode,
                                        "out": redact((p.stdout + p.stderr)[-300:])})
            produced = os.path.join(sub, name)
            if p.returncode or not os.path.exists(produced):
                raise PipelineError("ipsw fw aea failed rc=%d: %s" % (p.returncode, redact(p.stderr[-300:])))
            os.replace(produced, final)
            os.rmdir(sub)
        else:
            os.replace(member, final)
            member = None
    finally:
        if member and os.path.exists(member):
            os.remove(member)
    rec = verify_dmg(final)
    rec.update({"role": role, "member": r["path"], "build": info["build"], "encrypted_source": r["aea"]})
    with open(final + ".verify.json", "w") as f:
        json.dump(rec, f, indent=1)
    print("%s: %s %s bytes sha256=%s" % (name, rec["kind"], rec["size"], rec["sha256"]))
    return 0


# ---------------------------------------------------------------- decmpfs (LZBITMAP)
DECMPFS_LZBITMAP_ATTR, DECMPFS_LZBITMAP_RSRC = 13, 14
_zbm = None


def load_zbm(store):
    """Compile vendored libzbitmap.c (MIT, blacktop/lzfse-cgo) and load it with ctypes."""
    global _zbm
    if _zbm:
        return _zbm
    src = os.path.join(HERE, "vendor", "libzbitmap.c")
    so = os.path.join(store, "tmp", "libzbitmap.so")
    os.makedirs(os.path.dirname(so), exist_ok=True)
    if not os.path.exists(so) or os.path.getmtime(so) < os.path.getmtime(src):
        p = run(["gcc", "-O2", "-shared", "-fPIC", "-o", so, src])
        if p.returncode:
            raise PipelineError("gcc failed building libzbitmap: " + p.stderr[-300:])
    lib = ctypes.CDLL(so)
    lib.zbm_decompress.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_char_p, ctypes.c_size_t,
                                   ctypes.POINTER(ctypes.c_size_t)]
    lib.zbm_decompress.restype = ctypes.c_int
    _zbm = lib
    return lib


def zbm_block(lib, blk, want):
    buf = ctypes.create_string_buffer(want)
    n = ctypes.c_size_t(0)
    err = lib.zbm_decompress(buf, want, blk, len(blk), ctypes.byref(n))
    if err:
        raise PipelineError("zbm_decompress error %d" % err)
    return buf.raw[:n.value]


def parse_decmpfs_header(b):
    """16-byte decmpfs xattr header -> (type, uncompressed_size)."""
    if len(b) < 16 or b[:4] != b"fpmc":
        raise PipelineError("bad decmpfs header")
    return struct.unpack("<IQ", b[4:16])


def rsrc_blocks(rsrc):
    """Type 12/14 resource fork: LE u32 offset table (first entry = table size); returns block slices."""
    first = struct.unpack("<I", rsrc[:4])[0]
    if first % 4 or first < 8 or first > len(rsrc):
        raise PipelineError("bad rsrc offset table")
    offs = struct.unpack("<%dI" % (first // 4), rsrc[:first])
    return [rsrc[offs[i]:offs[i + 1]] for i in range(len(offs) - 1)]


def decode_lzbitmap_rsrc(rsrc, usize, lib):
    out = []
    left = usize
    for blk in rsrc_blocks(rsrc):
        want = min(left, 0x10000)
        if blk[:4] == b"ZBM\x09":
            d = zbm_block(lib, blk, want)
        elif len(blk) == want + 1 and blk[0] & 0x0F == 0x0F:  # stored block: 0xFF marker + raw bytes
            d = blk[1:]
        else:
            raise PipelineError("unknown rsrc block type 0x%02x len %d" % (blk[0] if blk else 0, len(blk)))
        if len(d) != want:
            raise PipelineError("block size %d != %d" % (len(d), want))
        out.append(d)
        left -= want
    if left:
        raise PipelineError("short data: %d bytes missing" % left)
    return out


def fix_decmpfs(dest, store):
    """7z leaves decmpfs files as empty file + ':com.apple.decmpfs' + ':com.apple.ResourceFork' streams.
    Decode them in place; delete the two consumed stream files. Returns (done, failures)."""
    done, fails = 0, []
    lib = None
    for root, _dirs, files in os.walk(dest):
        for fn in files:
            if not fn.endswith(":com.apple.decmpfs"):
                continue
            base = os.path.join(root, fn[:-len(":com.apple.decmpfs")])
            xa = os.path.join(root, fn)
            rs = base + ":com.apple.ResourceFork"
            try:
                with open(xa, "rb") as f:
                    hdr = f.read()
                ctype, usize = parse_decmpfs_header(hdr)
                lib = lib or load_zbm(store)
                if ctype == DECMPFS_LZBITMAP_RSRC:
                    with open(rs, "rb") as f:
                        chunks = decode_lzbitmap_rsrc(f.read(), usize, lib)
                elif ctype == DECMPFS_LZBITMAP_ATTR:
                    chunks = [zbm_block(lib, hdr[16:], usize)]
                else:
                    raise PipelineError("unsupported decmpfs type %d" % ctype)
                with open(base, "wb") as f:
                    for c in chunks:
                        f.write(c)
                os.remove(xa)
                if os.path.exists(rs):
                    os.remove(rs)
                done += 1
            except (PipelineError, OSError) as e:
                fails.append((os.path.relpath(base, dest), str(e)))
    return done, fails


# ---------------------------------------------------------------- extract
def cmd_extract(a):
    dmg = a.dmg if os.path.isabs(a.dmg) else os.path.join(a.store, "dmg", a.dmg)
    if not os.path.exists(dmg):
        raise PipelineError("no such image: " + dmg)
    dest = a.dest if os.path.isabs(a.dest) else os.path.join(a.store, "root", a.dest)
    os.makedirs(dest, exist_ok=True)
    name = os.path.basename(dmg)
    logs = store_dir(a, "logs")
    # full listing is always saved (the rest of the image is "listed, not extracted")
    p = run(["7z", "l", "-slt", dmg], timeout=3600)
    with open(os.path.join(logs, name + ".listing.txt"), "w") as f:
        f.write(p.stdout)
    ensure_free(a.store, 8 << 30)
    cmd = ["7z", "x", "-y", "-snl", "-snld", "-bso0", "-bsp0", "-o" + dest, dmg] + ["-x!" + x for x in a.exclude] + a.paths
    p = run(cmd, timeout=7200)
    with open(os.path.join(logs, name + ".7z.log"), "w") as f:
        f.write(p.stdout + p.stderr)
    unsupported = len(re.findall(r"Unsupported Method", p.stderr + p.stdout))
    done, fails = fix_decmpfs(dest, a.store)
    for rel, why in fails[:20]:
        print("FAIL", rel, why, file=sys.stderr)
    other = []
    relinked = 0
    for l in (p.stdout + p.stderr).splitlines():
        m = re.match(r"ERROR: Dangerous link via another link was ignored : (.+) : (.+)$", l)
        if m:  # 7z refuses link-to-link; recreate it (target is stored text, never followed)
            lp = os.path.join(dest, m.group(1))
            if os.path.realpath(os.path.dirname(lp)).startswith(os.path.realpath(dest) + os.sep) \
                    and (not os.path.lexists(lp) or (os.path.isfile(lp) and not os.path.islink(lp)
                                                      and os.path.getsize(lp) == 0)):
                if os.path.lexists(lp):
                    os.remove(lp)  # 7z leaves an empty placeholder file
                os.symlink(m.group(2), lp)
                relinked += 1
                continue
        if l.startswith("ERROR") and "Unsupported Method" not in l:
            other.append(l)
    for l in other[:20]:
        print(l, file=sys.stderr)
    rec = {"image": name, "dest": dest, "7z_rc": p.returncode, "unsupported_method_7z": unsupported,
           "decmpfs_decoded": done, "decmpfs_failed": len(fails), "other_7z_errors": len(other), "links_recreated": relinked,
           "paths": a.paths, "excluded": a.exclude}
    log_line(a, "extract.log", rec)
    print(json.dumps(rec))
    return 1 if fails or other or unsupported != done + len(fails) else 0


# ---------------------------------------------------------------- index
CPU = {0x0100000C: "arm64", 0x01000007: "x86_64", 0x0C: "arm", 0x07: "i386"}


def cpu_name(cputype, sub):
    n = CPU.get(cputype, "cpu0x%x" % cputype)
    s = sub & 0x00FFFFFF
    if n == "arm64" and s == 2:
        return "arm64e"
    if n == "x86_64" and s not in (3, 8):
        return "x86_64:%d" % s
    return n


def macho_info(head):
    """First bytes of a file -> {'archs':[...], 'filetype':int|None} or None."""
    if len(head) < 8:
        return None
    m = head[:4]
    if m in (b"\xcf\xfa\xed\xfe", b"\xce\xfa\xed\xfe"):
        if len(head) < 16:
            return None
        ct, cs, ft = struct.unpack("<iII", head[4:16])
        return {"archs": [cpu_name(ct & 0xFFFFFFFF, cs)], "filetype": ft}
    if m in (b"\xca\xfe\xba\xbe", b"\xca\xfe\xba\xbf"):
        n = struct.unpack(">I", head[4:8])[0]
        sz = 20 if m == b"\xca\xfe\xba\xbe" else 32
        if n == 0 or n > 8 or len(head) < 8 + n * sz:  # small n also rejects Java class files
            return None
        archs = []
        for i in range(n):
            ct, cs = struct.unpack(">II", head[8 + i * sz:16 + i * sz])
            archs.append(cpu_name(ct, cs))
        return {"archs": archs, "filetype": None}
    return None


def app_info(app_dir):
    for rel in ("Contents/Info.plist", "Info.plist"):
        p = os.path.join(app_dir, rel)
        if os.path.isfile(p) and not os.path.islink(p):
            try:
                with open(p, "rb") as f:
                    d = plistlib.load(f)
            except Exception:
                return None
            return {"bundle_id": d.get("CFBundleIdentifier"), "executable": d.get("CFBundleExecutable"),
                    "plist": rel}
    return None


def build_index(root):
    files, apps = [], []
    for cur, dirs, names in os.walk(root):
        for d in list(dirs):
            if d.endswith(".app"):
                full = os.path.join(cur, d)
                ai = app_info(full)
                if ai:
                    ai["path"] = os.path.relpath(full, root)
                    exe = ai["executable"]
                    for sub in ("Contents/MacOS", "."):
                        ep = os.path.join(full, sub, exe) if exe else None
                        if ep and os.path.isfile(ep) and not os.path.islink(ep):
                            with open(ep, "rb") as f:
                                mi = macho_info(f.read(4096))
                            ai["exec_archs"] = mi["archs"] if mi else None
                            break
                    apps.append(ai)
        for n in names:
            if ":com.apple." in n:
                continue
            full = os.path.join(cur, n)
            rel = os.path.relpath(full, root)
            if os.path.islink(full):
                files.append({"path": rel, "link": os.readlink(full)})
                continue
            if not os.path.isfile(full):
                continue
            ent = {"path": rel, "size": os.path.getsize(full)}
            with open(full, "rb") as f:
                mi = macho_info(f.read(4096))
            if mi:
                ent["macho"] = mi
                ent["sha256"] = sha256_file(full)
            files.append(ent)
    files.sort(key=lambda e: e["path"])
    apps.sort(key=lambda e: e["path"])
    return files, apps


def summarize(files, apps):
    arch = {}
    for f in files:
        if "macho" in f:
            k = "+".join(f["macho"]["archs"])
            arch[k] = arch.get(k, 0) + 1
    return {"files": sum(1 for f in files if "link" not in f), "symlinks": sum(1 for f in files if "link" in f),
            "macho": sum(1 for f in files if "macho" in f), "macho_by_arch": arch, "apps": len(apps)}


def cmd_index(a):
    root = os.path.join(a.store, "root")
    files, apps = build_index(root)
    out = {"schema": 1, "root": root, "files": files, "apps": apps, "counts": summarize(files, apps)}
    dst = os.path.join(a.store, "manifest.json")
    with open(dst, "w") as f:
        json.dump(out, f)
    print(json.dumps(out["counts"]))
    return 0


# ---------------------------------------------------------------- cli
def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    p.add_argument("--ipsw", default=DEF_IPSW)
    p.add_argument("--manifest", default=DEF_MANIFEST)
    p.add_argument("--store", default=DEF_STORE)
    p.add_argument("--product", default=DEF_PRODUCT)
    sub = p.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("inspect")
    s.add_argument("--json", action="store_true")
    s.set_defaults(fn=cmd_inspect)
    s = sub.add_parser("keys")
    s.add_argument("--image", help="role or member path (default: every AEA image)")
    s.add_argument("--pem")
    s.add_argument("--pem-db", default=os.path.expanduser("~/.config/ipsw/aea-pem-db.json"))
    s.set_defaults(fn=cmd_keys)
    s = sub.add_parser("decrypt")
    s.add_argument("image", help="role (e.g. Cryptex1,SystemOS) or member path")
    s.set_defaults(fn=cmd_decrypt)
    s = sub.add_parser("extract")
    s.add_argument("dmg", help="file in <store>/dmg or absolute path")
    s.add_argument("--dest", required=True, help="relative to <store>/root, or absolute")
    s.add_argument("--exclude", action="append", default=[])
    s.add_argument("paths", nargs="*", help="archive-relative paths to extract (default: all)")
    s.set_defaults(fn=cmd_extract)
    s = sub.add_parser("index")
    s.set_defaults(fn=cmd_index)
    a = p.parse_args(argv)
    try:
        return a.fn(a)
    except PipelineError as e:
        print("ERROR:", e, file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
