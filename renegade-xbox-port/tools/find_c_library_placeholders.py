#!/usr/bin/env python3
"""
Find functions in nxdk's C library (PDCLib) that are placeholders, and check whether
Renegade's source calls them.

Some functions in nxdk's PDCLib exist only so programs link: their body is assert(0), so
they halt the program the first time they are called (lrintf was one). The linker can't
warn about these, so this tool looks for them in the source.

Usage:
  python3 find_c_library_placeholders.py /workspaces/nxdk /workspaces/RENXBX/Code > placeholders.txt
"""
import os
import re
import sys

nxdk = sys.argv[1] if len(sys.argv) > 1 else "/workspaces/nxdk"
code = sys.argv[2] if len(sys.argv) > 2 else "/workspaces/RENXBX/Code"

PLACEHOLDER = re.compile(r"assert\s*\(\s*(0|false|!\s*\"[^\"]*\"|\"[^\"]*\"\s*&&\s*0)\s*\)"
                         r"|not\s+(yet\s+)?implemented|unimplemented|TODO: implement", re.I)
FUNC_DEF = re.compile(r"^[A-Za-z_][\w\s\*]*?\b([A-Za-z_]\w*)\s*\(([^;{]*)\)\s*$|"
                      r"^[A-Za-z_][\w\s\*]*?\b([A-Za-z_]\w*)\s*\(([^;{]*)\)\s*\{", re.M)
SKIP_NAMES = {"if", "for", "while", "switch", "return", "sizeof"}

# 1. Placeholder functions: definitions whose body contains a placeholder marker.
placeholders = {}   # name -> file
lib_root = os.path.join(nxdk, "lib", "pdclib")
for root, _, files in os.walk(lib_root):
    for f in files:
        if not f.endswith(".c"):
            continue
        path = os.path.join(root, f)
        text = open(path, encoding="latin-1").read()
        if not PLACEHOLDER.search(text):
            continue
        defs = [m for m in FUNC_DEF.finditer(text)]
        for i, m in enumerate(defs):
            name = m.group(1) or m.group(3)
            if not name or name in SKIP_NAMES:
                continue
            start = m.end()
            end = defs[i + 1].start() if i + 1 < len(defs) else len(text)
            body = text[start:end]
            if PLACEHOLDER.search(body):
                placeholders[name] = os.path.relpath(path, nxdk)

if not placeholders:
    print("No placeholder functions found in", lib_root)
    print("(If pdclib is empty, nxdk was cloned without --recursive.)")
    sys.exit(0)

# 2. Which of them Renegade's code calls (outside the PC-only Tools folder).
uses = {name: [] for name in placeholders}
call_re = {name: re.compile(r"(?<![\w.>:])" + re.escape(name) + r"\s*\(") for name in placeholders}
for root, _, files in os.walk(code):
    if "/Tools" in root or "/.git" in root:
        continue
    for f in files:
        if not f.lower().endswith((".cpp", ".c", ".h", ".inl")):
            continue
        path = os.path.join(root, f)
        try:
            text = open(path, encoding="latin-1").read()
        except OSError:
            continue
        for name, rx in call_re.items():
            if rx.search(text):
                uses[name].append(os.path.relpath(path, code))

used = sorted(n for n in placeholders if uses[n])
unused = sorted(n for n in placeholders if not uses[n])

print(f"{len(placeholders)} placeholder functions in nxdk's C library; "
      f"{len(used)} are called somewhere in Renegade's code.\n")
print("=== Called by Renegade (these will halt the game if reached) ===")
for n in used:
    files = uses[n]
    shown = ", ".join(files[:6]) + (f", ... ({len(files)} files)" if len(files) > 6 else "")
    print(f"  {n:16s} [{placeholders[n]}]")
    print(f"      used in: {shown}")
print("\n=== Not called by Renegade ===")
print("  " + " ".join(unused))
