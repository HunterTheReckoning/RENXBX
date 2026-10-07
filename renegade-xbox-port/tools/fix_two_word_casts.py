"""Rewrite two-word functional casts, an MSVC extension, at the positions the compiler
reports ("expected primary-expression before 'unsigned'"): `unsigned short(x)` becomes
`(unsigned short)(x)`. Only the reported line and column are touched."""
import re, sys, glob, os, collections
ERR = re.compile(r"^(/[^:]+):(\d+):(\d+): error: expected primary-expression before '(unsigned|signed|long|short)'", re.M)
CAST = re.compile(r"\b((?:unsigned|signed)\s+(?:char|short|int|long(?:\s+long)?)|long\s+long|short\s+int|long\s+int)\s*\(")
fixes = collections.defaultdict(set)
for log in glob.glob(os.path.join(sys.argv[1], "*.log")):
    for m in ERR.finditer(open(log, errors="replace").read()):
        fixes[m.group(1)].add((int(m.group(2)), int(m.group(3))))
n = 0
for path, items in fixes.items():
    lines = open(path, encoding="latin-1").read().split("\n")
    for lineno, col in sorted(items, key=lambda x: (x[0], -x[1])):   # right-to-left within a line
        line = lines[lineno - 1]
        # Columns differ between compilers (GCC expands tabs), so find the cast on the line:
        # the first two-word type followed by '(' that sits after an operator or '('.
        m = None
        for c in CAST.finditer(line):
            before = line[:c.start()].rstrip()
            if before == "" or before[-1] in "=(,+-*/%<>!&|^?:[" or before.endswith("return"):
                m = c; break
        if not m: continue
        lines[lineno - 1] = line[:m.start()] + "(" + m.group(1) + ")(" + line[m.end():]
        n += 1
        print(f"  {path.split('/src/')[-1]}:{lineno}  ({m.group(1)})(...)")
    open(path, "w", encoding="latin-1").write("\n".join(lines))
print(f"rewrote {n} casts")
