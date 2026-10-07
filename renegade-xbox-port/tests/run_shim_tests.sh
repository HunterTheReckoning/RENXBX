#!/usr/bin/env bash
# Unit tests for the port layer, built natively (not for Xbox) with stand-in Windows headers:
#   xbox_port.cpp          CRT/Win32 replacements (paths, strings, wide printf, threads, ...)
#   xbox_settings_store    the key/value file that replaces the Windows registry
#   d3dx8.h                the D3DX math the 3D engine uses (matrix product, transforms, FVF sizes)
#   xbox/d3d8              the Direct3D layer, against stand-ins for pbkit and the kernel that
#                          record the GPU commands it sends; the texture swizzle and the
#                          texture-stage to register-combiner translation
# Usage: ./tests/run_shim_tests.sh ~/CnC_Renegade/Code
set -e
CODE="${1:?usage: $0 <path to patched Code folder>}"
HERE="$(cd "$(dirname "$0")/shim" && pwd)"
TMP="$(mktemp -d)"
cp "$CODE/wwlib/xbox_port.h" "$CODE/wwlib/xbox_port.cpp" \
   "$CODE/wwlib/xbox_settings_store.h" "$CODE/wwlib/xbox_settings_store.cpp" "$TMP/"
cd "$TMP"
for t in test_basic test_wprintf test_system; do
    g++ -DNXDK -fshort-wchar -I"$HERE" -I. -w "$HERE/$t.cpp" xbox_port.cpp -o "$t"
    ./"$t"
done
g++ -std=gnu++17 -I. "$HERE/test_store.cpp" xbox_settings_store.cpp -o test_store
./test_store
g++ -std=gnu++17 -I"$HERE/../d3dx" -I"$CODE/xbox/include" "$HERE/../d3dx/test_d3dx_math.cpp" -o test_d3dx
./test_d3dx
g++ -std=gnu++17 -w -I"$HERE/../d3dlayer/stub" -I"$CODE/xbox/include" -I"$CODE/xbox/d3d8" \
    "$HERE/../d3dlayer/test_layer.cpp" "$CODE/xbox/d3d8/xbox_d3d8.cpp" "$CODE/xbox/d3d8/xbox_d3dx8.cpp" \
    "$CODE/xbox/d3d8/xbox_d3d8_combiners.cpp" -o test_d3dlayer
./test_d3dlayer
g++ -std=gnu++17 -w -I"$HERE/../d3dlayer/stub" -I"$CODE/xbox/include" -I"$CODE/xbox/d3d8" \
    "$HERE/../d3dlayer/test_combiners.cpp" "$CODE/xbox/d3d8/xbox_d3d8_combiners.cpp" -o test_combiners
./test_combiners
rm -rf "$TMP"
