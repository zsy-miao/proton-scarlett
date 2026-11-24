#!/usr/bin/env bash
# Verify that the private RADV recorder sees both DXGI presents in order.
set -euo pipefail
here=$(dirname "$(realpath "$0")")
w=$(mktemp -d "${TMPDIR:-/tmp}/scarlett-radv-present.XXXXXXXX")
mkdir "$w/game"
x86_64-w64-mingw32-g++ -O2 -static "$here/present-capture.cpp" -ld3d12 -ldxgi -ldxguid -luuid \
    -o "$w/game/game_scarlett_master.exe"
SCARLETT_TEST_GPU=radv SCARLETT_TEST_DISPLAY=sway SCARLETT_RECORD=1 TMPDIR="$w" \
    "$here/../test-launch-nixos.sh" "$w/game" "$@"
grep -aF 'PASS synthetic DXGI: presented red then green' "$w"/scarlett-run.*/logs/steam-999001.log
ffmpeg -nostdin -v error -i "$w"/scarlett-run.*/display.mp4 \
    -vf 'crop=2:2:400:300,scale=1:1,format=rgb24' -f rawvideo - | python3 -c '
import sys
red = green_after_red = False
for r,g,b in zip(*[iter(sys.stdin.buffer.read())]*3):
    red |= r > 245 and g < 10 and b < 10
    green_after_red |= red and g > 245 and r < 10 and b < 10
if not green_after_red:
    sys.exit("FAIL: expected red then green at the center of display.mp4")
print("PASS: decoded red then green from the private RADV display")'
