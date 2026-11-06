#!/bin/bash
# Assemble/compile the remaining dyld-1378 pieces and link them like Darling's system_loader.
S=~/dyld-1378; O=~/dyld-port/obj; D=~/darling; B=$D/build; E=$B/src/external
SDK=$D/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk
eval "$(sed -n 's/^\(DEFS\|INCS\|FL\)=/\1=/p' $HOME/darling-port-tools/dyld_try.sh)"
set -e
clang $DEFS $INCS -Wno-undef-prefix -Wno-macro-redefined -include $S/darling-compat/darling_dyld_prelude.h -target x86_64-apple-darwin20 -nostdinc -c $S/dyld/dyldStartup.s -o $O/dyldStartup.o
clang $DEFS $INCS -Wno-undef-prefix -Wno-macro-redefined -include $S/darling-compat/darling_dyld_prelude.h -target x86_64-apple-darwin20 -nostdinc -c $S/lsl/ProtectedStack.s -o $O/ProtectedStack.o
clang -std=gnu11 $DEFS $INCS $FL -c $S/darling-compat/darling_glue.c -o $O/darling_glue.o
clang -std=c++20 $DEFS $INCS $FL -c $S/darling-compat/darling_glue_cxx.cpp -o $O/darling_glue_cxx.o
set +e
LD=$E/cctools-port/cctools/ld64/src/x86_64-apple-darwin20-ld
A="$E/libc/liblibc_static.a $E/libclosure/libsystem_blocks_static.a $E/libcxx/libcxx_static.a $E/libunwind/libunwind_static.a $B/src/libm/libsystem_m_static.a $E/libcxxabi/libcxxabi_static.a $E/keymgr/libkeymgr_static.a $B/src/duct/src/libsystem_duct_static.a $E/libpthread/src/libpthread_static.a $E/cctools/libmacho/libmacho_static.a $E/libtrace/libsystem_trace_static.a $E/corecrypto/libcorecrypto_static.a $E/libc/liblibc_static64.a $E/compiler-rt/lib/builtins/libcompiler_rt_static64.a $E/libplatform/libplatform_static64.a $E/xnu/darling/src/libsystem_kernel/libsystem_kernel_static64.a $B/src/libsimple-darling/liblibsimple_darling.a"
$LD -arch x86_64 -platform_version macos 11.0 11.0 -dylinker -dead_strip -data_const -e __dyld_start -fixup_chains -image_base 0x1fe00000 \
  -exported_symbols_list $S/dyld/dyld.exp -o ~/dyld-port/dyld $O/*.o $A 2> ~/dyld-port/link.log
echo link=$?
grep -c "^ *\"_" ~/dyld-port/link.log
grep -v "^ld: warning" ~/dyld-port/link.log | grep "^  \"" | sed 's/, referenced.*//' | sort -u | head -80
