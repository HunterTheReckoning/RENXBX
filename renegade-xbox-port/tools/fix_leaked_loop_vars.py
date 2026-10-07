"""Fix variables declared in a for() and used after the loop ends (MSVC6 leaked them).
For each "'v' was not declared" / "use of undeclared identifier 'v'" error, find the nearest
earlier line of the form   for (TYPE v = ...;   (single declarator) and move the declaration
just above the loop:   TYPE v;  for (v = ...;   Leaves anything more complex for a person."""
import re, sys, glob, os, collections
ERR = re.compile(r"^(/[^:]+):(\d+):\d+: error: (?:'(\w+)' was not declared in this scope|use of undeclared identifier '(\w+)')", re.M)
NOTE = "// PORT: used after the loop (MSVC6 leaked loop variables)"
fixes = collections.defaultdict(set)
for log in glob.glob(os.path.join(sys.argv[1], "*.log")):
    for m in ERR.finditer(open(log, errors="replace").read()):
        fixes[m.group(1)].add((int(m.group(2)), m.group(3) or m.group(4)))
n = 0
for path, items in fixes.items():
    lines = open(path, encoding="latin-1").read().split("\n")
    done = set()
    for lineno, var in sorted(items, reverse=True):
        decl = re.compile(r"^(\s*)for\s*\(\s*((?:const\s+|unsigned\s+|signed\s+|long\s+|short\s+)*[\w:<>]+(?:\s*\*)*)\s+"
                          + re.escape(var) + r"(\s*=[^;,]*;)")
        for j in range(lineno - 2, max(-1, lineno - 300), -1):
            m = decl.match(lines[j])
            if m:
                if (j, var) in done: break
                indent, vtype, rest = m.group(1), m.group(2).strip(), m.group(3)
                # Already moved one of these above an earlier loop in this function, at this level?
                # Then reuse it, as the original did (old MSVC kept one variable for the function).
                reuse = False
                for k in range(j - 1, -1, -1):
                    if lines[k].startswith("}"): break                    # left the function
                    if NOTE in lines[k] and re.match(re.escape(indent) + r"\S[\w ]*\s" + re.escape(var) + r";", lines[k]):
                        reuse = True; break
                stripped = lines[j][:m.start(2)] + var + rest + lines[j][m.end(3):]
                lines[j] = stripped if reuse else f"{indent}{vtype} {var};\t{NOTE}\n" + stripped
                done.add((j, var)); n += 1
                print(f"  {path.split('/src/')[-1]}:{j+1}  {vtype} {var} moved above the loop")
                break
    open(path, "w", encoding="latin-1").write("\n".join(lines))
print(f"moved {n} declarations")
