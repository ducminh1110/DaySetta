#!/bin/sh
# fsapp.sh <app bundle path inside ~/fullsystem> [args]
# Same as bsapp.sh but runs an app from the FULL installer system (~/fullsystem)
# with the full system's frameworks + dyld shared cache, drawing through the
# daylight bridge. Screen: ~/.darling/private/tmp/daylight/desktop.png,
# app output: ~/darltest/last.log.
B=/Volumes/SystemRoot/home/ubuntu/darltest/apps/daylight-bridge.dylib
ROOT=$HOME/fullsystem
# Use the full system's own dyld shared cache if it has one; the full installer
# system and BaseSystem are the same 26.6 build, so BaseSystem's cache is a
# valid fallback (and gets used when the system payload ships no cache).
CACHE=${DL4_CACHE:-$ROOT/System/Library/dyld/}
# prefer the full system's own cache (x86_64h on Haswell+); fall back to BaseSystem
{ [ -f "$CACHE/dyld_shared_cache_x86_64" ] || [ -f "$CACHE/dyld_shared_cache_x86_64h" ]; } \
	|| CACHE=$HOME/basesystem/System/Library/dyld/
app=$1; shift
exe=$(SHADOW_ROOT=$ROOT SHADOW_OUT=$HOME/darltest/bundles-fs python3 ~/darling-port-tools/shadowapp.py "$app") || exit 1
# full-system GUI apps run as the user; recovery apps need root (DL4_ASROOT=1)
case "$app" in */CDIS/*|"Install macOS"*) root=${DL4_ASROOT:-1} ;; *) root=${DL4_ASROOT:-0} ;; esac
[ "$root" = 1 ] && exe="/Volumes/SystemRoot/home/ubuntu/darltest/asroot' '$exe"
darling shell true </dev/null >/dev/null 2>&1
T=${DL4_TIMEOUT:-60}
# Real desktop components (Dock/Finder/...) draw their own menu bar and Dock, so
# the built-in shell is off by default; set DL4_SHELL=1 to keep it.
(timeout -s KILL $T darling shell env DAYLIGHT_SHELL=${DL4_SHELL:-0} $DAYLIGHT_ENV /Volumes/SystemRoot/home/ubuntu/daylight/build-darling/daylight > ~/darltest/daylight.log 2>&1 </dev/null &)
sleep 3
timeout -s KILL $T darling shell sh -c "env DYLD_SHARED_CACHE_DIR=/Volumes/SystemRoot$CACHE DYLD_SHARED_REGION=private DARLING_GUI_BRIDGE=$B DYLD_FALLBACK_LIBRARY_PATH=/usr/lib/swift:/usr/local/lib:/usr/lib DARLING_DYLINKER=/usr/lib/dyl4 DARLING_DYLINKER_PATHS=$ROOT DYLD_FALLBACK_FRAMEWORK_PATH=/Volumes/SystemRoot$HOME/darltest/stubs $DL4_ENV '$exe' $*; echo exit=\$?" > ~/darltest/last.log 2>&1 </dev/null
sleep 2
for p in $(pgrep -f "build-darling/daylight\$"); do sudo -n kill -INT $p; done
sleep 1
