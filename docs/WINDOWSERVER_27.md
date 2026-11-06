# WindowServer for macOS 27 (G1)

Evidence classes: OBSERVED (ran here), DERIVED (computed from the 27 inventory), UNVERIFIED.

## What it is
`src/windowserver/` = Daylight's SkyLight MIG server + X11 frontend (copy, see `PROVENANCE.md`) built as an
arm64 Mach-O with Darling's toolchain (`build-darling.sh`, ARCH=arm64; ARCH=x86_64 COMPILE_ONLY=1 compiles). Per
ARCHITECTURE_DECISION_REPORT section 5 it sits in front of the DaySetta scene/compositor; today it still composites
with Daylight's own canvas compositor (`bridge.c`), not `src/scene`. UNVERIFIED: the hand-off to `src/scene`.

## MIG tables 26.6 -> 27 (DERIVED, `tools/gen27.py`)
Source: `protocol/mig-routines.json` (27) vs `protocol/macos-26.6-x86_64.json` (names). 738 named routines in 6
subsystems (3 more tables with ids 1..2 are other servers and are ignored). Names are by id only
(`inferred-from-26.6`).
- Same / not contradicted: 677; 14 ADDED ids (named `Added27_<id>`): 29041, 30523, 30535, 30592, 30600, 30603, 30607, 30637, 30638, 30639, 30640, 30644, 30645, 30646.
- 8 REMOVED ids (dropped): 30204, 30205, 30525, 30537, 30593, 30601, 30605, 30608.
- Reply size: 27 server `max_reply_msg + 8` equals the 26.6 client receive buffer for 399 of 453 recorded ids
  (OBSERVED relation), so those sizes are kept. 47 ids differ (CHANGED, below): no size is emitted (no
  default-success reply) and the 26.6 name at that id is unverified (ids shift near 30500). Client-side sizes in 27
  were not re-derived: UNVERIFIED.
- All 15 routines Daylight really implements keep their ids and sizes in 27.

| id | name at id in 26.6 | 26.6 client size | 27 server max reply |
|---|---|---|---|
| 30521 | DisplayCopyUserAdjustment | 64 | 36+8 |
| 30522 | DisplaySetUserAdjustment | 44 | 56+8 |
| 30527 | DisplayInvalidateUserAdjustment | 44 | 40+8 |
| 30529 | DisplayCopyCalibrationInfo | 64 | 36+8 |
| 30531 | GetUnderscanValue | 48 | 36+8 |
| 30534 | DisplayGetPreferHDR10 | 48 | 36+8 |
| 30540 | SelectiveSharingContextSetState | 44 | 52+8 |
| 30541 | SelectiveSharingContextCopyState | 64 | 36+8 |
| 30551 | CopyWindowCornerMask | 108 | 40+8 |
| 30553 | GetUnifiedKeyMapping | 48 | 64+8 |
| 30554 | SetUnifiedKeyMapping | 44 | 40+8 |
| 30555 | SelectiveSharingContextCopy | 64 | 36+8 |
| 30558 | SelectiveSharingManagerCopyAllSessions | 64 | 36+8 |
| 30559 | PersistenceSetSessionStatus | 44 | 56+8 |
| 30561 | GetLastUsedKeyboardID | 48 | 36+8 |
| 30564 | GetSpaceNeedsAuxiliaryScale | 48 | 36+8 |
| 30565 | HMDSetEnabled | 44 | 40+8 |
| 30566 | HMDEnabled | 48 | 36+8 |
| 30571 | GetWindowTransform3D | 108 | 36+8 |
| 30573 | GetSessionUID | 48 | 36+8 |
| 30575 | VirtualDisplayApplySettings | 44 | 40+8 |
| 30578 | GetWindowBackingStoreOverrideResolution | 48 | 36+8 |
| 30579 | SelectiveSharingContextSetPresentationDisplay | 44 | 40+8 |
| 30583 | DisplayCalibrationToolboxCommand | 64 | 36+8 |
| 30584 | DisplaySupportsDynamicGeometry | 48 | 56+8 |
| 30589 | GetZoomParametersForDisplay | 60 | 36+8 |
| 30590 | SetZoomParametersForDisplay | 44 | 52+8 |
| 30591 | PostCoordinatedDistributedNotificationFenced | 52 | 36+8 |
| 30594 | AllowPrivacyIndicatorSuppression | 48 | 36+8 |
| 30597 | TakeMouseFocusTracking | 44 | 52+8 |
| 30598 | DisplayIsPerPresetUserAdjustmentSupported | 48 | 36+8 |
| 30602 | DisplaySetUserAdjustmentForPreset | 44 | 56+8 |
| 30606 | DisplayInvalidateUserAdjustmentForPreset | 44 | 40+8 |
| 30619 | GetTVConnectDefaultPolicy | 48 | 36+8 |
| 30621 | ValidateDisplayWallConfiguration | 48 | 41000+8 |
| 30622 | EnableDisplayWallWithConfiguration | 44 | 40+8 |
| 30624 | IsDisplayWall | 48 | 36+8 |
| 30625 | DisplayGetUserAdjustmentRange | 148 | 40+8 |
| 30626 | SymbolicHotKeyName | 1076 | 140+8 |
| 30627 | UpdatePermittedFrontPSNList | 44 | 1068+8 |
| 30630 | DisplayGetCustomPresetInfo | 64 | 36+8 |
| 30631 | DisplaySetCustomPresetData | 44 | 56+8 |
| 30632 | DisplayGetLuminanceCorrectionFactor | 52 | 36+8 |
| 30633 | DisplayIsLiveUserAdjustmentSupported | 48 | 44+8 |
| 30634 | DisplayInvalidateLiveUserAdjustmentForPreset | 44 | 40+8 |
| 30636 | MenuBarPrivacyIndicatorInset | 52 | 36+8 |
| 33020 | SetDisplayContrast | 180 | 36+8 |

## Run (OBSERVED, 2026-11-06)
`src/windowserver/run-probe-demo.sh`: Xvfb + `darling shell windowserver` (bootstrap-registered
`com.apple.windowserver`, `.active`) + host `daylight-x11` + arm64 probe `dsws_probe`. Probe calls 29000, 29010, 32000,
30263, 30311 and the canvas messages; all PASS (`artifacts/ws27-logs/probe.log`, `server.log`). Screenshot:
`artifacts/screenshots/windowserver27-probe-client.png` - **probe client, not Apple UI**.

## Blocker: no cross-process memory (ESC-G1-1)
`fileport_makeport` aborts ("old LKM API"), `mach_make_memory_entry_64` loops on `dserver_rpc_interrupt_enter ... -111`;
no `darling_memory_entry_create`. The server therefore backs SkyLight shmem regions/canvases with host files and a
placeholder port; only the probe can use that. A genuine AppKit client needs real memory entries. `tools/run-genuine-gui.sh APP`
is prepared, UNVERIFIED (never run: needs darlingserver restored and libxpc).

## Not done
- Genuine 27 AppKit client; 27-specific display-state blob (`DAYLIGHT_EMPTY_DISPLAY_STATE` workaround retained).
- Added/changed routines have no handlers or sizes. `probe/handshake_client.c` last check is stale.
