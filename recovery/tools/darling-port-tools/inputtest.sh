#!/bin/sh
# inputtest.sh OUTDIR APP SCRIPT: runs a BaseSystem app (bsapp.sh) and plays SCRIPT against it. Script lines:
#   wait SECONDS | shot NAME (copies the screen to OUTDIR/NAME.png) | x11 (starts daylight-x11 on Xvfb :99)
#   xdo ARGS (xdotool on :99) | anything else: an input command for Daylight's FIFO (see daylight input.h)
out=$1; app=$2; script=$3
mkdir -p "$out"
D=/tmp/daylight; P=~/.darling/private/tmp/daylight
darling shutdown >/dev/null 2>&1; sleep 3
DL4_TIMEOUT=${DL4_TIMEOUT:-150} DL4_ENV="DAYLIGHT_BRIDGE_VERBOSE=1 $DL4_ENV" bash ~/darling-port-tools/bsapp.sh "$app" >/dev/null 2>&1 &
bspid=$!
while IFS= read -r line; do
	case "$line" in
	"wait "*) sleep "${line#wait }" ;;
	"shot "*) cp $P/desktop.png "$out/${line#shot }.png" 2>/dev/null; echo "shot ${line#shot }" ;;
	x11)
		pgrep -x Xvfb >/dev/null || (Xvfb :99 -screen 0 1920x1080x24 >/dev/null 2>&1 &)
		sleep 1; (DISPLAY=:99 ~/darltest/daylight-x11 > "$out/x11.log" 2>&1 &) ;;
	"xdo "*) eval "DISPLAY=:99 xdotool ${line#xdo }" ;;
	"xshot "*) DISPLAY=:99 xwd -root -silent > "$out/${line#xshot }.xwd" ;;
	"sig "*) # sig SIGNAL: signal the app (the main thread gets it; Darling prints its stack for fatal signals)
		p=$(pgrep -f "^/Volumes/SystemRoot.*$(basename "$app" .app).app/Contents/MacOS" | head -1); [ -n "$p" ] && sudo -n kill -s ${line#sig } $p ;;
	""|"#"*) ;;
	*) timeout 2 sh -c 'printf "%s\n" "$1" > "$2"' _ "$line" $D/input ;; # open blocks without a reader
	esac
done < "$script"
wait $bspid
cp ~/darltest/last.log "$out/app.log"; cp ~/darltest/daylight.log "$out/daylight.log"
pkill -x daylight-x11 2>/dev/null
