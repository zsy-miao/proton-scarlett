#!/usr/bin/env bash
set -euo pipefail
s=$(realpath "$(dirname "$0")/../native")
w=$(mktemp -d)
trap 'rm -rf -- "$w"' EXIT
mkdir "$w/game"
for src in d3d12_device d3d12_vm queue/queue; do
    x86_64-w64-mingw32-gcc -Os -Wall -Wextra -Werror -DSCARLETT_DEVICE_BOOTSTRAP -DSCARLETT_STAGED_PLACED \
        -c "$s/$src.c" -o "$w/${src##*/}.o"
done
x86_64-w64-mingw32-g++ -std=c++17 -Os -static -Wall -Wextra -Werror "$s/test-w-clamp.cpp" \
    "$w"/*.o -ld3d12 -ldxguid -o "$w/game/game_scarlett_master.exe"
"${DXC:-dxc}" -WX -T cs_6_0 -E main "$s/test-w-clamp.hlsl" -Fo "$w/game/test-w-clamp.dxil"
"$s/../test-launch-nixos.sh" "$w/game" "$@" | tee "$w/result"
run=$(sed -n 's/^Test output (including prefix): //p' "$w/result")
grep '^PASS +1e8/+c0 slots 2/3:' "$run/logs/steam-999001.log"
