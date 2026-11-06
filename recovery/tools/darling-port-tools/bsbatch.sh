#!/bin/sh
# bsbatch.sh <out dir> <app path>...: run BaseSystem GUI apps one after another with bsapp.sh and keep, per app,
# the composited screen (desktop.png), the app's output and Daylight's log.
out=$1; shift
mkdir -p "$out"
for app in "$@"; do
	n=$(basename "$app" .app | tr ' ' '_')
	darling shutdown >/dev/null 2>&1; sleep 3
	rm -rf ~/.darling/private/tmp/daylight
	DL4_TIMEOUT=${DL4_TIMEOUT:-50} DL4_ENV="DAYLIGHT_BRIDGE_VERBOSE=1" timeout 150 bash ~/darling-port-tools/bsapp.sh "$app" >/dev/null 2>&1
	cp ~/darltest/last.log "$out/$n.log"; cp ~/darltest/daylight.log "$out/$n.daylight.log"
	cp ~/.darling/private/tmp/daylight/desktop.png "$out/$n.png" 2>/dev/null
	echo "$n: $(grep -c 'bridge: window' "$out/$n.log") frames, $(grep -o 'exit=[0-9]*' "$out/$n.log" | tail -1), fatal=$(grep -c 'Darling: fatal' "$out/$n.log")"
done
