# IPSW 27 component pipeline report (T1)

Evidence classes: V = verified here by command/exit code, U = unknown/not checked.
Tool: `tools/ipsw/ds_ipsw.py` (inspect, keys, decrypt, extract, index); tests: `tools/ipsw/test_ds_ipsw.py`.
Store (outside repo, nothing from it is committed): `/home/ubuntu/macos-re/store-27/`.

## Input
- IPSW `/home/ubuntu/macos-re/ipsw/UniversalMac_27.0.1_26A434_Restore.ipsw` (read only). Build 26A434, 27.0.1 (V: `ds_ipsw.py inspect`).
- Identity: product type Mac14,3 = DeviceClass j473ap, variant "Customer Erase Install (IPSW)" (V: BuildManifest.plist). ipsw itself maps Mac14,3 to SystemOS 043-70701-651 (V: `ipsw extract --device` message).

## Images (roles -> member, from BuildManifest for Mac14,3) (V)
| role | member | bytes | AEA |
|---|---|---|---|
| OS (system volume) | 043-70867-640.dmg.aea | 11,274,289,152 | yes |
| Cryptex1,SystemOS (dyld cache) | 043-70701-651.dmg.aea | 2,415,919,104 | yes |
| BaseSystem | 022-20292-678.dmg.aea | 1,342,177,280 | yes |
| Cryptex1,AppOS | 043-70637-690.dmg | 54,525,952 | no |
| Cryptex1,RosettaOS | 094-86031-274.dmg | 7,717,519,360 | no |
| RestoreRamDisk | 043-70833-687.dmg | 373,293,083 | no |

## Keys (store-27/keys/*.key mode 0600, log store-27/logs/keys.log; key bytes never printed) (V)
- OS, SystemOS, BaseSystem: key obtained, source label `ipsw-default` (ipsw's own chain: embedded PEM, pem-db, key service). No local PEM and no pem-db file existed, so the first two local sources were skipped.
- Observed behavior (V, strace on `ipsw fw aea -k` for SystemOS): no network `connect` call (only the local dbus socket) and no PEM file opened. So the key came from ipsw internals, not from a live wkms request. Not re-traced for OS/BaseSystem (U).
- No HTTP status exists to log: ipsw reports only exit code (rc=0 for all three).
- Not attempted: ExclaveOS (not needed), unencrypted images need no key.

## Decrypt + DMG verification (V: `store-27/dmg/*.verify.json`)
- SystemOS: decrypted, 2,879,389,696 B, sha256 5fea3f16f856f3aa1b3f88aa3cedfe75977a36f70a17041d6cf6349ba9a0e4d2.
- OS: decrypted, 14,189,330,432 B, sha256 fbfb1a38af09d65dc86bb9814a13b870d1895e4c435c38fde42e00b52cff21b3.
- Finding: the decrypted payloads are NOT UDIF DMGs (no `koly` trailer or header). They are raw APFS containers: `NXSB` at offset 32, and `file` says "Apple File System (APFS)". The check therefore accepts koly or raw NXSB and records which (`kind: apfs-raw`). 7z lists both as Type = APFS (V).
- BaseSystem: key stored, NOT decrypted. AppOS, RosettaOS: NOT processed.

## APFS reader
- 7z 26.00 reads the APFS structure (V). No custom APFS parser was written, so there is no `apfs.py`.
- 7z limitation (V): LZBITMAP-compressed files (decmpfs type 14) fail with "Unsupported Method" and come out as empty file plus `:com.apple.decmpfs` and `:com.apple.ResourceFork` streams. LZVN files (system volume, 0 failures) are decoded by 7z.
- Fix in ds_ipsw.py: post-pass decodes type 14 (and 13) with vendored `tools/ipsw/vendor/libzbitmap.c` (MIT, source github.com/blacktop/lzfse-cgo; compiled with gcc, loaded via ctypes). Blocks starting 0xFF are stored raw. Result: SystemOS 3482 decoded, 0 failed. Spot check: decoded Assets.car starts `BOMStore`, AppKit_x86Support starts a Mach-O magic (V). Full-file hashes vs Apple are U.
- Types other than 13/14 raise a loud failure (none occurred).
- 7z refuses "dangerous" symlinks; used `-snl -snld`, and recreate the 2 link-to-link entries itself (links_recreated=1 per image).

## Extraction (store-27/root, extract log store-27/logs/extract.log) (V)
- SystemOS to `root/System/Cryptexes/OS/`: everything except `System/Library/dyld` (6.7 GB, not extracted). The 82 dyld files in the image match `/home/ubuntu/macos-re/cache-arm64e` by name and size (V); content hash not compared (U).
- OS image to `root/`: only /bin, /usr/bin, /usr/lib, Terminal.app, Finder.app, Dock.app, /System/Library/Fonts. The rest is listed, not extracted: `store-27/logs/043-70867-640.dmg.listing.txt` (514k lines of 7z -slt output).
- Files extracted (V: manifest.json): system volume 4803 regular + 73 symlinks; cryptex 6511 regular + 8 symlinks.

## Index (store-27/manifest.json, 1.8 MB) (V)
- files 12,187, symlinks 81, Mach-O 873 (sha256 only for Mach-O), apps 9.
- Arch of `/bin/echo`: fat x86_64 + arm64e + arm64 (sha256 9f13adc0...e92c).
- Terminal.app (com.apple.Terminal) executable `Terminal`: fat arm64e + arm64. Finder and Dock: arm64e + arm64.
- 19 fat files list cputype 0x1000017/0x1000013 (labelled `cpu0x...`): GPU metallib archives, not CPU code (U: inferred from path).

## Store paths that exist (V)
`store-27/{keys,logs,dmg,root,manifest.json}`; `dmg/043-70701-651.dmg`, `dmg/043-70867-640.dmg` (+ .verify.json); `root/bin/echo`; `root/System/Applications/Utilities/Terminal.app`; `root/System/Cryptexes/OS/usr/lib/system/libsystem_kernel.dylib`.

## Disk
After all steps `df /home/ubuntu`: 29 GB free (peak during OS decrypt was about 31 GB free minus 25 GB transient).

## Not covered / risks
- Compression sanity checked on samples only. Sealed-volume hashes, trustcache and root_hash not verified. No Mach-O was executed.
- Key source "no network" rests on one strace of one image.
