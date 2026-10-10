#!/bin/bash
# run-genuine-gui.sh APP [args]: launch a genuine macOS 27 AppKit app (e.g. hello2 built from
# daylight/tools/macprobe/hello2.m with -mmacosx-version-min=14.0) against the DaySetta windowserver under
# Darling aarch64, show it on Xvfb and take a screenshot.
# NOT YET VERIFIED: needs darlingserver memory entries/fileports (ESC-G1-1) and a working libxpc. Until then
# use src/windowserver/run-probe-demo.sh (probe client, not Apple UI).
# Env: DSWS_DISPLAY (:97), DSWS_CACHE (27 arm64e cache), T (app timeout, 60), DSWS_APP_ENV (extra env for the app)
set -u
APP=${1:?usage: run-genuine-gui.sh APP [args]}; shift
DS=$(cd "$(dirname "$0")/.." && pwd); WS=$DS/src/windowserver
B=/Volumes/SystemRoot$WS/build-darling-arm64
L=$DS/artifacts/ws27-logs; SHOT=$DS/artifacts/screenshots; mkdir -p "$L" "$SHOT" /tmp/daylight
CACHE=${DSWS_CACHE:-/home/ubuntu/macos-re/cache-arm64e/dyld_shared_cache_arm64e}
DISP=${DSWS_DISPLAY:-:97}
[ -x "$WS/build-darling-arm64/windowserver" ] || ARCH=arm64 "$WS/build-darling.sh" || exit 1
[ -e "$APP" ] || { echo "no such app: $APP" >&2; exit 1; }
Xvfb "$DISP" -screen 0 1920x1080x24 >"$L/xvfb.log" 2>&1 & XV=$!
sleep 1
darling shell env DAYLIGHT_EMPTY_DISPLAY_STATE=1 "$B/windowserver" </dev/null >"$L/genuine-server.log" 2>&1 & SRV=$!
for i in $(seq 30); do grep -q "serving com.apple.windowserver.active" "$L/genuine-server.log" && break; sleep 1; done
DISPLAY=$DISP "$WS/build-darling-arm64/daylight-x11" /tmp/daylight >"$L/genuine-x11.log" 2>&1 & FE=$!
timeout -s KILL "${T:-60}" darling shell env DARLING_DYLINKER=/usr/lib/dyl4 DARLING_DS_CACHE="$CACHE" \
  ${DSWS_APP_ENV:-} "/Volumes/SystemRoot$(readlink -f "$APP")" "$@" </dev/null >"$L/genuine-app.log" 2>&1 & APPPID=$!
sleep "${DSWS_SHOT_DELAY:-20}"
DISPLAY=$DISP import -window root "$SHOT/windowserver27-genuine-appkit.png"
echo "app log: $L/genuine-app.log; server log: $L/genuine-server.log; screenshot: $SHOT/windowserver27-genuine-appkit.png"
kill $APPPID $FE $XV 2>/dev/null
sudo kill $SRV 2>/dev/null   # PIDs only; never pkill -f
