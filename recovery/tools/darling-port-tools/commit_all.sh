#!/bin/bash
# Commit work in progress in every dirty Darling submodule and the superproject, on port/macos26 branches.
MSG="$1"
cd ~/darling
git config user.name >/dev/null || git config --global user.name "Darling port (local)"
git config user.email >/dev/null || git config --global user.email "darling-port@localhost"
git submodule foreach --quiet --recursive 'if [ -n "$(git status --porcelain)" ]; then
	b=$(git branch --show-current); [ -z "$b" ] && git checkout -q -b port/macos26
	git add -A && git commit -q -m "'"$MSG"'

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>" && echo "committed $name on $(git branch --show-current)"; fi'
b=$(git branch --show-current); [ -z "$b" ] && git checkout -q -b port/macos26
git add -A && git commit -q -m "$MSG

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>" && echo "committed superproject on $(git branch --show-current)"
