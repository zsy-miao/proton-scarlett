#!/usr/bin/env bash
# Build project-owned x64 PE libraries with MinGW, without game files.
set -euo pipefail
here=$(realpath "$(dirname "$0")")
out=${1:?usage: build-runtime.sh NEW-OUTPUT-DIRECTORY}
mkdir "$out"
x86_64-w64-mingw32-gcc -shared -Os -s -Wall -Wextra -Werror -Wl,--no-insert-timestamp \
    "$here/native/d3d12_device.c" "$here/native/d3d12_xs.def" -ld3d12 -o "$out/d3d12_xs.dll"
build_dll() {
    local source=$1 output=$2
    shift 2
    x86_64-w64-mingw32-gcc -std=c11 -shared -Os -s -Wall -Wextra -Werror \
        -Wl,--no-insert-timestamp "$here/native/$source/$source.c" "$@" -o "$out/$output.dll"
}
build_dll xmem xmem
build_dll chad chad
build_dll xapu xapu
build_dll acphal AcpHal
build_dll xg xg_xs
build_dll xcrdapi XCRDAPI -luuid
