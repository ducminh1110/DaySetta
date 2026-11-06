"""Print the source files of one target in an Xcode project (including dependency-free static libs)."""
import re, sys
s=open(sys.argv[1]).read()
objs={}
for m in re.finditer(r'\n\t\t([0-9A-F]{24}) (?:/\*.*?\*/ )?= \{(.*?)\n\t\t\};',s,re.S): objs[m.group(1)]=m.group(2)
targets={}
for k,v in objs.items():
    if 'isa = PBXNativeTarget' in v:
        name=re.search(r'\bname = "?([^";]+)"?;',v).group(1); targets[name]=v
def sources(name):
    v=targets[name]; out=[]
    phases=re.search(r'buildPhases = \((.*?)\);',v,re.S).group(1)
    for pid in re.findall(r'([0-9A-F]{24})',phases):
        pv=objs.get(pid,'')
        if 'PBXSourcesBuildPhase' in pv:
            for fid in re.findall(r'([0-9A-F]{24})',re.search(r'files = \((.*?)\);',pv,re.S).group(1)):
                fr=re.search(r'fileRef = ([0-9A-F]{24})',objs[fid]).group(1)
                out.append(re.search(r'path = "?([^";]+)"?;',objs[fr]).group(1))
    return out
if len(sys.argv)>2:
    for t in sys.argv[2:]: print(t, len(sources(t)), ' '.join(sources(t)))
