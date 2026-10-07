"""Fix MSVC6 for-loop scoping: a loop variable declared in one for() and reused in a later
for() without a declaration. Reads compiler logs, finds 'X was not declared' errors on lines
of the form 'for (X = ...', looks back for the earlier 'for (TYPE X = ...' to get the type,
and rewrites the later loop to declare it. Only touches lines matching that exact shape."""
import re, sys, glob, os, collections
# Usage: python3 fix_for_scope.py <logs folder>   (e.g. logs-default from nxdk_compile_test.sh)
LOGS = sys.argv[1] if len(sys.argv) > 1 else "logs"
# GCC: "'x' was not declared in this scope"   Clang: "use of undeclared identifier 'x'"
ERR = re.compile(r"^(/[^:]+):(\d+):\d+: error: (?:'(\w+)' was not declared in this scope"
                 r"|use of undeclared identifier '(\w+)')", re.M)
fixes = collections.defaultdict(set)   # path -> {(line, var)}
for log in glob.glob(os.path.join(LOGS, "*.log")):
    for m in ERR.finditer(open(log, errors="replace").read()):
        fixes[m.group(1)].add((int(m.group(2)), m.group(3) or m.group(4)))
total = 0
for path, items in fixes.items():
    lines = open(path, encoding="latin-1").read().split("\n")
    changed = False
    for lineno, var in sorted(items):
        i = lineno - 1
        pat = re.compile(r"(\bfor\s*\(\s*)(" + re.escape(var) + r"\s*=)")
        if not pat.search(lines[i]): continue
        decl = re.compile(r"\bfor\s*\(\s*((?:const\s+|unsigned\s+|signed\s+|long\s+|short\s+)*[\w:<>]+(?:\s*\*)*)\s+(\*?)"
                          + re.escape(var) + r"\s*=")
        vtype = None
        for j in range(i - 1, max(-1, i - 400), -1):
            d = decl.search(lines[j])
            if d: vtype = d.group(1).strip() + (" *" if d.group(2) else ""); break
        if not vtype: continue
        lines[i] = pat.sub(lambda m: m.group(1) + vtype + " " + m.group(2), lines[i], count=1)
        changed = True; total += 1
        print(f"  {path.split('/src/')[-1]}:{lineno}  for ({vtype} {var} = ...")
    if changed: open(path, "w", encoding="latin-1").write("\n".join(lines))
print(f"fixed {total} loops")
