---
name: darling-xnu-port
description: "State of the user's Darling port to xnu-12377.121.6 (macOS 26.x); where it builds/runs, patch locations, fixes made and open issues (2026-10-29)"
metadata:
  node_type: memory
  type: project
  originSessionId: fa91baf1-5dd6-4aeb-8211-2202c0fc70d0
  modified: 2026-10-29T12:24:34.692Z
---

Darling (darlinghq/darling @ 3447ff5) ported from xnu-7195.141.2 (macOS 11) to xnu-12377.121.6 (macOS 26.5, newest Apple has published). Version labels set to Darwin 26.2.0 / macOS 27.2 at the user's request. First full build + run succeeded 2026-10-29 on the user's x86_64 box: `darling shell` runs sw_vers, bash, ls, ps, launchctl list, perl.

**Why:** foundation for the hybrid plan in [[daylight-windowserver]] (Apple frameworks on Darling's kernel layer).

**How to apply:**
- Build/test host: x86_64 Ubuntu 24.04 `ubuntu@remotemac.mooo.com` (user provided password in chat; suggest key auth). Tree at `~/darling`, build `~/darling/build` (configure with empty CMAKE_BUILD_TYPE — Release breaks objc4 even upstream), installed to /usr/local. The user's macOS VM there was deleted on request.
- All changes are uncommitted; exported as patches in `~/darling-port-patches/` on the aarch64 host (darling.patch for the superproject + one per submodule: xnu, bootstrap_cmds, libc, libmalloc, network_cmds, syslog, IOKitUser, iokitd) plus the extended AvailabilityVersions DSL (macOS 15/26 entries; dyld set versions are guessed).
- Key fixes: tools/sync-xnu-sdk-headers.py (SDK symlinks), regenerated 16 static MIG headers in the SDK, Availability headers from AvailabilityVersions-157.2 (+ iosmac/uikitformac compat), -DXNU_PLATFORM_MacOSX, migcom/mldr host shim darling_host_cdefs.h (use "SHELL:-include"), mach_msg_overwrite kept on mach_msg_overwrite_trap under DARLING, _vfork alias, voucher aux stubs in mach_msg.c, task_read/inspect_for_pid syscalls (538/539).
- gen/bsdsyscalls/SYS.h and ___vfork.S are copies of libsyscall/custom files — edit both.
- Fixed upstream bug (not port-related): Foundation's -[NSUserDefaults setPersistentDomain:forName:] used registerDefaults: (volatile), so `defaults write` never reached disk; also added macOS type flags (-int/-bool/-array-add/-dict/...) to tools/defaults.m. Patch: src_external_foundation.patch. Good upstream PR candidate.
- mach_msg2_trap (trap 47) implemented 2026-10-29 in emulation mach_traps.c (translates scalar/vector forms onto mach_msg_overwrite RPC; header taken from registers; aux reported empty); libsyscall uses the upstream mach_msg2 path on x86_64, old trap loop kept for i386; xtrace names trap 47. Test: ~/darling-port-patches/tests/mach_msg2_test.c (13/13 pass; build with source-tree SDK + cctools ld64 `-syslibroot /usr/local/libexec/darling`).
- Open: darlingserver lacks mach_ports_register3/lookup3; vm_reclaim.c dropped; 31 new syscalls ENOSYS; other submodules still 11.x.
- Never use `pkill -f` with patterns that appear in the ssh command line itself — it kills the session.
