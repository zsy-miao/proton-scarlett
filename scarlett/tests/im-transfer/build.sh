#!/usr/bin/env bash
# Compilation, CPU controls and fixture inputs only.
set -euo pipefail
s=$(cd "$(dirname "$0")" && pwd)
g=${1:?scratch output game directory required}
mkdir -p "$g"
for e in vs ps; do
    "${DXC:-dxc}" -WX -Ges -Gis -O3 -Qstrip_debug -Qstrip_reflect -T "${e}_6_0" -E "$e" \
        -Fo "$g/$e.dxil" "$s/../../native/im_ui_sdr.hlsl"
done
x86_64-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -Werror -static "$s/draw.cpp" -ld3d12 -o "$g/game_scarlett_master.exe"
python3 -IB "$s/data.py" "$g"
python3 -IB "$s/cpu.py" "$g"
