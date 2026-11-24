#!/usr/bin/env bash
# Cross-build the RAW32 readback fixture against the XMem DLL in the tested tool.
set -euo pipefail
[[ $# == 2 ]] || { echo "usage: $0 MATCHED-XMEM-DLL NEW-FIXTURE-DIR" >&2; exit 2; }
n=$(realpath "$(dirname "$0")/../native")
xmem=$(realpath "$1")
mkdir -- "$2"
cd -- "$2"
obj=$(mktemp -d)
trap 'rm -rf -- "$obj"' EXIT
flags=(-Os -Wall -Wextra -Werror -DSCARLETT_DEVICE_BOOTSTRAP -DSCARLETT_STAGED_PLACED
    -DSCARLETT_EXPERIMENTAL_TAGGED_BATCH -DSCARLETT_EXPERIMENTAL_PRIMARY_RELEASE
    -DSCARLETT_EXPERIMENTAL_DIRECT_WAIT -DSCARLETT_EXPERIMENTAL_BRIDGE)
for f in d3d12_device queue/queue d3d12_vm; do
    x86_64-w64-mingw32-gcc "${flags[@]}" -c "$n/$f.c" -o "$obj/${f##*/}.o"
done
x86_64-w64-mingw32-g++ -Os -Wall -Wextra -Werror -std=c++17 -static -Wl,--no-insert-timestamp \
    -DTEST_RAW32 "$n/test-raw38.cpp" "$obj"/{d3d12_device,queue,d3d12_vm}.o "$xmem" \
    -ld3d12 -ldxgi -ldxguid -o game_scarlett_master.exe
"${DXC:-dxc}" -T cs_6_0 -E main "$n/test-raw38.hlsl" -Fo raw38.dxil
