#!/bin/bash
# build.sh: build micro.c as an arm64e Mach-O (stub dylibs only provide link-time symbols).  Output: ~/darltest/bench/micro
set -e
D=~/darling; LD=$D/build-aarch64/src/external/cctools-port/cctools/ld64/src/aarch64-apple-darwin20-ld
O=~/darltest/bench; mkdir -p $O; cd $O
CF="clang -target arm64e-apple-darwin20 -mcpu=generic+crc+aes+pauth -nostdinc -ffreestanding -fPIC -O1"
RT=~/dyld-1378/darling-compat/retag-obj.py
stub() { # name installname symbols...
  n=$1; inst=$2; shift 2; : > $n.c; for s in "$@"; do echo "void* $s(void){return 0;}" >> $n.c; done
  $CF -c $n.c -o $n.o; python3 $RT $n.o
  $LD -arch arm64e -platform_version macos 11.0 11.0 -dylib -install_name $inst $n.o -o $n.dylib 2>&1 | grep -v warning || true
}
stub libSystem /usr/lib/libSystem.B.dylib printf clock_gettime dispatch_get_global_queue dispatch_async_f dispatch_semaphore_create dispatch_semaphore_signal dispatch_semaphore_wait dispatch_time
stub libobjc /usr/lib/libobjc.A.dylib objc_getClass sel_registerName objc_msgSend
stub CoreFoundation /System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation CFStringCreateWithCString CFStringGetLength CFRetain CFRelease CFStringCreateMutableCopy CFStringAppend
$CF -c ~/daysetta/tools/bench/micro.c -o micro.o; python3 $RT micro.o
$LD -arch arm64e -platform_version macos 11.0 11.0 -fixup_chains -e _main micro.o libSystem.dylib libobjc.dylib CoreFoundation.dylib -o micro 2>&1 | grep -v warning || true
python3 -I - <<'PY'
import struct,os
p=os.path.expanduser('~/darltest/bench/micro')
b=bytearray(open(p,'rb').read()); struct.pack_into('<I',b,8,0x80000002); open(p,'wb').write(b)
PY
chmod +x micro; ls -la micro
