#!/usr/bin/env bash
# Kiểm tra máy x86 (ubuntu@remotemac.mooo.com qua ~/bin/x86) sau khi
# provider khôi phục dữ liệu. Chỉ đọc, không thay đổi gì trên máy x86.
#
# Cách dùng:
#   bash recovery/check-x86-recovered.sh
set -euo pipefail

X86=/home/ubuntu/bin/x86

"$X86" '
echo "=== HOST ==="
hostname; uname -m; date

echo "=== TREES ==="
for d in ~/darling ~/darltest ~/build-darling ~/darling-port-patches ~/darling-port-tools; do
  if [ -e "$d" ]; then ls -ld "$d"; else echo "MISSING $d"; fi
done

echo "=== DARLINGSERVER GIT ==="
DS=~/darling/src/external/darlingserver
if [ -d "$DS" ]; then
  git -C "$DS" log --oneline -5
  echo "--- status ---"
  git -C "$DS" status --porcelain | head -30
  echo "--- local commits (not on remotes) ---"
  git -C "$DS" rev-list --count HEAD --not --remotes 2>/dev/null || true
else
  echo "MISSING $DS"
fi

echo "=== FIX PRESENCE CHECKS ==="
if [ -d "$DS" ]; then
  echo "--- fileport in src/include ---"
  grep -RIl fileport "$DS/src" "$DS/include" 2>/dev/null | head -10 \
    || echo "no fileport matches"
  echo "--- audit token lines ---"
  grep -n "audit_token.val" "$DS/duct-tape/src/task.c" 2>/dev/null | head -10 \
    || echo "task.c not found or no matches"
  echo "--- restartable routines ---"
  grep -n "task_restartable_ranges" "$DS/duct-tape/src/task.c" 2>/dev/null | head -5 \
    || echo "no matches"
  echo "--- asid 100000 ---"
  grep -RIn "100000" "$DS/duct-tape/src" 2>/dev/null | head -5 \
    || echo "no matches"
fi

echo "=== /usr/local INSTALL ==="
ls -l /usr/local/bin/darling* /usr/local/sbin/darling* 2>/dev/null | head -10 \
  || echo "no darling binaries in /usr/local"

echo "=== HOME SUMMARY ==="
ls -la ~ | head -25
'
