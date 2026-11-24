#!/usr/bin/env bash
# Build a hardware fixture in a fresh directory; TOOL is a staged Scarlett Proton.
set -euo pipefail
gpu=${1:?usage: tail-timestamp.sh nvidia|radv STAGED-TOOL}
tool=$(realpath "${2:?missing staged tool}")
s=$(realpath "$(dirname "$0")/..")
w=$(mktemp -d "${TMPDIR:-/tmp}/tail-timestamp.XXXXXXXX")
echo "Fixture and logs: $w"
cd "$w"; mkdir game
f=(-Os -Wall -Wextra -Werror -DSCARLETT_DEVICE_BOOTSTRAP -DSCARLETT_STAGED_PLACED
   -DSCARLETT_EXPERIMENTAL_DIRECT_WAIT -DSCARLETT_EXPERIMENTAL_PRIMARY_RELEASE -DTEST_TAIL_TIMESTAMP)
case $gpu in
    nvidia) display=xvfb ;;
    radv) display=sway; f+=(-DTEST_WB_TAIL) ;;
    *) exit 2 ;;
esac
for c in d3d12_device d3d12_vm testq; do
    x86_64-w64-mingw32-gcc "${f[@]}" -c "$s/native/$c.c" -o "$c.o"
done
x86_64-w64-mingw32-g++ "${f[@]}" -std=c++17 -static -DTEST_PRIMARY_BARRIER -DTEST_PRIMARY_CLEAR -DTEST_PRIMARY_RELEASE \
    "$s/native/test-allocation-info.cpp" testq.o d3d12_device.o d3d12_vm.o -ld3d12 -ldxguid -ldxgi -o game/game_scarlett_master.exe
"${DXC:-dxc}" -WX -T cs_6_0 -E main "$s/native/test-primary-barrier.hlsl" -Fo game/test-primary-barrier.dxil
TMPDIR="$w" SCARLETT_TEST_GPU="$gpu" SCARLETT_TEST_DISPLAY="$display" "$s/test-launch-nixos.sh" game "$tool"
grep -hF 'PASS +240 pair:' scarlett-run.*/logs/steam-*.log
