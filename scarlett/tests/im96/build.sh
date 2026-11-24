#!/usr/bin/env bash
set -euo pipefail
s=$(cd "$(dirname "$0")" && pwd)
g=${1:?new scratch output directory required}
# The launcher uses a retail-shaped filename; never replace an existing directory.
mkdir -- "$g"
for stage in vs ps; do
    "${DXC:-dxc}" -WX -Ges -Gis -O3 -Qstrip_debug -Qstrip_reflect -T "${stage}_6_0" -E "$stage" \
        -Fo "$g/$stage.dxil" "$s/../../native/im_default_pos_cpv_tex.hlsl"
done
x86_64-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -Werror -static "$s/draw.cpp" -ld3d12 -o "$g/game_scarlett_master.exe"
python3 -IB "$s/fixture.py" "$g"
python3 -IB "$s/cpu.py" "$g"
