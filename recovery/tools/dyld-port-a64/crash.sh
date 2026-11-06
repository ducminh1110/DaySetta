#!/bin/bash
# crash.sh BIN args: run under strace of shellspawn, print the signals
O=/tmp/claude-1001/-home-ubuntu/0b182f4a-fdf5-457c-bf1f-27afc98a81e7/scratchpad/st4.txt; rm -f $O
SP=$(ps -eo pid,args | awk '/usr\/libexec\/shellspawn/ && !/awk/ {print $1; exit}')
(sudo strace -f -qq -e trace=none -e signal=SIGILL,SIGSEGV,SIGBUS,SIGABRT -o $O -p $SP &) ; sleep 2
~/dyld-port-a64/run.sh "$@" >/dev/null 2>&1; sleep 1; sudo pkill -x strace; head -${N:-6} $O | cut -c1-300
