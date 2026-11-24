#!/usr/bin/env bash
set -euo pipefail
s=$(realpath "$(dirname "$0")/../native"); gpu=${1:?usage: wc-float1440.sh radv|nvidia STAGED-TOOL [r8uint|depth1440|row|small]}; tool=$(realpath "${2:?missing staged tool}")
case "$gpu" in radv) src=test-wc-float1440; display=sway ;; nvidia) src=test-rtx-fp1440; display=xvfb ;; *) exit 2 ;; esac
unit_device=d3d12_device
case ${3:-float} in float) ;; r8uint) src=test-wc-r8uint ;; depth1440) src=test-wc-depth1440 ;; small) src=test-small-rt ;; row) src=test-wc-row; unit_device=test-r32-device ;; *) exit 2 ;; esac
w=$(mktemp -d); trap 'rm -rf -- "$w"' EXIT; mkdir "$w/game"
for unit in "$unit_device" d3d12_vm queue/queue; do
    x86_64-w64-mingw32-gcc -Os -Wall -Wextra -Werror -DSCARLETT_DEVICE_BOOTSTRAP -DSCARLETT_STAGED_PLACED -c "$s/$unit.c" -o "$w/${unit##*/}.o"
done
x86_64-w64-mingw32-g++ -std=c++17 -Os -static -Wall -Wextra -Werror "$s/$src.cpp" "$w"/*.o -ld3d12 -ldxguid -o "$w/game/game_scarlett_master.exe"
if [[ $src == test-small-rt ]]; then shader=small
elif [[ $src == test-wc-row ]]; then
    "${DXC:-dxc}" -WX -T cs_6_0 -E main -DWRITE "$s/$src.hlsl" -Fo "$w/game/write.dxil"; shader=read
elif [[ $src == test-wc-depth1440 ]]; then
    "${DXC:-dxc}" -WX -T vs_6_0 -E vs "$s/test-msaa-depth.hlsl" -Fo "$w/game/ro-vs.dxil"
    "${DXC:-dxc}" -WX -T ps_6_0 -E ps "$s/test-ro-dsv.hlsl" -Fo "$w/game/ro-ps.dxil"
    shader=depth1024; src=test-depth1024
elif [[ $src == test-wc-r8uint ]]; then shader=r8uint
elif [[ $gpu == nvidia ]]; then
    "${DXC:-dxc}" -WX -T cs_6_0 -E main "$s/$src-uav.hlsl" -Fo "$w/game/fp-uav.dxil"; shader=fp
else shader=float1440; fi
"${DXC:-dxc}" -WX -T cs_6_0 -E main "$s/$src.hlsl" -Fo "$w/game/$shader.dxil"
SCARLETT_TEST_GPU="$gpu" SCARLETT_TEST_DISPLAY="$display" "$s/../test-launch-nixos.sh" "$w/game" "$tool" | tee "$w/result"
run=$(sed -n 's/^Test output (including prefix): //p' "$w/result"); grep '^PASS ' "$run/logs/steam-999001.log"
