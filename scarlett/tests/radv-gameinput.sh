#!/usr/bin/env bash
# Drive the installed provider in a separate process; check pixels after H.264 decoding.
set -euo pipefail
here=$(dirname "$(realpath "$0")")
case ${SCARLETT_TEST_GPU:-radv} in
    radv) display=sway; pixel=400:300 ;;
    nvidia) display=xvfb; pixel=200:160 ;;
    *) echo "SCARLETT_TEST_GPU must be nvidia or radv" >&2; exit 2 ;;
esac
sdk=$(realpath "${SCARLETT_GAMEINPUT_HEADER:?Set to SDK 10.0.26100 um/GameInput.h}")
w=$(mktemp -d "${TMPDIR:-/tmp}/scarlett-pad.XXXXXXXX")
mkdir "$w/game" "$w/inc"
ln -s "$sdk" "$w/inc/GameInput.h"
printf 'LIBRARY GameInput.dll\nEXPORTS\nGameInputCreate @1 NONAME\n' >"$w/import.def"
x86_64-w64-mingw32-dlltool -d "$w/import.def" -l "$w/import.a"
x86_64-w64-mingw32-g++ -O2 -static -DTEST_GAMEINPUT -I"$w/inc" "$here/present-capture.cpp" \
    "$w/import.a" -ld3d12 -ldxgi -ldxguid -luuid -o "$w/game/game_scarlett_master.exe"
SCARLETT_TEST_GPU=${SCARLETT_TEST_GPU:-radv} SCARLETT_TEST_DISPLAY=$display SCARLETT_RECORD=1 SCARLETT_INPUT_TITLE='Presentation fixture' \
    TMPDIR="$w" "$here/../test-launch-nixos.sh" "$w/game" "$@" &
launcher=$!
trap 'kill "$launcher" 2>/dev/null || :' EXIT
for i in {1..300}; do
    run=("$w"/scarlett-run.*)
    log=${run[0]}/logs/steam-999001.log
    if grep -aqF 'GameInput step=1' "$log" 2>/dev/null; then break; fi
    kill -0 "$launcher"; sleep .1
done
grep -aqF 'GameInput step=1' "$log"
# Leave multiple neutral samples for the recorder before the bounded hold.
sleep .6
timeout 3 bash -c 'printf "invalid 700\nspace 700\n" >"$1"' _ "${run[0]}/input.fifo"
for step_key in '3 w' '5 Left'; do
    read -r step key <<<"$step_key"
    for i in {1..50}; do
        grep -aqF "GameInput step=$step " "$log" && break
        kill -0 "$launcher"; sleep .1
    done
    grep -aqF "GameInput step=$step " "$log"
    sleep .5
    timeout 3 bash -c 'printf "%s 700\n" "$2" >"$1"' _ "${run[0]}/input.fifo" "$key"
done
wait "$launcher"
trap - EXIT
grep -F 'refused input' "${run[0]}/logs/input.log"
for key in space w Left; do grep -F ": $key held 700 ms" "${run[0]}/logs/input.log"; done
awk '/^input window hwnd=/{sub(/\r$/,""); if (!n++) window=$0; if ($0!=window) bad=1}
     END {exit (n!=3 || bad)}' "${run[0]}/logs/input.log"
test "$(readlink "${run[0]}/compat/scarlett-title/pfx/drive_c/windows/system32/gameinput.dll")" = /proton/scarlett/GameInput.dll
grep -ai 'system32.*gameinput\.dll".*: native' "$log"
grep -aF 'PASS synthetic GameInput DXGI: A, forward, camera left; all returned to neutral' "$log"
ffmpeg -v error -i "${run[0]}/display.mp4" -vf "crop=2:2:$pixel,scale=1:1,format=rgb24" -f rawvideo - |
    python3 -c 'import itertools, sys
rgb=iter(sys.stdin.buffer.read())
runs=[(k,len(list(v))) for k,v in itertools.groupby(
    "blue" if b>245 and max(r,g)<10 else "green" if g>245 and max(r,b)<10 else
    "red" if r>245 and max(g,b)<10 else "yellow" if min(r,g)>245 and b<10 else "other"
    for r,g,b in zip(rgb,rgb,rgb))]
print("Synthetic DXGI center pixel:", runs)
if [k for k,n in runs if k!="other" and n>=2]!=["blue","green","blue","red","blue","yellow","blue"]: sys.exit(1)'
