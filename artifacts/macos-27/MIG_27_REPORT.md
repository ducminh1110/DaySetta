# macOS 27.0.1 (26A434) SkyLight MIG scan (WP1b)

No macOS 27 compatibility is claimed. This is a static address inventory only.

## Method (OBSERVED, `tools/macos27/scan_mig.py`)
1. Open the split cache `/home/ubuntu/macos-re/cache-arm64e/dyld_shared_cache_arm64e`; image at
   vmaddr 0x187093000; UUID checked == 8A3B348E4637368592D06CBC2F36A234 (script aborts otherwise).
2. Scan every 8-byte-aligned offset of SkyLight `__DATA_CONST`, `__AUTH_CONST`, `__DATA` sections.
3. Header layout (from `/home/ubuntu/daylight/tools/mig_scan.py` lines 47-58): server ptr @0,
   start i32 @8, end i32 @12, maxsize u32 @16, reserved ptr @24; then end-start descriptors
   of 40 bytes (impl, stub, argc, descr_count, arg_descr, max_reply_msg).
4. Pointers decoded by `tools/macos27/fixups.py` `Fixups.resolve` (slide info v5, WP1a);
   no PAC mask guessing. Accept only if server in SkyLight `__TEXT`, 0 < end-start <= 4096,
   every stub 0 or decoded into `__TEXT`, every impl 0 or in `__TEXT`, arg_descr 0, argc <= 64,
   at least one live stub. Pre-filter (raw): reserved == 0, maxsize != 0, 0 < start.
5. Names only by exact (subsystem start id, routine id) in
   `/home/ubuntu/daylight/protocol/macos-26.6-x86_64.json`; status `inferred-from-26.6`.

## Evidence
- `artifacts/macos-27/mig-routines.json` (addresses only; includes `source.rejected`, `__TEXT` bounds).
- `tools/macos27/verify_wp1.sh` -> VERIFY PASS (also re-checks bounds via `scan_mig.py --text-bounds`).
- Rejections: 56 candidates passed the pre-filter; 47 rejected `server_ptr_not_in_text`; 9 accepted.

## Result: 9 subsystems, 744 routines (26.6 baseline: 9, 738)
| Start..end (27) | 26.6 name (inferred) | 27 live | 26.6 | added ids | removed ids |
|---|---|---:|---:|---|---|
| 1..3 (0x1e8743248) | ? (ambiguous) | 2 | 2 | none | none |
| 1..3 (0x1e8743e10) | ? (ambiguous) | 2 | 2 | none | none |
| 1..3 (0x1e8743e80) | ? (ambiguous) | 2 | 2 | none | none |
| 29000..29042 | CGXRendezvous | 42 | 41 | 29041 | none |
| 30000..30647 | CGXServices | 580 | 575 | 13 ids (see JSON) | 8 ids (see JSON) |
| 32000..32074 | CGXWindowServer | 72 | 72 | none | none |
| 33000..33029 | ServicesRelocated | 29 | 29 | none | none |
| 34000..34010 | ServerRelocated | 10 | 10 | none | none |
| 35000..35005 | CGXHMD | 5 | 5 | none | none |

CGXServices added: 30523 30535 30592 30600 30603 30607 30637-30640 30644-30646.
CGXServices removed (stub 0 in 27 or outside range): 30204 30205 30525 30537 30593 30601 30605 30608.

## Totals
- Routines named (inferred-from-26.6): 724. Named "?": 20 (6 in the three start-1 subsystems,
  1 CGXRendezvous, 13 CGXServices).
- Subsystems named: 6 of 9; 3 are "?".

## UNKNOWNs
- The three start=1 subsystems (26.6: CGYSpinControl, CGYCaptureReply, CGYEventTap, all start 1,
  2 routines) cannot be told apart by start id; per the brief they stay "?". Which descriptor is
  which: UNKNOWN (needs server-function or symbol evidence, not collected).
- Names/semantics of the 20 "?" routines and of every "removed" id: UNKNOWN. A removed id means
  only "no live stub at that slot in 27"; whether it was deleted or renumbered is UNKNOWN.
- An id match with 26.6 does not prove the same signature or behavior (argc and
  max_reply_msg were not compared against 26.6; the baseline JSON has no such fields).
- 26.6 baseline is x86_64, 27 is arm64e: differences may be arch-related. UNKNOWN.
- Impl pointers exist but were not validated beyond lying in `__TEXT`.
- The 4088 vs 4099 image-count difference: UNKNOWN (untouched).
