#!/bin/bash
# Server under Darling aarch64 + Xvfb + host X11 frontend + probe client; screenshot to artifacts/screenshots.
# Probe client, not Apple UI.
set -u
HERE=$(cd "$(dirname "$0")" && pwd); DS=$(cd "$HERE/../.." && pwd)
B=/Volumes/SystemRoot$HERE/build-darling-arm64
L=$DS/artifacts/ws27-logs; SHOT=$DS/artifacts/screenshots; mkdir -p "$L" "$SHOT" /tmp/daylight
DISP=${DSWS_DISPLAY:-:97}
Xvfb $DISP -screen 0 1920x1080x24 >"$L/xvfb.log" 2>&1 & XV=$!
sleep 1
darling shell "$B/windowserver" </dev/null >"$L/server.log" 2>&1 & SRV=$!
for i in $(seq 30); do grep -q "serving com.apple.windowserver.active" "$L/server.log" && break; sleep 1; done
DISPLAY=$DISP "$HERE/build-darling-arm64/daylight-x11" /tmp/daylight >"$L/x11.log" 2>&1 & FE=$!
darling shell "$B/dsws_probe" </dev/null >"$L/probe.log" 2>&1
sleep 3
DISPLAY=$DISP import -window root "$SHOT/windowserver27-probe-client-nochrome.png"
cat "$L/probe.log"
kill $FE $XV 2>/dev/null
# server runs under setuid darling: stop it with sudo (by PID of the darling process, not by pattern)
sudo kill $SRV 2>/dev/null
