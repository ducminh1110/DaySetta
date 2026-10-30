#!/bin/bash
# run.sh [--no-cold]: time genuine macOS 27 commands on Darling aarch64 via dyl4 + the 27 cache, warm and cold, with ds-exec trap counts.
# Needs: installed Darling (darling-port-tools/dsh.sh), ~/darltest/{echo.s1,plutil.s1,t.plist}, ~/darltest/bench/micro (build.sh).
# Output: artifacts/bench/<date>.txt  (never contains Apple binaries; the 27 inputs live outside the repo)
D=$(cd "$(dirname "$0")/../.." && pwd); OUT=$D/artifacts/bench/$(date +%F-%H%M).txt; mkdir -p "$D/artifacts/bench"
CACHE=/home/ubuntu/macos-re/cache-arm64e/dyld_shared_cache_arm64e; T=/Volumes/SystemRoot/home/ubuntu/darltest
[ -x ~/darltest/bench/micro ] || bash "$(dirname "$0")/build.sh" >/dev/null
run() { # label timeout args...
  local label=$1 to=$2; shift 2
  local f=$(mktemp); local s=$(date +%s.%N)
  timeout -s KILL $to darling shell env MLDR_DSEXEC_TIMING=1 DARLING_DYLINKER=/usr/lib/dyl4 DARLING_DS_CACHE=$CACHE "$@" >"$f" 2>&1 </dev/null; local rc=$?
  local e=$(date +%s.%N)
  printf '%-34s wall=%6.2fs rc=%d\n' "$label" "$(echo "$e - $s" | bc)" $rc | tee -a "$OUT"
  grep -E "^ds-exec: (cache map|emulated)" "$f" | sed 's/^/    /' | tee -a "$OUT"
  [ -n "$V" ] && grep -v "^ds-exec" "$f" | head -${V} | sed 's/^/    | /' | tee -a "$OUT"; rm -f "$f"
}
{ echo "# bench $(date -Is) host=$(uname -m) cpus=$(nproc)"; echo "# commit daysetta=$(git -C $D rev-parse --short HEAD) darling=$(git -C ~/darling rev-parse --short HEAD) dyld=$(git -C ~/dyld-1378 rev-parse --short HEAD)"; } | tee "$OUT"
if [ "$1" != "--no-cold" ]; then
  echo "## cold (container restarted, page cache dropped)" | tee -a "$OUT"
  darling shutdown >/dev/null 2>&1 </dev/null; sleep 2; sudo -n sh -c 'sync; echo 3 > /proc/sys/vm/drop_caches' 2>/dev/null
  darling shell true </dev/null >/dev/null 2>&1; darling shell true </dev/null >/dev/null 2>&1
  run "cold 27 echo" 120 $T/echo.s1 hi
fi
echo "## warm" | tee -a "$OUT"
for i in 1 2 3; do run "27 echo #$i" 120 $T/echo.s1 hi; done
for i in 1 2 3; do run "27 plutil -help #$i" 120 $T/plutil.s1 -help; done
run "27 plutil -convert xml1 #1" 120 $T/plutil.s1 -convert xml1 -o - $T/t.plist
run "27 plutil -p (needs vouchers/cfprefsd)" 120 $T/plutil.s1 -p $T/t.plist
V=8 run "micro: objc/CF/dispatch x1e6" 300 $T/bench/micro
