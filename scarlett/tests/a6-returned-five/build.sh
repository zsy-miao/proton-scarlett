#!/usr/bin/env bash
# Cross-build and hash the two providers and both revisions of the fixture.
set -euo pipefail
[[ $# == 1 ]] || { echo "usage: $0 QUALIFIED_47002876_STAGE" >&2; exit 2; }
here=$(cd "$(dirname "$0")" && pwd)
base=$(realpath "$1")
(cd "$base" && sha256sum --strict -c "$here/preimages.sha256")
out=$(mktemp -d "${TMPDIR:-/tmp}/a6-returned-five.XXXXXXXX")
echo "Build directory: $out"
cd "$out"
n=scarlett/native
mkdir -p scarlett/tests/clear-fallback inputs obj control provider r1 fixture
cp -RL "$base/$n" "$n"
cp "$base"/scarlett/tests/clear-fallback/*.hpp scarlett/tests/clear-fallback/
cp "$base"/inputs/{d3d12_vm.o,im96_host_dxil.h} inputs/
cp "$base/fixture/xmem.dll" fixture/
apply(){ patch --batch --forward --fuzz=0 -p1 < "$here/$1.patch"; }
cc=x86_64-w64-mingw32-gcc
flags=(-Os -Wall -Wextra -Werror -DSCARLETT_DEVICE_BOOTSTRAP -DSCARLETT_STAGED_PLACED
       -DSCARLETT_EXPERIMENTAL_DIRECT_WAIT -DSCARLETT_EXPERIMENTAL_PRIMARY_RELEASE
       -DSCARLETT_EXPERIMENTAL_TAGGED_BATCH -DSCARLETT_EXPERIMENTAL_BRIDGE -DSCARLETT_PAIR_SUBMIT
       -DXS_IM96_FACTORY -DXS_RTX_RETURNED_TABLE -DXS_RTX_ATTRIBUTED_FIVE -Iinputs)
apply production
for dir in control provider; do
    if [[ $dir == provider ]]; then flags+=(-DXS_AMD_RETURNED_FIVE); fi
    "$cc" "${flags[@]}" -c "$n/d3d12_device.c" -o obj/device.o
    "$cc" "${flags[@]}" -DXS_TRACE_FRESH_PUBLICATION -c "$n/queue/queue.c" -o obj/queue.o
    "$cc" -shared -s -Wl,--no-insert-timestamp,--image-base,0x24d560000 obj/{device,queue}.o \
        inputs/d3d12_vm.o "$n/d3d12_xs.def" -ld3d12 -ldxgi -o "$dir/d3d12_xs.dll"
done
flags+=(-DTEST_A6_FIVE -DTEST_RESTORED_CLEAR -DTEST_CARRIED_60 -DTEST_NATIVE_FIRST180 -DTEST_FIRST180
        -DTEST_NATIVE_TRIP -DTEST_RTX_TRIP -DTEST_LATE_COLOUR -DTEST_INLINE_B27 -DTEST_SECOND_VB
        -DTEST_DISTINCT_PRIMARY -DTEST_DEFER_RELEASE -DTEST_RETURNED_SETTERS -DTEST_FRESH190
        -DTEST_MEASURED_B27 -DTEST_CARRIED_REPLACEMENT -DTEST_CARRIED_TARGET -DTEST_SPLIT6_TARGET)
apply fixture
"$cc" "${flags[@]}" -c "$n/test-clear-device.c" -o obj/test-clear-device.o
for dir in r1 fixture; do
    if [[ $dir == fixture ]]; then apply fixture-r2; sha256sum --strict -c "$here/sources.sha256"; fi
    "$cc" "${flags[@]}" -c "$n/queue/test-clear4-bind.c" -o obj/test-clear4-bind.o
    x86_64-w64-mingw32-g++ -std=c++17 -static -Wl,--no-insert-timestamp "${flags[@]}" \
        "$n/test-clear-pipeline.cpp" obj/{test-clear4-bind,test-clear-device}.o inputs/d3d12_vm.o \
        fixture/xmem.dll -lbcrypt -ld3d12 -ldxgi -ldxguid -o "$dir/game_scarlett_master.exe"
done
sha256sum --strict -c "$here/outputs.sha256"
