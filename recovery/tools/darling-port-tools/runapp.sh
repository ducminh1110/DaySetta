#!/bin/sh
# runapp.sh <binary under ~/darltest/apps> [args]: run a genuine macOS 27 app binary through dyl4 + the 27 cache (retargeted from 26.6), log to ~/darltest/last.log
b=$1; shift
timeout -s KILL ${DL4_TIMEOUT:-60} darling shell sh -c "env DARLING_DYLINKER=/usr/lib/dyl4 DARLING_DS_CACHE=/home/ubuntu/macos-re/cache-arm64e/dyld_shared_cache_arm64e $DL4_ENV /Volumes/SystemRoot/home/ubuntu/darltest/apps/$b $*; echo exit=\$?" > ~/darltest/last.log 2>&1 </dev/null
echo rc=$?; head -${DL4_LINES:-60} ~/darltest/last.log
