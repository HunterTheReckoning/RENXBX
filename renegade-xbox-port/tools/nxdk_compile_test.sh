#!/usr/bin/env bash
# nxdk_compile_test.sh -- compile Renegade's foundation libraries with nxdk's real compiler.
#
# Usage (inside WSL, after activating nxdk):
#   eval "$(~/nxdk/bin/activate -s)"
#   ./nxdk_compile_test.sh ~/CnC_Renegade/Code
#
# (An earlier "nowchar" mode tried -fno-wchar; nxdk's C library needs a real wchar_t, so that
#  approach is ruled out. xbox_prelude.h solves the wide-character mismatch instead.)
#
# Writes results-<mode>.txt (upload this) and logs-<mode>/ with every compiler message.
# Compiles to object files only; nothing is linked yet.

set -u

CODE="${1:?usage: $0 <path to Renegade Code folder> [default|nowchar]}"
MODES="${2:-default}"
CODE="$(cd "$CODE" && pwd)" || exit 1
HERE="$(cd "$(dirname "$0")" && pwd)"

if ! command -v nxdk-cxx >/dev/null 2>&1 || [ -z "${NXDK_DIR:-}" ]; then
    echo "nxdk is not active. Run:  eval \"\$(~/nxdk/bin/activate -s)\"  (adjust the path)" >&2
    exit 1
fi
if [ ! -f "$CODE/wwlib/xbox_port.h" ] || [ ! -f "$CODE/xbox/include/new.h" ]; then
    echo "$CODE does not look like the patched source (wwlib/xbox_port.h missing)." >&2
    echo "Apply the patches first: git am patches/*.patch" >&2
    exit 1
fi

# Libraries to test, by Visual C++ project file.
PROJECTS="wwlib/wwlib.dsp WWMath/wwmath.dsp wwdebug/wwdebug.dsp wwsaveload/wwsaveload.dsp
          wwbitpack/wwbitpack.dsp wwtranslatedb/wwtranslatedb.dsp ww3d2/ww3d2.dsp
          wwphys/wwphys.dsp"

# ww3d2 files still waiting on a replacement: the two sound files need the audio system
# (Miles was stripped from the release). animatedsoundmgr_xbox.cpp stands in for the first.
WW3D2_LATER="ww3d2/animatedsoundmgr.cpp ww3d2/soundrobj.cpp"

# Files deliberately left out of the Xbox build (PC-only or unused by the game).
EXCLUDE="wwlib/ddraw.cpp wwlib/dsurface.cpp wwlib/convert.cpp wwlib/LaunchWeb.cpp
         wwlib/WWCOMUtil.cpp wwlib/keyboard.cpp wwlib/msgloop.cpp wwlib/verchk.cpp
         wwlib/Except.cpp wwlib/rcfile.cpp wwlib/regexpr.cpp wwlib/gnu_regex.c
         wwlib/sha.cpp wwlib/mpu.cpp wwlib/xsurface.cpp ww3d2/framgrab.cpp"


# Print "source|include dirs" for every source file, using each project's own /I list
# (as Visual C++ did) so that duplicate headers elsewhere (e.g. Code/Scripts/vector.h)
# are never picked up by mistake.
list_sources() {
    python3 - "$CODE" $PROJECTS <<'PY'
import os, re, sys
code = sys.argv[1]
dirs = {d.lower(): d for d in os.listdir(code) if os.path.isdir(os.path.join(code, d))}
for dsp in sys.argv[2:]:
    proj = os.path.dirname(dsp)
    text = open(os.path.join(code, dsp), encoding="latin-1").read()
    incs = [proj]
    for m in re.finditer(r'/I\s+"?([^"\s]+)"?', text):
        name = m.group(1).replace("\\", "/").rstrip("/").split("/")[-1]
        real = dirs.get(name.lower())
        if real and real not in incs: incs.append(real)
    inc_flags = " ".join("-I" + os.path.join(code, d) for d in incs)
    for line in text.splitlines():
        m = re.match(r'\s*SOURCE=(.+)', line)
        if not m: continue
        src = m.group(1).strip().strip('"').replace("\\", "/")
        if src.startswith("./"): src = src[2:]
        if src.lower().endswith((".cpp", ".c")):
            print(os.path.normpath(os.path.join(proj, src)) + "|" + inc_flags)
PY
}

WWLIB_INCLUDES="-I$CODE/wwlib -I$CODE/wwdebug -I$CODE/WWMath"
W3D_INCLUDES="-I$CODE/ww3d2 -I$CODE/WWAudio -I$CODE/wwdebug -I$CODE/wwlib -I$CODE/WWMath -I$CODE/wwsaveload"

# Flags every Xbox build of the engine needs:
#   Code/xbox/include  stand-ins for old MSVC headers (new.h, memory.h, mmsystem.h)
#   xbox_prelude.h     force-included first: makes the engine's WCHAR the native wchar_t
#   -Wno-c++11-narrowing  MSVC accepted constant tables like random.cpp's seed values,
#                         wrapping them to negative ints; this keeps that behaviour.
PORTFLAGS="-I$CODE/xbox/include -include $CODE/xbox/include/xbox_prelude.h -Wno-c++11-narrowing"
SOURCES="$(list_sources)
wwlib/xbox_port.cpp|$WWLIB_INCLUDES
wwlib/xbox_settings_store.cpp|$WWLIB_INCLUDES
wwlib/registry_xbox.cpp|$WWLIB_INCLUDES
wwlib/except_xbox.cpp|$WWLIB_INCLUDES
ww3d2/animatedsoundmgr_xbox.cpp|$W3D_INCLUDES
xbox/d3d8/xbox_d3d8.cpp|$W3D_INCLUDES
xbox/d3d8/xbox_d3d8_combiners.cpp|$W3D_INCLUDES
xbox/d3d8/xbox_d3dx8.cpp|$W3D_INCLUDES"
EXCLUDE=" $(echo $EXCLUDE) "   # flatten to one space-separated line
WW3D2_LATER=" $(echo $WW3D2_LATER) "

for mode in $MODES; do
    case "$mode" in
        default) MODEFLAGS="" ;;
        nowchar) MODEFLAGS="-Xclang -fno-wchar" ;;
        *) echo "unknown mode: $mode" >&2; exit 1 ;;
    esac

    OUT="$PWD/results-$mode.txt"
    LOGS="$PWD/logs-$mode"
    OBJS="$PWD/obj-$mode"
    rm -rf "$LOGS" "$OBJS"; mkdir -p "$LOGS" "$OBJS"

    {
        echo "nxdk compile test, mode: $mode"
        echo "clang: $(clang --version | head -n 1)"
        echo "nxdk:  $NXDK_DIR"
        echo "flags: $PORTFLAGS $MODEFLAGS"
        echo
    } > "$OUT"

    ok=0; fail=0; skipped=0; later=0
    while IFS='|' read -r rel INCLUDES; do
        [ -n "$rel" ] || continue
        rel_lc="${rel,,}"   # project files and disk disagree on case (FramGrab.cpp), so compare lower-case
        if [[ "${WW3D2_LATER,,}" == *" $rel_lc "* ]]; then
            echo "LATER $rel" >> "$OUT"; later=$((later + 1)); continue
        fi
        if [[ "${EXCLUDE,,}" == *" $rel_lc "* ]]; then
            echo "SKIP $rel" >> "$OUT"; skipped=$((skipped + 1)); continue
        fi
        src="$CODE/$rel"
        log="$LOGS/$(echo "$rel" | tr '/' '_').log"
        obj="$OBJS/$(echo "$rel" | tr '/' '_').obj"
        if [ ! -f "$src" ]; then
            echo "MISSING $rel" >> "$OUT"; fail=$((fail + 1)); continue
        fi
        case "$src" in
            *.c) compiler=nxdk-cc ;;
            *)   compiler=nxdk-cxx ;;
        esac
        if $compiler -c -w $PORTFLAGS $MODEFLAGS $INCLUDES "$src" -o "$obj" > "$log" 2>&1; then
            echo "OK   $rel" >> "$OUT"; ok=$((ok + 1))
        else
            first="$(grep -m1 -E 'error' "$log" | sed -E "s|$CODE/||" | cut -c1-160)"
            echo "FAIL $rel :: $first" >> "$OUT"; fail=$((fail + 1))
        fi
        printf "\r[%s] ok %d  fail %d  skipped %d" "$mode" "$ok" "$fail" "$skipped" >&2
    done <<< "$SOURCES"
    echo >&2

    {
        echo
        echo "SUMMARY ($mode): $ok compiled, $fail failed, $skipped skipped, $later later (waiting on the audio replacement)"
        echo
        echo "Most common errors:"
        grep -h -E 'error:' "$LOGS"/*.log 2>/dev/null \
            | sed -E "s|^.*error: ||; s/'[^']*'/'X'/g" | sort | uniq -c | sort -rn | head -25
    } >> "$OUT"
    echo "Wrote $OUT"
done
