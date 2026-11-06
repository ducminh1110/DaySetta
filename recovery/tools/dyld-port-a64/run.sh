#!/bin/bash
# run.sh BIN [args]: run a Mach-O under Darling with dyl4 + 27 cache; log in $LOG
S=${SDIR:-/tmp/claude-1001/-home-ubuntu/0b182f4a-fdf5-457c-bf1f-27afc98a81e7/scratchpad/apple}
b=$1; shift; case $b in /*) p=$b;; *) p=$S/$b;; esac
rm -f /tmp/st4 /tmp/log4
MLDR_DSEXEC_STATS=/tmp/st4 MLDR_DSEXEC_LOG=/tmp/log4 timeout ${T:-120} darling shell env DARLING_DYLINKER=/usr/lib/dyl4 DARLING_DS_CACHE=/home/ubuntu/macos-re/cache-arm64e/dyld_shared_cache_arm64e $XENV /Volumes/SystemRoot$p "$@" </dev/null >/tmp/out4 2>/tmp/err4
echo rc=$?; xxd /tmp/out4 | head -5; head -${L:-20} /tmp/err4; grep -v "=0$" /tmp/st4 | tr '\n' ' '; echo; head -5 /tmp/log4
