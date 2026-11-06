#!/bin/bash
# Rebuild Darling's libsystem_kernel (+static for dyld), install it, relink dyl4.
cd ~/darling/build && (make -j8 system_kernel system_kernel_static64 > ~/darltest/mk.log 2>&1; echo make=$?)
grep error: ~/darltest/mk.log | head -5
darling shutdown >/dev/null 2>&1; sleep 2
sudo -n cp src/external/xnu/darling/src/libsystem_kernel/libsystem_kernel.dylib /usr/local/libexec/darling/usr/lib/system/
bash $HOME/darling-port-tools/dl4.sh build >/dev/null
