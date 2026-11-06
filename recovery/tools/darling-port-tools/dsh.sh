#!/bin/sh
# dsh.sh CMD...: run a command in the Darling container and print its output afterwards. Never pipe `darling shell`
# directly: when it boots the container, launchd and daemons inherit the pipe and the reader never sees EOF.
# DSH_TIMEOUT (s, default 90) kills it; DSH_BS=1 adds the BaseSystem shared cache environment (dyl4 binaries).
out=$(mktemp /tmp/dsh.XXXXXX)
if [ -n "$DSH_BS" ]; then
	set -- env DYLD_SHARED_CACHE_DIR=/Volumes/SystemRoot/home/ubuntu/basesystem/System/Library/dyld/ \
		DYLD_SHARED_REGION=private "$@"
fi
timeout -s KILL ${DSH_TIMEOUT:-90} darling shell "$@" > "$out" 2>&1 </dev/null
rc=$?
cat "$out"; rm -f "$out"
exit $rc
