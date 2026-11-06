#!/bin/sh
# fsdesktop.sh [APP ...]
# Run the real macOS desktop components from ~/fullsystem together in ONE Daylight
# session (Dock, Finder, SystemUIServer, ControlCenter, NotificationCenter by
# default). Each app is launched through a shadow bundle (shadowapp.py) with the
# shared cache + daylight bridge, exactly like fsapp.sh, but they all stay up at
# once and share the same window server. The built-in shell is disabled so the
# real Dock owns the screen.
#
# Per-app output: ~/darltest/desk-<app>.log ; Daylight: ~/darltest/daylight.log
# Screen PNG: ~/.darling/private/tmp/daylight/desktop.png
B=/Volumes/SystemRoot/home/ubuntu/darltest/apps/daylight-bridge.dylib
ROOT=$HOME/fullsystem
CACHE=$ROOT/System/Library/dyld/
{ [ -f "$CACHE/dyld_shared_cache_x86_64" ] || [ -f "$CACHE/dyld_shared_cache_x86_64h" ]; } \
	|| CACHE=$HOME/basesystem/System/Library/dyld/
APPS="$*"
[ -z "$APPS" ] && APPS="System/Library/CoreServices/Dock.app System/Library/CoreServices/Finder.app System/Library/CoreServices/SystemUIServer.app System/Library/CoreServices/ControlCenter.app System/Library/CoreServices/NotificationCenter.app"
T=${DL4_TIMEOUT:-600}

darling shell true </dev/null >/dev/null 2>&1
(timeout -s KILL $T darling shell env DAYLIGHT_SHELL=0 $DAYLIGHT_ENV \
	/Volumes/SystemRoot/home/ubuntu/daylight/build-darling/daylight > ~/darltest/daylight.log 2>&1 </dev/null &)
sleep 3

for app in $APPS; do
	exe=$(SHADOW_ROOT=$ROOT SHADOW_OUT=$HOME/darltest/bundles-fs \
		python3 ~/darling-port-tools/shadowapp.py "$app") || { echo "shadow fail: $app"; continue; }
	name=$(basename "$app" .app | tr ' ' '_')
	echo "launch $name -> $exe"
	(timeout -s KILL $T darling shell sh -c \
		"env DYLD_SHARED_CACHE_DIR=/Volumes/SystemRoot$CACHE DYLD_SHARED_REGION=private \
		DARLING_GUI_BRIDGE=$B DYLD_FALLBACK_LIBRARY_PATH=/usr/lib/swift:/usr/local/lib:/usr/lib \
		DARLING_DYLINKER=/usr/lib/dyl4 DARLING_DYLINKER_PATHS=$ROOT \
		DYLD_FALLBACK_FRAMEWORK_PATH=/Volumes/SystemRoot$HOME/darltest/stubs $DL4_ENV \
		'$exe'; echo exit=\$?" > ~/darltest/desk-$name.log 2>&1 </dev/null &)
	sleep 3
done

echo "desktop launched; apps: $APPS"
