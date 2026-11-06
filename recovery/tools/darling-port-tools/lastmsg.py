import sys,re,collections
# For each thread: the last mach_msg (send/done pairs) in a machtrace log.
last={}; ids=collections.Counter()
for line in open(sys.argv[1],errors="replace"):
    m=re.match(r"machtrace: (\d+)/(\d+) (send|done)(.*)",line)
    if not m: continue
    last[(m.group(1),m.group(2))]=(m.group(3),m.group(4).strip())
for (p,t),(k,v) in sorted(last.items()):
    print(p,t,k,v)
