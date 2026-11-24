#!/usr/bin/env bash
# Build project-owned x64 PE libraries with MinGW, without game files.
set -euo pipefail
here=$(realpath "$(dirname "$0")")
out=${1:?usage: build-runtime.sh NEW-OUTPUT-DIRECTORY}
mkdir "$out"
x86_64-w64-mingw32-gcc -shared -Os -s -Wl,--no-insert-timestamp "$here/native/d3d12_xs.def" -o "$out/d3d12_xs.dll"
x86_64-w64-mingw32-gcc -shared -Os -s -Wall -Wextra -Werror -Wl,--no-insert-timestamp "$here/native/xmem/xmem.c" -o "$out/xmem.dll"
