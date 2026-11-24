#!/usr/bin/env bash
set -euo pipefail
here=$(realpath "$(dirname "$0")")
build=$(mktemp -d)
trap 'rm -rf -- "$build"' EXIT
cd "$build"
# Link the console DLL, not MinGW's desktop d3d12 import library.
printf 'LIBRARY d3d12_xs.dll\nEXPORTS\nD3D12CreateDevice\nD3D12SerializeVersionedRootSignature\n' >xs.def
x86_64-w64-mingw32-dlltool -d xs.def -l xs.a
x86_64-w64-mingw32-gcc -O2 -Wall -Werror "$here/queue-fence.c" xs.a -o game_scarlett_master.exe
"$here/../test-launch-nixos.sh" "$build" "$@"
