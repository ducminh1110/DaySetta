#!/bin/sh
# shotloop.sh: keep the web gallery's desktop image fresh from Daylight's PNG.
# Detached (run with setsid nohup). Sources:
#   ~/.darling/private/tmp/daylight/desktop.png  (Daylight writes it every few ms...max 1s)
# Writes ~/shots/00-desktop-now.png and, if writable, the served web root.
SRC="$HOME/.darling/private/tmp/daylight/desktop.png"
A="$HOME/shots/00-desktop-now.png"
B="/usr/share/novnc/shots/00-desktop-now.png"
while :; do
	if [ -f "$SRC" ]; then
		cp -f "$SRC" "$A" 2>/dev/null
		[ -w "$(dirname "$B")" ] && cp -f "$SRC" "$B" 2>/dev/null
	fi
	sleep 1
done
