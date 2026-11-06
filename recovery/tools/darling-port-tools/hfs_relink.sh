#!/bin/sh
# hfs_relink.sh <7z extraction dir> <links.txt from hfs_hardlinks.py>: replaces the empty link files with hard links
# to their "[HFS+ Private Data]/iNode<N>" target.
dir=$1; list=$2
n=0
while IFS="$(printf '\t')" read -r node rel; do
	t="$dir/[HFS+ Private Data]/$node"
	[ -f "$t" ] || continue
	d=$(dirname "$dir/$rel")
	chmod u+w "$d" 2>/dev/null
	ln -f "$t" "$dir/$rel" && n=$((n + 1))
done < "$list"
echo "relinked $n"
