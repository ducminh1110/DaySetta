#!/usr/bin/env python3 -I
# dis.py ADDR [NWORDS]: disassemble cache code at an (unslid) address of the 27 cache (reads .NN files directly)
import struct,glob,sys,subprocess
addr=int(sys.argv[1],16); n=int(sys.argv[2]) if len(sys.argv)>2 else 24
D='/home/ubuntu/macos-re/cache-arm64e/'
for fn in sorted(glob.glob(D+'dyld_shared_cache_arm64e*')):
    if fn.endswith(('.map','.atlas')): continue
    h=open(fn,'rb').read(0x4000)
    if h[:4]!=b'dyld': continue
    mo,mc=struct.unpack_from('<II',h,0x10)
    for i in range(mc):
        a,sz,fo=struct.unpack_from('<QQQ',h,mo+i*32)
        if a<=addr<a+sz:
            d=open(fn,'rb'); d.seek(fo+addr-a); b=d.read(n*4)
            hexs=' '.join('0x%02x'%x for x in b)
            print(subprocess.run(['llvm-mc','--disassemble','-triple=arm64e','-mattr=+v8.5a,+pauth,+lse,+rcpc'],input=hexs,capture_output=True,text=True).stdout)
            sys.exit()
