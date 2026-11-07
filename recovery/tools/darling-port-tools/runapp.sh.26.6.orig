#!/bin/sh
# runapp.sh <binary under ~/darltest/apps> [args]: run a macOS app binary through dyl4 + the 26.6 cache, log to ~/darltest/last.log
b=$1; shift
python3 $HOME/darling-port-tools/use_dyl4.py ~/darltest/apps/$b ~/darltest/bs$b >/dev/null && chmod +x ~/darltest/bs$b
timeout -s KILL ${DL4_TIMEOUT:-60} darling shell sh -c "env DYLD_SHARED_CACHE_DIR=/Volumes/SystemRoot/home/ubuntu/basesystem/System/Library/dyld/ DYLD_SHARED_REGION=private $DL4_ENV /Volumes/SystemRoot/home/ubuntu/darltest/bs$b $*; echo exit=\$?" > ~/darltest/last.log 2>&1 </dev/null
echo rc=$?; head -${DL4_LINES:-60} ~/darltest/last.log
