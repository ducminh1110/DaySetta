#!/bin/bash
# rebuild mldr + libsystem_kernel (dylib+static), relink dyl4, install, restart container
cd ~/darling/build-aarch64 && ninja mldr system_kernel system_kernel_static64 2>&1 | grep -E "error|FAILED"
~/dyld-1378/darling-compat/build-aarch64.sh link 2>&1 | tail -1
darling shutdown >/dev/null 2>&1; sleep 2; R=/usr/local/libexec/darling
for d in usr/libexec/darling bin; do sudo cp src/startup/mldr/mldr $R/$d/mldr.new && sudo mv $R/$d/mldr.new $R/$d/mldr; done
sudo cp src/external/xnu/darling/src/libsystem_kernel/libsystem_kernel.dylib $R/usr/lib/system/
sudo cp ~/dyld-port-a64e/dyld $R/usr/lib/dyl4
timeout -s KILL 90 darling shell true </dev/null >/dev/null 2>&1; echo up=$?
