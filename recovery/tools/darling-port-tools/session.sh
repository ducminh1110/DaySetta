#!/bin/sh
# session.sh <app> [args]: Daylight (window server) + the app; daylight-ca (QuartzCore render server) is the
# launchd job org.daylight.ca, started on demand when the app first talks to com.apple.CARenderServer.
darling shell true </dev/null >/dev/null 2>&1
T=${DL4_TIMEOUT:-60}
(timeout -s KILL $T darling shell env $DAYLIGHT_ENV /Volumes/SystemRoot/home/ubuntu/daylight/build-darling/daylight > ~/darltest/daylight.log 2>&1 </dev/null &)
sleep 3
~/darltest/runapp.sh "$@"
sleep 2
for p in $(pgrep -f "build-darling/daylight\$"); do sudo -n kill -INT $p; done
sleep 1
