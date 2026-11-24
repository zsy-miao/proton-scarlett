#!/usr/bin/env bash
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd); src=$here/../../native
exe=$(realpath "${1:?pinned title exe}"); cap=$(realpath "${2:?stream.bin vs.bin ps.bin capture directory}"); tool=$(realpath "${3:?test tool directory}")
w=$(mktemp -d "${TMPDIR:-/tmp}/clear-fallback.XXXXXXXX"); cd "$w"; mkdir game radv nvidia; echo "Results: $w"
python3 "$here/prepare.py" "$exe" game/emitter.bin
cp "$cap"/{stream,vs,ps}.bin game
for f in "$src/test-clear-device.c" "$src/queue/queue.c" "$src/d3d12_vm.c"; do
    x86_64-w64-mingw32-gcc -Os -Wall -Wextra -Werror -DSCARLETT_DEVICE_BOOTSTRAP -DSCARLETT_STAGED_PLACED -c "$f" -o "$(basename "$f" .c).o"
done
x86_64-w64-mingw32-g++ -std=c++17 -Os -Wall -Wextra -Werror -static "$here/test.cpp" ./*.o -ld3d12 -lbcrypt -o game/game_scarlett_master.exe
for gpu in radv nvidia; do
    display=xvfb; if [[ $gpu == radv ]]; then display=sway; fi
    TMPDIR="$w/$gpu" SCARLETT_TEST_GPU=$gpu SCARLETT_TEST_DISPLAY=$display "$src/../test-launch-nixos.sh" game "$tool"
    grep -F 'PASS original 122630' "$w/$gpu"/scarlett-run.*/logs/steam-*.log
done
