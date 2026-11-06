#!/bin/bash
# Trial compile of dyld-1378 sources against Darling's SDK; reports per-file errors.
S=~/dyld-1378; O=~/dyld-port/obj; mkdir -p $O; : > ~/dyld-port/errors.txt
D=~/darling; B=$D/build; SDK=$D/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk
DEFS="-DDARLING -DDARWIN -DPLATFORM_MacOSX -DTARGET_OS_MAC=1 -DXNU_PLATFORM_MacOSX -D_DARWIN_C_SOURCE -D_LIBCPP_NO_EXCEPTIONS=1 -D_LIBC_NO_FEATURE_VERIFICATION -D__APPLE__ -D__DYNAMIC__ -D__MACH__ -DBUILDING_DYLD=1 -DPRIVATE=1"
INCS="-I$S/darling-compat -I$S/dyld -I$S/common -I$S/lsl -I$S/mach_o -I$S/libdyld -I$S/include -I$S/include/mach-o -I$S/cache_builder -I$S/shared_cache_runtime -I$S/other-tools -I$S/darling-compat/libcxx -I/usr/lib/llvm-18/lib/clang/18/include -I$B/src/include -I$D/src/include -I$D/basic-headers -I$SDK/usr/include -I$B/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk/usr/include -I$D/framework-include -I$D/framework-private-include -I$D/src/external/lkm/include -I$D/src/lib/include"
FL="-Wno-nullability-completeness -Wno-deprecated-declarations -Wno-availability -Wno-expansion-to-defined -Wno-elaborated-enum-base -Wno-undef-prefix -mmacosx-version-min=11.0 -target x86_64-apple-darwin20 -nostdinc -fPIC -fno-stack-check -fno-exceptions -fno-rtti -O1 -g -include $S/darling-compat/darling_dyld_prelude.h -include $D/src/duct/include/CrashReporterClient.h"
ok=0; bad=0
for f in "$@"; do
  case $f in *.cpp) std=-std=c++20;; *.c) std=-std=gnu11;; *) std="";; esac
  o=$O/$(echo $f | tr / _).o
  if clang $std $DEFS $INCS $FL -c $S/$f -o $o 2>>~/dyld-port/errors.txt; then ok=$((ok+1)); else bad=$((bad+1)); echo "FAILED $f" >> ~/dyld-port/errors.txt; fi
done
echo ok=$ok bad=$bad
grep "error:" ~/dyld-port/errors.txt | sed "s/^[^ ]*error: //" | sort | uniq -c | sort -rn | head -60
