#!/bin/sh
# Cross-builds the window server + probe client as Mach-O with the Darling toolchain (adapted from
# daylight/tools/build-darling.sh). ARCH=arm64 (default) or x86_64. Output: $OUT (default build-darling-$ARCH).
set -e
ARCH=${ARCH:-arm64}
case $ARCH in
arm64) LDARCH=aarch64; DARLING_BUILD_DEFAULT=$HOME/darling/build-aarch64 ;;
x86_64) LDARCH=x86_64; DARLING_BUILD_DEFAULT=$HOME/darling/build; [ -d "$DARLING_BUILD_DEFAULT" ] || DARLING_BUILD_DEFAULT=$HOME/darling/build-aarch64 ;;
*) echo "ARCH must be arm64 or x86_64" >&2; exit 2 ;;
esac
DARLING_SRC=${DARLING_SRC:-$HOME/darling}
DARLING_BUILD=${DARLING_BUILD:-$DARLING_BUILD_DEFAULT}
DARLING_ROOT=${DARLING_ROOT:-/usr/local/libexec/darling}
SDK=$DARLING_SRC/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk
# generated headers (MIG output such as mach/clock_priv.h) live in the build tree overlay, searched first
GEN=$DARLING_BUILD/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk/usr/include
LD=$DARLING_BUILD/src/external/cctools-port/cctools/ld64/src/$LDARCH-apple-darwin20-ld
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${OUT:-$HERE/build-darling-$ARCH}
mkdir -p "$OUT"

cc() { clang -target $ARCH-apple-macos11 -isysroot "$SDK" -isystem "$GEN" -DXNU_PLATFORM_MacOSX -Wno-deprecated-declarations -O1 -g -Wall -Wextra -I"$HERE/include" -c "$@"; }
# COMPILE_ONLY=1: compile every file but do not link (the installed Darling root here is arm64 only)
link() { [ -n "${COMPILE_ONLY:-}" ] && return 0; out=$1; shift; "$LD" -arch $ARCH -platform_version macos 11.0 11.0 -syslibroot "$DARLING_ROOT" -lSystem -U _darling_memory_entry_create -o "$out" "$@"; }

OBJS=
for f in bridge input process dispatch dl_routines dl_canned dl_reply_sizes dl_shm_seed dl_display_state display window session shell main_darling; do
	cc "$HERE/src/$f.c" -o "$OUT/$f.o"
	OBJS="$OBJS $OUT/$f.o"
done
link "$OUT/windowserver" $OBJS -lbsm
for t in "$HERE"/probe/*.c; do
	n=$(basename "$t" .c)
	cc -I"$HERE/src" "$t" -o "$OUT/$n.o"
	link "$OUT/$n" "$OUT/$n.o"
done
echo "built into $OUT"
[ -n "${COMPILE_ONLY:-}" ] && { echo "compiled (not linked) for $ARCH"; exit 0; }
# host X11 frontend (Linux, native)
[ -n "$NO_X11" ] || ${HOST_CC:-/usr/bin/gcc} -O1 -Wall -o "$OUT/daylight-x11" "$HERE/frontends/x11/daylight-x11.c" -lX11
