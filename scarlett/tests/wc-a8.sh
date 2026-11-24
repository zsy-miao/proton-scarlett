#!/usr/bin/env bash
set -euo pipefail
s=$(realpath "$(dirname "$0")/../native")
gpu=${1:?usage: wc-a8.sh radv|nvidia STAGED-TOOL}
tool=$(realpath "${2:?missing staged tool}")
target=${3:-a8}
case "$target" in a8) pass='PASS A8 +428:' ;; bgra) pass='PASS BGRA8 slot 1 ' ;; *) exit 2 ;; esac
defines=()
case "$gpu" in
    radv) display=sway; defines+=(-DTEST_RADV) ;;
    nvidia) display=xvfb ;;
    *) exit 2 ;;
esac
w=$(mktemp -d)
trap 'rm -rf -- "$w"' EXIT
mkdir "$w/game"
for src in d3d12_device d3d12_vm queue/queue; do
    x86_64-w64-mingw32-gcc -Os -Wall -Wextra -Werror -DSCARLETT_DEVICE_BOOTSTRAP -DSCARLETT_STAGED_PLACED \
        -c "$s/$src.c" -o "$w/${src##*/}.o"
done
x86_64-w64-mingw32-g++ -std=c++17 -Os -static -Wall -Wextra -Werror "${defines[@]}" "$s/test-wc-$target.cpp" \
    "$w"/*.o -ld3d12 -ldxguid -o "$w/game/game_scarlett_master.exe"
"${DXC:-dxc}" -WX -T cs_6_0 -E main "$s/test-wc-$target.hlsl" -Fo "$w/game/$target.dxil"
SCARLETT_TEST_GPU="$gpu" SCARLETT_TEST_DISPLAY="$display" "$s/../test-launch-nixos.sh" "$w/game" "$tool" | tee "$w/result"
run=$(sed -n 's/^Test output (including prefix): //p' "$w/result")
grep -F "$pass" "$run/logs/steam-999001.log"
