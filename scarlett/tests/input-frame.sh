#!/usr/bin/env bash
set -euo pipefail
here=$(realpath "$(dirname "$0")")
build=$(mktemp -d)
trap 'rm -rf -- "$build"' EXIT
x86_64-w64-mingw32-gcc -O2 -Wall -Werror "$here/input-frame.c" -lgdi32 -o "$build/game_scarlett_master.exe"
"$here/../test-launch-nixos.sh" "$build" "$@" | tee "$build/run.log"
work=$(sed -n 's/^Test output (including prefix): //p' "$build/run.log")
grep -F 'PASS Scarlett private input/frame' "$work/logs/steam-999001.log"
