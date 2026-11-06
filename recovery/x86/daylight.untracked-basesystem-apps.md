# BaseSystem 26.6 GUI apps on Darling + Daylight

How each AppKit app of the macOS 26.6 recovery BaseSystem does, run with
`darling-port-tools/bsapp.sh <path in ~/basesystem>` (sweep: `bsbatch.sh OUT APP...`, list in `~/darltest/apps.txt`).
Apps under `System/Installation/CDIS` and `Install macOS Tahoe.app` run as root (`asroot`), as in recoveryOS.

For the Mac-only recovery apps (Disk Utility, Startup Disk, Startup Security Utility, KeyRecoveryAssistant, ...)
"runs" means: starts without hanging, draws its window correctly, takes mouse and keyboard, does not crash, and shows
its empty state properly ("Loading disks", "no volumes", "firmware not supported"). Making them see real disks
(StorageKit, DiskArbitration, storagekitd, IOMedia, APFS) is out of scope.

| App | State | Blocker / note |
|---|---|---|
| Terminal | works: bash, keyboard, activation | — |
| Disk Utility | **done** (recovery-app bar): starts, window with toolbar, "Loading disks" empty state | disk listing (StorageKit, DiskArbitration, IOMedia, APFS) deliberately out of scope: no use on Linux. Needed authd (launchd routine 718, OFD locks) |
| Startup Disk | window, click → alert | no boot volumes |
| Startup Security Utility | "Your firmware is not supported" dialog | expected on a non-Apple-silicon, non-T2 machine |
| Installation Log | window with toolbar | no `/var/log/install.log` |
| Lock | full-screen PIN lock screen | — |
| Device Recovery Assistant | "Recovery" page with Continue | — |
| KeyRecoveryAssistant | "Your Mac has no volumes to recover" | — |
| Recovery Diagnose | window, table | `com.apple.recoverydiagnosed` not running |
| macEACS | blank window | content not drawn yet |
| Recovery Springboard | crash: XPC API misuse | needed root (fixed with `asroot`), then `com.apple.RecoveryOS-Agent` |
| macOS Update Assistant | crash in OSIPowerController | IOPS power-source notifications need notifyd (now running) |
| Language Chooser | crash: objc_msgSend → NULL on a worker thread | `+[LCAUtilities isEnrolledInDEP]` path, under investigation |
| Share Disk | AuthorizationCreate assertion | needs authd (`mkdaemons.py --only=authd`) |
| Boot Picker | exits with 45 | unknown yet |
| Boot Recovery Assistant | exits 0 | `com.apple.bosreporter` missing |
| Install macOS Tahoe | crash | launches InstallAssistant, which needs dyl4 (`DARLING_DYLINKER`) |
| Web Browser | SIGTRAP at start | unknown yet |
| Safari | cannot run from BaseSystem.dmg alone | `Safari.framework`'s binary lives in the App cryptex, not in the dmg or the shared cache |
| Time Machine System Restore, TextInputSwitcher, Installer Progress, DiskImageMounter, Dwell Control, FindMyMacMessenger | no window | agents or apps that open windows on demand |

Shared fixes made while sweeping (Darling): HFS+ hard links of the extracted BaseSystem, getattrlist/getattrlistbulk,
pathconf, Sandbox checks, workqueue locking and stack size, libdispatch cooperative queues and workloops,
notifyd (sandbox, xpc event publisher), mldr `DARLING_DYLINKER`.
