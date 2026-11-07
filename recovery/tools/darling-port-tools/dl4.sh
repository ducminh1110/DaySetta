#!/bin/bash
# dl4.sh build FILE...   recompile changed dyld sources, relink, install as /usr/lib/dyl4
# dl4.sh run BIN ARGS... run a macOS 27 binary (path inside ~/macos-re/store-27/root, or absolute) through dyl4 + the 27 cache (retargeted from 26.6 BaseSystem)
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
	shift; B=$HOME/dyld-1378/darling-compat/build-aarch64.sh
	[ $# -gt 0 ] && bash $B compile "$@" | head -3
	bash $B link 2>&1 | grep link= && sudo -n cp ~/dyld-port-a64e/dyld $R/usr/lib/dyl4.new && sudo -n mv -f $R/usr/lib/dyl4.new $R/usr/lib/dyl4 ;;
run)
	ensure_darling
	shift; b=$1; shift; case $b in /*) p=$b;; *) p=$HOME/macos-re/store-27/root/$b;; esac
	timeout -s KILL ${DL4_TIMEOUT:-60} darling shell env DARLING_DYLINKER=/usr/lib/dyl4 DARLING_DS_CACHE=/home/ubuntu/macos-re/cache-arm64e/dyld_shared_cache_arm64e $DL4_ENV /Volumes/SystemRoot$p "$@" > ~/darltest/last.log 2>&1 </dev/null
	echo "rc=$?"; head -${DL4_LINES:-40} ~/darltest/last.log ;;
esac
