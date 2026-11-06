"""Point the port's helper scripts at ~/darling-port-tools instead of /tmp."""
import os, re, glob
d = os.path.expanduser('~/darling-port-tools')
for f in glob.glob(d + '/*.sh'):
    s = open(f).read()
    n = re.sub(r'/tmp/(dyld_try\.sh|dyld_link\.sh|dl4\.sh|use_dyl4\.py|batch\.sh|kinstall\.sh)', r'$HOME/darling-port-tools/\1', s)
    if n != s:
        open(f, 'w').write(n)
        print('fixed', os.path.basename(f))
