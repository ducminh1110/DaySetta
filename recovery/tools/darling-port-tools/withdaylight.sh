#!/bin/sh
# withdaylight.sh <app> [args]: start Daylight inside the prefix, run the app through runapp.sh, stop Daylight.
darling shell true </dev/null >/dev/null 2>&1
(timeout -s KILL ${DL4_TIMEOUT:-60} darling shell env $DAYLIGHT_ENV /Volumes/SystemRoot/home/ubuntu/daylight/build-darling/daylight > ~/darltest/daylight.log 2>&1 &)
sleep 3
~/darltest/runapp.sh "$@"
for p in $(pgrep -f "build-darling/daylight\$"); do sudo -n kill -INT $p; done
sleep 1
