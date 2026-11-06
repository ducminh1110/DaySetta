#!/bin/sh
# fstest.sh APP TAG [SECONDS...]
# Run a ~/fullsystem app through fsapp.sh in the background and copy Daylight's
# screen PNG to ~/shots/<tag>-<t>.png at each of the given times (default 20 40).
# Then print the app log, fsapp log and Daylight log.
app=$1; tag=$2; shift 2
times="$*"; [ -z "$times" ] && times="20 40"
setsid nohup bash "$HOME/darling-port-tools/fsapp.sh" "$app" >/tmp/fs-$tag.log 2>&1 </dev/null &
prev=0
for t in $times; do
	sleep $((t - prev)); prev=$t
	cp -f "$HOME/.darling/private/tmp/daylight/desktop.png" "$HOME/shots/$tag-$t.png" 2>/dev/null
done
echo "=== fs-$tag.log (fsapp) ==="; tail -20 /tmp/fs-$tag.log
echo "=== last.log (app) ==="; tail -25 "$HOME/darltest/last.log" 2>/dev/null
echo "=== daylight.log ==="; tail -8 "$HOME/darltest/daylight.log" 2>/dev/null
echo "=== shots ==="; ls -l "$HOME"/shots/$tag-*.png 2>/dev/null
