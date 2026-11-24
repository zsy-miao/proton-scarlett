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
(
    cd "$g"
    for root in B27 PAIR; do
        "${DXC:-dxc}" "$s/roots.hlsl" -T rootsig_1_1 -E "$root" -Fo "$root.rs"
    done
    echo '14619793f5b26a039d76cacb5219bc488036b5a4e3166263d700befdbf605018  B27.rs' | sha256sum -c -
    for stage in vs ps; do
        "${DXC:-dxc}" -dumpbin -verifyrootsignature PAIR.rs "$stage.dxil"
        if "${DXC:-dxc}" -dumpbin -verifyrootsignature B27.rs "$stage.dxil" >"$stage.root-check" 2>&1; then
            echo "unexpected $stage compatibility with b27-only root" >&2; exit 1
        fi
        grep -F 'not fully bound' "$stage.root-check"
    done
)
x86_64-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -Werror -static "$s/draw.cpp" -ld3d12 -o "$g/game_scarlett_master.exe"
python3 -IB "$s/fixture.py" "$g"
python3 -IB "$s/cpu.py" "$g"
