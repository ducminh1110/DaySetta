#!/bin/sh
# bsapp.sh <app bundle path inside ~/basesystem> [args]: Daylight + a BaseSystem GUI app (run from a shadow bundle,
# see shadowapp.py) drawing through daylight-bridge. Screen: ~/.darling/private/tmp/daylight/desktop.png,
# app output: ~/darltest/last.log.
B=/Volumes/SystemRoot/home/ubuntu/darltest/apps/daylight-bridge.dylib
app=$1; shift
exe=$(python3 ~/darling-port-tools/shadowapp.py "$app") || exit 1
# Recovery apps run as root in recoveryOS (and some insist, e.g. XPC lookups targeting uid 0): DL4_ASROOT=1/0
case "$app" in */CDIS/*|"Install macOS"*) root=${DL4_ASROOT:-1} ;; *) root=${DL4_ASROOT:-0} ;; esac
[ "$root" = 1 ] && exe="/Volumes/SystemRoot/home/ubuntu/darltest/asroot' '$exe"
darling shell true </dev/null >/dev/null 2>&1
T=${DL4_TIMEOUT:-60}
(timeout -s KILL $T darling shell env $DAYLIGHT_ENV /Volumes/SystemRoot/home/ubuntu/daylight/build-darling/daylight > ~/darltest/daylight.log 2>&1 </dev/null &)
sleep 3
timeout -s KILL $T darling shell sh -c "env DYLD_SHARED_CACHE_DIR=/Volumes/SystemRoot/home/ubuntu/basesystem/System/Library/dyld/ DYLD_SHARED_REGION=private DARLING_GUI_BRIDGE=$B DYLD_FALLBACK_LIBRARY_PATH=/usr/lib/swift:/usr/local/lib:/usr/lib DARLING_DYLINKER=/usr/lib/dyl4 DARLING_DYLINKER_PATHS=$HOME/basesystem DYLD_FALLBACK_FRAMEWORK_PATH=/Volumes/SystemRoot$HOME/darltest/stubs $DL4_ENV '$exe' $*; echo exit=\$?" > ~/darltest/last.log 2>&1 </dev/null
sleep 2
for p in $(pgrep -f "build-darling/daylight\$"); do sudo -n kill -INT $p; done
sleep 1
