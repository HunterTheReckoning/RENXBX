"""Rewrite #include "name" so the case matches the real file on disk."""
import os, re, sys
ROOT = sys.argv[1]
by_lower = {}
for root, _, files in os.walk(ROOT):
    if "/.git" in root: continue
    for f in files: by_lower.setdefault(f.lower(), set()).add(f)
inc = re.compile(r'^(\s*#\s*include\s*["<])([^">]+)([">])', re.M)
changed = 0; files_changed = 0; ambiguous = set()
for root, _, files in os.walk(ROOT):
    if "/.git" in root: continue
    for f in files:
        if not f.lower().endswith((".cpp", ".c", ".h", ".hpp", ".inl")): continue
        path = os.path.join(root, f)
        text = open(path, encoding="latin-1").read()
        def fix(m):
            global changed
            target = m.group(2).replace("\\", "/")
            base = os.path.basename(target)
            real = by_lower.get(base.lower())
            if not real or base in real: return m.group(0)
            if len(real) > 1: ambiguous.add(base); return m.group(0)
            new = target[: len(target) - len(base)] + next(iter(real))
            changed += 1
            return m.group(1) + new + m.group(3)
        new_text = inc.sub(fix, text)
        if new_text != text:
            open(path, "w", encoding="latin-1").write(new_text); files_changed += 1
print(f"rewrote {changed} includes in {files_changed} files; ambiguous: {sorted(ambiguous)}")
