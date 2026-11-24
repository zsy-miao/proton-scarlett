#!/usr/bin/env bash
set -euo pipefail
s=$(realpath "$(dirname "$0")/..")
tool=$(realpath "${1:?usage: rtx-r32-tile.sh STAGED-TOOL [radv|nvidia]}")
w=$(mktemp -d "${TMPDIR:-/tmp}/r32-tile.XXXXXXXX")
gpu=${2:-nvidia}; defines=()
case "$gpu" in nvidia) display=xvfb ;; radv) display=sway; defines+=(-DTEST_RADV) ;; *) exit 2 ;; esac
mkdir "$w/game"
for unit in test-r32-device d3d12_vm queue/queue; do
    x86_64-w64-mingw32-gcc -Os -Wall -Wextra -Werror -DSCARLETT_DEVICE_BOOTSTRAP -DSCARLETT_STAGED_PLACED \
        -c "$s/native/$unit.c" -o "$w/${unit##*/}.o"
done
x86_64-w64-mingw32-g++ -std=c++17 -Os -Wall -Wextra -Werror -static "${defines[@]}" "$s/native/test-r32-tile.cpp" \
    "$w"/*.o -ld3d12 -ldxguid -o "$w/game/game_scarlett_master.exe"
"${DXC:-dxc}" -WX -T cs_6_0 -E main "$s/native/test-r32-tile.hlsl" -Fo "$w/game/tile.dxil"
TMPDIR="$w" SCARLETT_TEST_GPU="$gpu" SCARLETT_TEST_DISPLAY="$display" "$s/test-launch-nixos.sh" "$w/game" "$tool"
grep -h '^PASS +c8/+360' "$w"/scarlett-run.*/logs/steam-*.log
mkdir "$w/ranges"
x86_64-w64-mingw32-g++ -std=c++17 -Os -Wall -Wextra -Werror -static "${defines[@]}" "$s/native/test-tiled-ranges.cpp" \
    -ld3d12 -ldxguid -o "$w/ranges/game_scarlett_master.exe"
TMPDIR="$w" SCARLETT_TEST_GPU="$gpu" SCARLETT_TEST_DISPLAY="$display" "$s/test-launch-nixos.sh" "$w/ranges" "$tool" | tee "$w/range-run"
run=$(sed -n 's/^Test output (including prefix): //p' "$w/range-run")
grep '^PASS .*collision until GPU retirement' "$run/logs/steam-999001.log"
