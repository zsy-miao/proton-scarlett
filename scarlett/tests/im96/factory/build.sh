#!/usr/bin/env bash
set -euo pipefail
umask 077
s=$(realpath "$(dirname "$0")")
n=$(realpath "$s/../../../native")
inputs=$(realpath "${1:?private F96 input directory required}")
out=$(realpath -m "${2:?new output directory required}")
mkdir "$out"
mkdir "$out/game" "$out/obj"
cd "$out/obj"
python3 -IB "$s/../factory-shaders.py" "$PWD/im96_host_dxil.h"
flags=(-Os -Wall -Wextra -Werror -DSCARLETT_DEVICE_BOOTSTRAP -DSCARLETT_STAGED_PLACED
       -DSCARLETT_EXPERIMENTAL_TAGGED_BATCH -DSCARLETT_EXPERIMENTAL_PRIMARY_RELEASE
       -DSCARLETT_EXPERIMENTAL_DIRECT_WAIT -DSCARLETT_EXPERIMENTAL_BRIDGE -DSCARLETT_PAIR_SUBMIT
       -DXS_IM96_FACTORY -I. -I"$n" -I"$n/queue")
for src in "$n/d3d12_device.c" "$n/d3d12_vm.c" "$n/queue/queue.c" "$s/list.c"; do
    x86_64-w64-mingw32-gcc "${flags[@]}" -c "$src" -o "$(basename "$src" .c).o"
done
x86_64-w64-mingw32-gcc -shared -s -Wl,--no-insert-timestamp,--image-base,0x24d560000 \
    d3d12_device.o queue.o d3d12_vm.o "$n/d3d12_xs.def" -ld3d12 -ldxgi -o ../d3d12_xs.dll
x86_64-w64-mingw32-g++ "${flags[@]}" -std=c++17 -static -s -Wl,--no-insert-timestamp \
    "$s/main.cpp" d3d12_device.o list.o d3d12_vm.o -ld3d12 -ldxgi -o ../game/game_scarlett_master.exe
cd ../game
while read -r digest name; do cp -- "$inputs/$name" "$name"; done < "$s/inputs.sha256"
sha256sum -c "$s/inputs.sha256"
