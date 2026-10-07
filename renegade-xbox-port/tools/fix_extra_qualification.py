"""Remove extra qualifications reported by the compiler ("extra qualification 'X::' on member 'Y'"):
inside class X, a member declared as X::Y(...) becomes Y(...). Reads compiler logs; only edits
the reported line, and only the exact 'X::' token in front of the named member."""
import re, sys, glob, os, collections
logs = sys.argv[1]
ERR = re.compile(r"^(/[^:]+):(\d+):\d+: error: extra qualification '(\w+)::' on member '([~\w]+|operator\s*[^']+)'", re.M)
fixes = collections.defaultdict(set)
for log in glob.glob(os.path.join(logs, "*.log")):
    for m in ERR.finditer(open(log, errors="replace").read()):
        fixes[m.group(1)].add((int(m.group(2)), m.group(3), m.group(4)))
n = 0
for path, items in fixes.items():
    lines = open(path, encoding="latin-1").read().split("\n")
    for lineno, cls, member in items:
        i = lineno - 1
        if member.startswith("operator"):
            member_re = r"operator\s*" + re.escape(member[len("operator"):].strip())
        else:
            member_re = re.escape(member)
        new = re.sub(r"\b" + re.escape(cls) + r"::\s*(?=" + member_re + r")", "", lines[i], count=1)
        if new != lines[i]:
            lines[i] = new; n += 1
            print(f"  {path.split('/src/')[-1]}:{lineno}  {cls}::{member}")
    open(path, "w", encoding="latin-1").write("\n".join(lines))
print(f"removed {n} extra qualifications")
