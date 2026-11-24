#!/usr/bin/env bash
set -euo pipefail
s=$(realpath "$(dirname "$0")/..")
tool=$(realpath "${1:?usage: rtx-r32-tile.sh STAGED-TOOL [radv|nvidia|radv-typed|rtx-typed|rtx-r11|radv-r11|rtx-small|radv-small]}")
w=$(mktemp -d "${TMPDIR:-/tmp}/r32-tile.XXXXXXXX")
gpu=${2:-nvidia}; defines=()
source=test-r32-tile; marker="^PASS +c8/+360"
case "$gpu" in
    rtx-small) gpu=nvidia; display=xvfb; source=test-small-tile; marker="^PASS tiled fmt=41" ;;
    radv-small) gpu=radv; display=sway; defines+=(-DTEST_RADV); source=test-small-tile; marker="^PASS tiled fmt=41" ;;
    radv-r11) gpu=radv; display=sway; defines+=(-DTEST_RADV); source=test-r11-place; marker="^PASS R11 original five-mip WC" ;; rtx-r11) gpu=nvidia; display=xvfb; source=test-r11-place; marker="^PASS R11 original five-mip WC" ;; radv-typed) gpu=radv; display=sway; defines+=(-DTEST_RADV); source=test-r32-typed; marker="^PASS RADV 0032 typed aliases" ;; rtx-typed) gpu=nvidia; display=xvfb; source=test-r32-typed; marker="^PASS RTX 0032 typed aliases" ;; nvidia) display=xvfb ;; radv) display=sway; defines+=(-DTEST_RADV) ;; *) exit 2 ;; esac
mkdir "$w/game"
for unit in test-r32-device d3d12_vm queue/queue; do
    x86_64-w64-mingw32-gcc -Os -Wall -Wextra -Werror -DSCARLETT_DEVICE_BOOTSTRAP -DSCARLETT_STAGED_PLACED \
        -c "$s/native/$unit.c" -o "$w/${unit##*/}.o"
done
x86_64-w64-mingw32-g++ -std=c++17 -Os -Wall -Wextra -Werror -static "${defines[@]}" "$s/native/$source.cpp" \
    "$w"/*.o -ld3d12 -ldxguid -o "$w/game/game_scarlett_master.exe"
if [[ $source == test-r11-place || $source == test-r32-typed ]]; then
    "${DXC:-dxc}" "${defines[@]}" -WX -T cs_6_0 -E sample_alias "$s/native/$source.hlsl" -Fo "$w/game/tile.dxil"
    "${DXC:-dxc}" "${defines[@]}" -WX -T cs_6_0 -E write_alias "$s/native/$source.hlsl" -Fo "$w/game/write.dxil"
elif [[ $source == test-r32-tile ]]; then
    "${DXC:-dxc}" -WX -T cs_6_0 -E main "$s/native/$source.hlsl" -Fo "$w/game/tile.dxil"
fi
TMPDIR="$w" SCARLETT_TEST_GPU="$gpu" SCARLETT_TEST_DISPLAY="$display" "$s/test-launch-nixos.sh" "$w/game" "$tool"
grep -h "$marker" "$w"/scarlett-run.*/logs/steam-*.log
[[ $source != test-small-tile ]] || exit 0
mkdir "$w/ranges"
x86_64-w64-mingw32-g++ -std=c++17 -Os -Wall -Wextra -Werror -static "${defines[@]}" "$s/native/test-tiled-ranges.cpp" \
    -ld3d12 -ldxguid -o "$w/ranges/game_scarlett_master.exe"
TMPDIR="$w" SCARLETT_TEST_GPU="$gpu" SCARLETT_TEST_DISPLAY="$display" "$s/test-launch-nixos.sh" "$w/ranges" "$tool" | tee "$w/range-run"
run=$(sed -n 's/^Test output (including prefix): //p' "$w/range-run")
grep '^PASS .*collision until GPU retirement' "$run/logs/steam-999001.log"
