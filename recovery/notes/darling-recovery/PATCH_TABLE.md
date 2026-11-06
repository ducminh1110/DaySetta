# D1 patch application table (2026-11-06) - nothing pushed
Submodule init: `git submodule update --init --recursive --depth 1 --jobs 8` -> exit 0, 149/149 ok, 0 failed (log: submodule-init.log; `git submodule status` all ' ').
Pre-D1 state saved: pre-D1-state.txt (212 entries), pre-D1-state.patch (560 KB). Branch `daysetta/recovered` (local) in superproject (commit 600ae5da) and in the submodules below.
| Patch (/home/ubuntu/darling-port-patches/) | Target | Result |
|---|---|---|
| src_external_xnu.patch (1117 files) | src/external/xnu | NOT APPLICABLE: tree already at port/xnu-12377.121.6 @3780f21 whose diff vs ac9718f touches the same 1117 files (+42435/-829); forward and reverse `git apply --check` fail because the tree is newer. Branch daysetta/recovered created at 3780f21 (no new commit). |
| darling.patch (235 files) | superproject | PARTIAL / already in tree: 187 SDK symlinks, 33 SDK deletions (patch's own deletions; files already absent), basic-headers Availability*, SystemVersion*.plist, tools/sync-xnu-sdk-headers.py were already present in the working tree (reverse check OK). Newly applied (3way, clean): src/CMakeLists.txt, src/hosttools/CMakeLists.txt, src/launchd/src/core.c, src/startup/mldr/{CMakeLists.txt,include/darling_host_cdefs.h}, src/xtrace/mach_trace.cpp. No conflicts. |
| AvailabilityVersions-157.2-darling.dsl | AvailabilityVersions | NOT APPLICABLE as patch: it is a DSL source (493 lines), no counterpart in submodule; the generated AvailabilityVersions.h was already modified in working tree and was committed on daysetta/recovered (ea5d00a). DSL stays in darling-port-patches as reference. |
| src_external_bootstrap_cmds.patch | bootstrap_cmds | applied (3way, clean), commit 2e9cac4 |
| src_external_foundation.patch | foundation | applied clean, 844a5ff |
| src_external_iokitd.patch | iokitd | applied clean, e0bf47f |
| src_external_IOKitUser.patch | IOKitUser | applied clean, bd01ec9 |
| src_external_libc.patch | libc | applied clean, fb7b418 |
| src_external_libmalloc.patch | libmalloc | applied clean, c4b8363 |
| src_external_network_cmds.patch | network_cmds | applied clean, 372857b |
| src_external_syslog.patch | syslog | applied clean, 6be9628 |
| tests/mach_msg2_test.c, work/{getattrlistbulk.c,getattrlist_generic.c,attrlist_pack.h} | - | not patches; kept in place, referenced from LOST_FIXES.md #8/#13 |
Totals: 8 applied, 1 partial (darling.patch), 2 not applicable (xnu, dsl). Superproject commit also records the pre-existing working-tree state (stack.c, plists, headers) and the new submodule pointers. Local branches: superproject + AvailabilityVersions, xnu, bootstrap_cmds, foundation, iokitd, IOKitUser, libc, libmalloc, network_cmds, syslog.
