"""After a loop variable's declaration was moved above its loop (old MSVC kept loop variables
alive to the end of the function), a later loop at the same level that still declares the same
variable, `for (TYPE var = ...)`, would make a new variable hiding it, so code after that loop
would read the wrong one. Old MSVC had a single variable, so those loops must reuse it:
`for (var = ...)`. Loops nested deeper are separate scopes and are left alone.
  python3 fix_shadowed_loop_vars.py FILE...     (prints what it changes)"""
import re, sys
NOTE = "// PORT: used after the loop (MSVC6 leaked loop variables)"
changed_total = 0
for path in sys.argv[1:]:
    lines = open(path, encoding="latin-1").read().split("\n")
    changed = 0
    for i, line in enumerate(lines):
        if NOTE not in line:
            continue
        m = re.match(r"^(\s*)(\S[\w ]*?)\s+(\w+);", line)
        if not m:
            continue
        indent, var = m.group(1), m.group(3)
        loop = re.compile("^" + re.escape(indent) + r"for\s*\(\s*((?:const\s+|unsigned\s+|signed\s+|long\s+|short\s+)*[\w:<>]+(?:\s*\*)*)\s+" + re.escape(var) + r"(\s*=)")
        for j in range(i + 1, len(lines)):
            if lines[j].startswith("}"):
                break                                   # end of the function
            lm = loop.match(lines[j])
            if lm:
                lines[j] = lines[j][:lm.start(1)] + var + lm.group(2) + lines[j][lm.end(2):]
                changed += 1
                print(f"  {path}:{j + 1}  for ({lm.group(1).strip()} {var} = ...) -> for ({var} = ...)")
    if changed:
        open(path, "w", encoding="latin-1").write("\n".join(lines))
        changed_total += changed
print(f"{changed_total} loops now reuse the function's variable")
