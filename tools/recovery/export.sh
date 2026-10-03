#!/bin/bash
# Export local-only work (Darling fork, dyl4, recovery notes, owner tools) into recovery/
# as git bundles (exact commits incl. merges) so a lost host can be rebuilt.
# Restore: see recovery/README.md. Never exports Apple binaries or keys.
set -euo pipefail
R=$(cd "$(dirname "$0")/../.." && pwd)/recovery
H=/home/ubuntu
rm -rf "$R/darling" "$R/dyld-1378"; mkdir -p "$R/darling" "$R/dyld-1378" "$R/notes" "$R/tools"

bund() { # repo dir, out file, label, manifest
  local d=$1 out=$2 label=$3 m=$4 excl="" n
  n=$(git -C "$d" rev-list --count HEAD --not --remotes 2>/dev/null || echo 0)
  if [ "$n" != 0 ]; then
    local oldest; oldest=$(git -C "$d" rev-list HEAD --not --remotes | tail -1)
    # shallow/imported root (Apple OSS tag or upstream shallow tip): exclude, record it to refetch
    git -C "$d" rev-parse -q --verify "$oldest^" >/dev/null || { excl="^$oldest"; n=$((n-1)); }
    [ "$n" -gt 0 ] && git -C "$d" bundle create -q "$out" HEAD --not --remotes $excl
  fi
  git -C "$d" diff HEAD --binary > "$out.uncommitted.diff"; [ -s "$out.uncommitted.diff" ] || rm "$out.uncommitted.diff"
  echo "$label head=$(git -C "$d" rev-parse HEAD) branch=$(git -C "$d" branch --show-current) local_commits=$n ${excl:+excluded_import=${excl#^}($(git -C "$d" log -1 --format=%s ${excl#^}))}" >> "$m"
}
D=$H/darling; M=$R/darling/MANIFEST.txt; : > "$M"
bund "$D" "$R/darling/_super.bundle" _super "$M"
git -C "$D" submodule foreach --quiet 'echo $sm_path' | while read -r p; do
  n=$(git -C "$D/$p" rev-list --count HEAD --not --remotes 2>/dev/null || echo 0)
  dirty=$(git -C "$D/$p" status --short --untracked-files=no | wc -l)
  if [ "$n" != 0 ] || [ "$dirty" -gt 0 ]; then bund "$D/$p" "$R/darling/${p//\//__}.bundle" "$p" "$M"; fi
done
bund $H/dyld-1378 "$R/dyld-1378/dyld-1378.bundle" dyld-1378 "$R/dyld-1378/MANIFEST.txt"

rsync -a --include='*/' --include='*.md' --include='*.txt' --include='*.tsv' --exclude='*' $H/darling-recovery/ "$R/notes/darling-recovery/"
rsync -a $H/darling-port-patches/ "$R/notes/darling-port-patches/"
rsync -a $H/darling-port-tools/ "$R/tools/darling-port-tools/"
rsync -a --include='*.sh' --include='*.py' --include='*.md' --exclude='*' $H/dyld-port-a64/ "$R/tools/dyld-port-a64/"
cp $H/DARLING-MACOS26-HANDOFF.md $H/NEW-SESSION-PROMPT.md "$R/notes/" 2>/dev/null || true
rsync -a $H/cleanup-report/ "$R/notes/cleanup-report/"
mkdir -p "$R/notes/claude-memory"; cp $H/.claude/projects/-home-ubuntu/memory/*.md "$R/notes/claude-memory/"
echo "exported to $R ($(du -sh "$R" | cut -f1))"
