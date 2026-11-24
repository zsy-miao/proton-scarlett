#!/usr/bin/env bash
# Drive the installed provider in a separate process; check pixels after H.264 decoding.
set -euo pipefail
here=$(dirname "$(realpath "$0")")
sdk=$(realpath "${SCARLETT_GAMEINPUT_HEADER:?Set to SDK 10.0.26100 um/GameInput.h}")
w=$(mktemp -d "${TMPDIR:-/tmp}/scarlett-pad.XXXXXXXX")
mkdir "$w/game" "$w/inc"
ln -s "$sdk" "$w/inc/GameInput.h"
printf 'LIBRARY GameInput.dll\nEXPORTS\nGameInputCreate @1 NONAME\n' >"$w/import.def"
x86_64-w64-mingw32-dlltool -d "$w/import.def" -l "$w/import.a"
x86_64-w64-mingw32-g++ -O2 -static -DTEST_GAMEINPUT -I"$w/inc" "$here/present-capture.cpp" \
    "$w/import.a" -ld3d12 -ldxgi -ldxguid -luuid -o "$w/game/game_scarlett_master.exe"
SCARLETT_TEST_GPU=radv SCARLETT_TEST_DISPLAY=sway SCARLETT_RECORD=1 SCARLETT_INPUT_TITLE='Presentation fixture' \
    TMPDIR="$w" "$here/../test-launch-nixos.sh" "$w/game" "$@" &
launcher=$!
trap 'kill "$launcher" 2>/dev/null || :' EXIT
for i in {1..300}; do
    run=("$w"/scarlett-run.*)
    log=${run[0]}/logs/steam-999001.log
    if grep -aqF 'GameInput buttons=0' "$log" 2>/dev/null; then break; fi
    kill -0 "$launcher"; sleep .1
done
grep -aqF 'GameInput buttons=0' "$log"
# Leave multiple neutral samples for the recorder before the bounded hold.
sleep .6
timeout 3 bash -c 'printf "invalid 700\nspace 700\n" >"$1"' _ "${run[0]}/input.fifo"
wait "$launcher"
trap - EXIT
grep -F 'refused input' "${run[0]}/logs/input.log"
grep -F 'space held 700 ms' "${run[0]}/logs/input.log"
test "$(readlink "${run[0]}/compat/scarlett-title/pfx/drive_c/windows/system32/gameinput.dll")" = /proton/scarlett/GameInput.dll
grep -ai 'system32.*gameinput\.dll".*: native' "$log"
grep -aF 'PASS synthetic GameInput DXGI: neutral, A, released' "$log"
ffmpeg -v error -i "${run[0]}/display.mp4" -vf 'crop=2:2:400:300,scale=1:1,format=rgb24' -f rawvideo - |
    python3 -c 'import itertools, sys
rgb=iter(sys.stdin.buffer.read())
runs=[(k,len(list(v))) for k,v in itertools.groupby(
    "blue" if b>245 and max(r,g)<10 else "green" if g>245 and max(r,b)<10 else "other"
    for r,g,b in zip(rgb,rgb,rgb))]
print("Synthetic DXGI center pixel:", runs)
if [k for k,n in runs if k!="other" and n>=2]!=["blue","green","blue"]: sys.exit(1)'
