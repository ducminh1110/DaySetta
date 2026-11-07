#!/bin/bash
# dl4.sh build FILE...   recompile changed dyld sources, relink, install as /usr/lib/dyl4
# dl4.sh run BIN ARGS... run a BaseSystem binary (path inside ~/basesystem) through dyl4 + the 26.6 cache
R=/usr/local/libexec/darling
# Restart the Darling container if its shellspawn is gone (e.g. after replacing system libraries).
ensure_darling() {
	if ! timeout -s KILL 60 darling shell true </dev/null >/dev/null 2>&1; then
		darling shutdown >/dev/null 2>&1; sudo -n pkill -9 -x darlingserver; sudo -n pkill -9 -x mldr; sleep 2
		timeout -s KILL 90 darling shell true </dev/null >/dev/null 2>&1
	fi
}

case "$1" in
build)
	shift; cd ~/dyld-1378 && [ $# -gt 0 ] && bash $HOME/darling-port-tools/dyld_try.sh "$@" | head -3
	bash $HOME/darling-port-tools/dyld_link.sh 2>&1 | grep link= && sudo -n cp ~/dyld-port/dyld $R/usr/lib/dyl4 ;;
run)
	ensure_darling
	shift; b=$1; shift; n=$(echo "$b" | tr / _)
	python3 $HOME/darling-port-tools/use_dyl4.py ~/basesystem/$b ~/darltest/bs$n >/dev/null && chmod +x ~/darltest/bs$n
	timeout -s KILL ${DL4_TIMEOUT:-60} darling shell env DYLD_SHARED_CACHE_DIR=/Volumes/SystemRoot/home/ubuntu/basesystem/System/Library/dyld/ DYLD_SHARED_REGION=private $DL4_ENV /Volumes/SystemRoot/home/ubuntu/darltest/bs$n "$@" > ~/darltest/last.log 2>&1 </dev/null
	echo "rc=$?"; head -${DL4_LINES:-40} ~/darltest/last.log ;;
esac
