---
name: apple-oss-distribution-versions
description: apple-oss-distributions/distribution-macOS release.json pins the exact open-source component tags per macOS release (use instead of guessing tags)
metadata:
  type: reference
---

https://github.com/apple-oss-distributions/distribution-macOS — tags `macos-260` … `macos-265`; each has `release.json` mapping project → tag. User pointed to it on 2026-10-29.

macOS 26.5 (`macos-265`): dyld-1378, xnu-12377.121.6, libpthread-539.100.4, libplatform-375.120.2, Libc-1752.120.2, libdispatch-1542.100.32, libmalloc-812.100.31, objc4-951.7, Libsystem-1356, libclosure-96, AvailabilityVersions-157.2, IOKitUser-100231.120.3, configd-1405.120.5, Security-61901.120.67.
BaseSystem we run is 26.6 (ships dyld-1387, not published), so 26.5 sources are the closest match. Used by [[darling-xnu-port]] and the dyld port.
