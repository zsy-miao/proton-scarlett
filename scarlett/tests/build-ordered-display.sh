#!/usr/bin/env bash
# Cross-build once; use this directory unchanged for both private display runs.
set -euo pipefail
s=$(realpath "$(dirname "$0")/../native")
out=${1:?usage: build-ordered-display.sh NEW-FIXTURE-DIR [restore-radv|restore-rtx]}
mkdir -- "$out"
obj=$(mktemp -d)
trap 'rm -rf -- "$obj"' EXIT
f=(-Os -Wall -Wextra -Werror -DSCARLETT_DEVICE_BOOTSTRAP -DSCARLETT_STAGED_PLACED
   -DSCARLETT_EXPERIMENTAL_DIRECT_WAIT -DSCARLETT_EXPERIMENTAL_PRIMARY_RELEASE -DTEST_TAIL_TIMESTAMP
   -DSCARLETT_EXPERIMENTAL_TAGGED_BATCH -DTEST_TAGGED_BATCH -DSCARLETT_EXPERIMENTAL_BRIDGE -DTEST_PRESENT_BRIDGE
   -DTEST_ORDERED_BATCH -DTEST_ORDERED_PRESENT)
case ${2:-ordered} in
    restore-radv|restore-rtx)
        f+=(-DSCARLETT_PAIR_SUBMIT -DTEST_REACQUIRE)
        if [[ $2 == restore-rtx ]]; then f+=(-DTEST_SR_REUSE); fi ;;
    ordered)
        if [[ ${TEST_PAIR_SPLIT:-0} == 1 ]]; then f+=(-DTEST_PAIR_SPLIT -DTEST_SAME_BATCH_RELEASE)
        elif [[ ${TEST_SAME_BATCH_RELEASE:-0} == 1 ]]; then f+=(-DTEST_SAME_BATCH_RELEASE); fi ;;
    *) exit 2 ;;
esac
for c in d3d12_device d3d12_vm testq; do
    x86_64-w64-mingw32-gcc "${f[@]}" -c "$s/$c.c" -o "$obj/$c.o"
done
x86_64-w64-mingw32-g++ "${f[@]}" -std=c++17 -static -DTEST_PRIMARY_BARRIER -DTEST_PRIMARY_CLEAR \
    -DTEST_PRIMARY_RELEASE "$s/test-allocation-info.cpp" "$obj"/{testq,d3d12_device,d3d12_vm}.o \
    -ld3d12 -ldxguid -ldxgi -Wl,--no-insert-timestamp -o "$out/game_scarlett_master.exe"
"${DXC:-dxc}" -WX -T cs_6_0 -E main "$s/test-primary-barrier.hlsl" -Fo "$out/test-primary-barrier.dxil"
(cd "$out" && sha256sum game_scarlett_master.exe test-primary-barrier.dxil > SHA256SUMS)
