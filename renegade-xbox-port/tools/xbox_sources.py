#!/usr/bin/env python3
"""Print the engine sources the Xbox build compiles, one absolute path per line.

  python3 xbox_sources.py <Code folder> foundation      foundation libraries only
  python3 xbox_sources.py <Code folder> engine          foundation + ww3d2 + wwphys + the Direct3D layer

Sources come from EA's Visual C++ project files, minus the files the Xbox build leaves out or
replaces, plus the port's own files. Kept in step with tools/nxdk_compile_test.sh."""
import os, re, sys

code = os.path.abspath(sys.argv[1])
which = sys.argv[2] if len(sys.argv) > 2 else "foundation"

FOUNDATION = ["wwlib/wwlib.dsp", "WWMath/wwmath.dsp", "wwdebug/wwdebug.dsp", "wwsaveload/wwsaveload.dsp",
              "wwbitpack/wwbitpack.dsp", "wwtranslatedb/wwtranslatedb.dsp"]
ENGINE = FOUNDATION + ["ww3d2/ww3d2.dsp", "wwphys/wwphys.dsp"]

# PC-only or unused by the game.
EXCLUDE = {"wwlib/ddraw.cpp", "wwlib/dsurface.cpp", "wwlib/convert.cpp", "wwlib/launchweb.cpp",
           "wwlib/wwcomutil.cpp", "wwlib/keyboard.cpp", "wwlib/msgloop.cpp", "wwlib/verchk.cpp",
           "wwlib/except.cpp", "wwlib/rcfile.cpp", "wwlib/regexpr.cpp", "wwlib/gnu_regex.c",
           "wwlib/sha.cpp", "wwlib/mpu.cpp", "wwlib/xsurface.cpp", "ww3d2/framgrab.cpp",
           # waiting on the audio replacement (animatedsoundmgr_xbox.cpp stands in for the first)
           "ww3d2/animatedsoundmgr.cpp", "ww3d2/soundrobj.cpp"}

PORT_FOUNDATION = ["wwlib/xbox_port.cpp", "wwlib/xbox_settings_store.cpp", "wwlib/registry_xbox.cpp",
                   "wwlib/except_xbox.cpp"]
PORT_ENGINE = PORT_FOUNDATION + ["ww3d2/animatedsoundmgr_xbox.cpp", "xbox/d3d8/xbox_d3d8.cpp", "xbox/d3d8/xbox_d3d8_combiners.cpp",
                                 "xbox/d3d8/xbox_d3dx8.cpp"]

def project_sources(dsp):
    proj = os.path.dirname(dsp)
    for line in open(os.path.join(code, dsp), encoding="latin-1"):
        m = re.match(r'\s*SOURCE=(.+)', line)
        if not m:
            continue
        src = m.group(1).strip().strip('"').replace("\\", "/")
        if src.startswith("./"):
            src = src[2:]
        rel = os.path.normpath(os.path.join(proj, src))
        if rel.lower().endswith(".cpp") and rel.lower() not in EXCLUDE:
            yield rel

projects, extra = (ENGINE, PORT_ENGINE) if which == "engine" else (FOUNDATION, PORT_FOUNDATION)
for dsp in projects:
    for rel in project_sources(dsp):
        print(os.path.join(code, rel))
for rel in extra:
    print(os.path.join(code, rel))
