#!/usr/bin/env python3
"""Unit tests for pure logic in ds_ipsw.py (synthetic data only, no Apple bytes). Run: python3 -I this_file."""
import ctypes
import os
import plistlib
import shutil
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ds_ipsw as d  # noqa: E402


def manifest(product="Mac14,3"):
    def ent(path):
        return {"Info": {"Path": path}}
    return plistlib.dumps({
        "ProductVersion": "27.0.1", "ProductBuildVersion": "26A434",
        "BuildIdentities": [
            {"Ap,ProductType": "Mac99,9", "Info": {"DeviceClass": "x"}, "Manifest": {"OS": ent("other.dmg")}},
            {"Ap,ProductType": product, "Info": {"DeviceClass": "j473ap", "Variant": "V"},
             "Manifest": {"OS": ent("a.dmg.aea"), "Cryptex1,AppOS": ent("b.dmg"),
                          "KernelCache": ent("kernelcache.release.x"), "NoInfo": {}}},
        ]})


class Manifest(unittest.TestCase):
    def test_roles(self):
        i = d.parse_manifest(manifest())
        self.assertEqual(i["build"], "26A434")
        self.assertEqual(i["device"], "j473ap")
        self.assertEqual(set(i["roles"]), {"OS", "Cryptex1,AppOS"})
        self.assertTrue(i["roles"]["OS"]["aea"])
        self.assertFalse(i["roles"]["Cryptex1,AppOS"]["aea"])
        self.assertEqual(d.image_name(i["roles"]["OS"]), "a.dmg")

    def test_unknown_product(self):
        with self.assertRaises(d.PipelineError):
            d.parse_manifest(manifest(), "Mac1,1")

    def test_resolve(self):
        i = d.parse_manifest(manifest())
        self.assertEqual(d.resolve(i, "a.dmg.aea")[0], "OS")
        with self.assertRaises(d.PipelineError):
            d.resolve(i, "nope")


class Dmg(unittest.TestCase):
    def test_classify(self):
        koly = b"koly" + b"\0" * 508
        self.assertEqual(d.classify_dmg(b"\0" * 4096, koly, 9999), "udif-trailer")
        self.assertEqual(d.classify_dmg(b"koly" + b"\0" * 100, b"\0" * 512, 9999), "udif-header")
        apfs = b"\0" * 32 + b"NXSB" + b"\0" * 100
        self.assertEqual(d.classify_dmg(apfs, b"\0" * 512, 9999), "apfs-raw")
        self.assertIsNone(d.classify_dmg(b"\0" * 4096, b"\0" * 512, 9999))

    def test_verify_file(self):
        with tempfile.TemporaryDirectory() as t:
            p = os.path.join(t, "x.dmg")
            with open(p, "wb") as f:
                f.write(b"\0" * 32 + b"NXSB" + b"\0" * 5000)
            r = d.verify_dmg(p)
            self.assertEqual(r["kind"], "apfs-raw")
            self.assertEqual(len(r["sha256"]), 64)
            with open(p, "wb") as f:
                f.write(b"\1" * 5000)
            with self.assertRaises(d.PipelineError):
                d.verify_dmg(p)


class MachO(unittest.TestCase):
    def thin(self, ct, cs, ft=2):
        return b"\xcf\xfa\xed\xfe" + struct.pack("<iII", ct, cs, ft) + b"\0" * 16

    def test_thin(self):
        self.assertEqual(d.macho_info(self.thin(0x0100000C, 0x80000002))["archs"], ["arm64e"])
        self.assertEqual(d.macho_info(self.thin(0x0100000C, 0))["archs"], ["arm64"])
        self.assertEqual(d.macho_info(self.thin(0x01000007, 3))["archs"], ["x86_64"])

    def test_fat_and_nonmacho(self):
        fat = b"\xca\xfe\xba\xbe" + struct.pack(">I", 2)
        fat += struct.pack(">IIIII", 0x01000007, 3, 4096, 1, 12) + struct.pack(">IIIII", 0x0100000C, 0x80000002, 8192, 1, 12)
        self.assertEqual(d.macho_info(fat)["archs"], ["x86_64", "arm64e"])
        self.assertIsNone(d.macho_info(b"\xca\xfe\xba\xbe\0\0\0\x34" + b"\0" * 64))  # Java class
        self.assertIsNone(d.macho_info(b"#!/bin/sh\n"))


class Decmpfs(unittest.TestCase):
    def test_header(self):
        self.assertEqual(d.parse_decmpfs_header(b"fpmc" + struct.pack("<IQ", 14, 1234)), (14, 1234))
        with self.assertRaises(d.PipelineError):
            d.parse_decmpfs_header(b"xxxx" + b"\0" * 12)

    def test_rsrc_blocks(self):
        rs = struct.pack("<3I", 12, 15, 17) + b"abc" + b"de"
        self.assertEqual(d.rsrc_blocks(rs), [b"abc", b"de"])
        with self.assertRaises(d.PipelineError):
            d.rsrc_blocks(struct.pack("<I", 3) + b"\0" * 8)

    def test_stored_block_and_bad_block(self):
        raw = bytes(range(256)) * 4
        rs = struct.pack("<2I", 8, 8 + 1 + len(raw)) + b"\xff" + raw
        self.assertEqual(b"".join(d.decode_lzbitmap_rsrc(rs, len(raw), None)), raw)
        bad = struct.pack("<2I", 8, 12) + b"zzzz"
        with self.assertRaises(d.PipelineError):
            d.decode_lzbitmap_rsrc(bad, 4, None)

    @unittest.skipUnless(shutil.which("gcc"), "gcc needed for vendored libzbitmap")
    def test_zbm_roundtrip(self):
        with tempfile.TemporaryDirectory() as t:
            lib = d.load_zbm(t)
            comp = ctypes.CDLL(os.path.join(t, "tmp", "libzbitmap.so"))
            comp.zbm_compress.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_char_p, ctypes.c_size_t,
                                          ctypes.POINTER(ctypes.c_size_t)]
            src = (b"daysetta " * 5000)[:40000]
            out = ctypes.create_string_buffer(len(src) * 2 + 64)
            n = ctypes.c_size_t(0)
            self.assertEqual(comp.zbm_compress(out, len(out), src, len(src), ctypes.byref(n)), 0)
            blob = out.raw[:n.value]
            self.assertEqual(blob[:4], b"ZBM\x09")
            rs = struct.pack("<2I", 8, 8 + len(blob)) + blob
            self.assertEqual(b"".join(d.decode_lzbitmap_rsrc(rs, len(src), lib)), src)


class Misc(unittest.TestCase):
    def test_redact(self):
        self.assertEqual(d.redact("key base64:" + "A" * 44), "key base64:<redacted>")
        self.assertEqual(d.redact("short"), "short")

    def test_index_and_summary(self):
        with tempfile.TemporaryDirectory() as t:
            app = os.path.join(t, "Foo.app", "Contents", "MacOS")
            os.makedirs(app)
            with open(os.path.join(t, "Foo.app", "Contents", "Info.plist"), "wb") as f:
                plistlib.dump({"CFBundleIdentifier": "x.foo", "CFBundleExecutable": "Foo"}, f)
            with open(os.path.join(app, "Foo"), "wb") as f:
                f.write(b"\xcf\xfa\xed\xfe" + struct.pack("<iII", 0x0100000C, 2, 2) + b"\0" * 16)
            with open(os.path.join(t, "t.txt"), "w") as f:
                f.write("hi")
            os.symlink("t.txt", os.path.join(t, "l"))
            files, apps = d.build_index(t)
            s = d.summarize(files, apps)
            self.assertEqual((s["files"], s["symlinks"], s["macho"], s["apps"]), (3, 1, 1, 1))
            self.assertEqual(apps[0]["bundle_id"], "x.foo")
            self.assertEqual(apps[0]["exec_archs"], ["arm64e"])
            self.assertEqual(s["macho_by_arch"], {"arm64e": 1})


if __name__ == "__main__":
    unittest.main()
