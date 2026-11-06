#!/bin/sh
# fpall.sh PID: frame-pointer backtraces of every thread of a Darling process (gdb attach). Prints
# "T tid comm wchan" then "F addr image+offset" lines; symbolize cache frames with dscsym.py.
P=$1
for t in /proc/$P/task/*; do
	T=$(basename $t)
	echo "T $T $(cat $t/comm | tr ' ' _) $(cat $t/wchan)"
	sudo -n gdb -batch -nx -p $P -ex "python
import gdb
for th in gdb.selected_inferior().threads():
    if th.ptid[1] == $T:
        th.switch()
pc = int(gdb.parse_and_eval('\$pc')); bp = int(gdb.parse_and_eval('\$rbp'))
print('RA %x' % pc)
inf = gdb.selected_inferior()
for i in range(40):
    if bp == 0: break
    try:
        m = inf.read_memory(bp, 16).tobytes()
    except Exception:
        break
    nbp = int.from_bytes(m[:8], 'little'); ra = int.from_bytes(m[8:], 'little')
    print('RA %x' % ra)
    if nbp <= bp: break
    bp = nbp
end" 2>/dev/null | grep '^RA' | while read _ a; do
		sudo -n awk -v a=$a 'BEGIN{x=strtonum("0x" a)} {split($1,r,"-"); s=strtonum("0x" r[1]); e=strtonum("0x" r[2]); if (x>=s && x<e) {n=split($6,p,"/"); printf "F %s %s+0x%x\n", a, p[n], x-s+strtonum("0x" $3); exit}}' /proc/$P/maps
	done
done
