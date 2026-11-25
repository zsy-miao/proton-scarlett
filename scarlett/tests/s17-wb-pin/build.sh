#!/usr/bin/env bash
set -euo pipefail
[[ $# == 1 ]] || { echo "usage: $0 BF6D6155_COMBINED_OUTPUT" >&2; exit 2; }
here=$(cd "$(dirname "$0")" && pwd)
base=$(realpath "$1")
(cd "$base" && sha256sum --strict -c "$here/preimages.sha256")
out=$(mktemp -d "${TMPDIR:-/tmp}/s17-wb-pin.XXXXXXXX")
echo "Build directory: $out"
cd "$out"
n=scarlett/native
mkdir -p scarlett/tests/clear-fallback inputs obj off on fixture
cp -RL "$base/$n" "$n"
cp "$base"/scarlett/tests/clear-fallback/*.hpp scarlett/tests/clear-fallback/
cp "$base"/inputs/{d3d12_vm.o,im96_host_dxil.h} inputs/
cp "$base"/combined-amd-r2/{colour-PS.bin,colour-stream.bin,colour-VS.bin,emitter.bin,ps.bin,stream.bin,vs.bin,xmem.dll} fixture/
patch --batch --forward --fuzz=0 -p1 < "$here/s17-wb.patch"
cc=x86_64-w64-mingw32-gcc
f=(-Os -Wall -Wextra -Werror -DSCARLETT_DEVICE_BOOTSTRAP -DSCARLETT_STAGED_PLACED
   -DSCARLETT_EXPERIMENTAL_DIRECT_WAIT -DSCARLETT_EXPERIMENTAL_PRIMARY_RELEASE
   -DSCARLETT_EXPERIMENTAL_TAGGED_BATCH -DSCARLETT_EXPERIMENTAL_BRIDGE -DSCARLETT_PAIR_SUBMIT
   -DXS_IM96_FACTORY -DXS_RTX_RETURNED_TABLE -DXS_RTX_ATTRIBUTED_FIVE -Iinputs
   -DXS_TRACE_FRESH_PUBLICATION -DXS_TRACE_S14_SUCCESSOR -DXS_RTX_S14_JOB -DXS_RTX_S14_QUEUED4 -DXS_AMD_RETURNED_FIVE)
"$cc" "${f[@]}" -c "$n/d3d12_device.c" -o obj/device.o
for dir in off on; do
    if [[ $dir == on ]]; then f+=(-DXS_AMD_S17_WB); fi
    "$cc" "${f[@]}" -c "$n/queue/queue.c" -o "$dir/queue.o"
    "$cc" -shared -s -Wl,--no-insert-timestamp,--image-base,0x24d560000 obj/device.o "$dir/queue.o" inputs/d3d12_vm.o \
        "$n/d3d12_xs.def" -ld3d12 -ldxgi -o "$dir/d3d12_xs.dll"
done
cmp off/d3d12_xs.dll "$base/combined/d3d12_xs.dll"
f+=(-DTEST_DEFER_RELEASE -DTEST_DISTINCT_PRIMARY -DTEST_SECOND_VB -DTEST_LATE_COLOUR
    -DTEST_INLINE_B27 -DTEST_PAIR_FIXTURE -DTEST_AMD_S17_WB)
for s in queue/test-clear4-bind test-clear-device; do
    "$cc" "${f[@]}" -c "$n/$s.c" -o "obj/${s##*/}.o"
done
x86_64-w64-mingw32-g++ -std=c++17 -static -Wl,--no-insert-timestamp "${f[@]}" "$n/test-clear-pipeline.cpp" \
    obj/{test-clear4-bind,test-clear-device}.o inputs/d3d12_vm.o fixture/xmem.dll -lbcrypt -ld3d12 -ldxgi -ldxguid -o fixture/game_scarlett_master.exe
sha256sum --strict -c "$here/outputs.sha256"
