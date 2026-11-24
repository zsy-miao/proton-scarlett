#!/usr/bin/env bash
set -euo pipefail
here=$(realpath "$(dirname "$0")")
build=$(mktemp -d)
trap 'rm -rf -- "$build"' EXIT
cd "$build"
printf 'LIBRARY gameinput.dll\nEXPORTS\nGameInputCreate\n' > gameinput.def
x86_64-w64-mingw32-dlltool -d gameinput.def -l gameinput.a
x86_64-w64-mingw32-gcc -O2 -Wall -Werror "$here/gameinput.c" gameinput.a -o game_scarlett_master.exe
cp "$here/private-xtest.py" .
"$here/../test-launch-nixos.sh" "$PWD" "$@" | tee run.log
work=$(sed -n 's/^Test output (including prefix): //p' run.log)
grep -F 'XTEST private Xvfb' "$work/logs/steam-999001.log"
grep -E 'GameInput (before|held|after):' "$work/logs/steam-999001.log"
grep -F 'PASS XCL GameInput Space -> A -> released' "$work/logs/steam-999001.log"
