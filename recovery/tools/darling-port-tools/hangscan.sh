#!/bin/sh
# hangscan.sh APP N [SECONDS]: runs a BaseSystem app N times (inputtest.sh, wait SECONDS, default 25) and reports
# for each run whether it has a window; without one, the main thread's backtrace (fpall.sh) goes to
# ~/darltest/hangscan/run<i>.txt for dscsym.py.
app=$1; n=$2; t=${3:-25}
mkdir -p ~/darltest/hangscan
cd ~/darling-port-tools
printf "wait %s\n" $((t + 8)) > ~/darltest/hangscan/script.txt
for i in $(seq 1 $n); do
	rm -rf ~/darltest/hangscan/out
	(DL4_TIMEOUT=$((t + 10)) sh inputtest.sh ~/darltest/hangscan/out "$app" ~/darltest/hangscan/script.txt >/dev/null 2>&1 &)
	sleep $t
	w=$(grep -a -c "bridge: window" ~/darltest/last.log)
	P=$(pgrep -f "^/Volumes/SystemRoot.*$(basename "$app" .app).app/Contents/MacOS" | head -1)
	echo "run $i: windows $w pid ${P:-none}"
	if [ "$w" = 0 ] && [ -n "$P" ]; then
		sh fpall.sh $P 2>/dev/null | awk '/^T /{n++} n==1' > ~/darltest/hangscan/run$i.txt
	fi
	while pgrep -f "^sh inputtest.sh" >/dev/null; do sleep 1; done
done
