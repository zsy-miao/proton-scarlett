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
    python3 -IB "$s/exact-root.py" exact.hlsl
    "${DXC:-dxc}" -WX -T rootsig_1_1 -E EXACT exact.hlsl -Fo exact.rs
    echo '75011501d5fee24043c8b55af57ea04b85c9ae5604eae12147607f687c72dd62  exact.rs' | sha256sum -c -
    for root in B27 PAIR; do
        "${DXC:-dxc}" "$s/roots.hlsl" -T rootsig_1_1 -E "$root" -Fo "$root.rs"
    done
    echo '14619793f5b26a039d76cacb5219bc488036b5a4e3166263d700befdbf605018  B27.rs' | sha256sum -c -
    for stage in vs ps; do
        "${DXC:-dxc}" -dumpbin -verifyrootsignature exact.rs "$stage.dxil"
        "${DXC:-dxc}" -dumpbin -verifyrootsignature PAIR.rs "$stage.dxil"
        if "${DXC:-dxc}" -dumpbin -verifyrootsignature B27.rs "$stage.dxil" >"$stage.root-check" 2>&1; then
            echo "unexpected $stage compatibility with b27-only root" >&2; exit 1
        fi
        grep -F 'not fully bound' "$stage.root-check"
    done
)
obj=$(mktemp -d)
trap 'rm -rf -- "$obj"' EXIT
flags=(-Os -Wall -Wextra -Werror -DSCARLETT_DEVICE_BOOTSTRAP -DSCARLETT_STAGED_PLACED
    -DSCARLETT_EXPERIMENTAL_TAGGED_BATCH -DSCARLETT_EXPERIMENTAL_PRIMARY_RELEASE
    -DSCARLETT_EXPERIMENTAL_DIRECT_WAIT -DSCARLETT_EXPERIMENTAL_BRIDGE)
extra=()
if [[ ${IM96_RECT:-0} == 1 ]]; then
    : "${XMEM_DLL:?rectangle fixture requires the private used table}"
    flags+=(-DTEST_IM96_RECT)
    "${DXC:-dxc}" -WX -Ges -Gis -O3 -Qstrip_debug -Qstrip_reflect -T gs_6_0 -E gs -Fo "$g/rect.dxil" "$s/rect.hlsl"
    "${DXC:-dxc}" -dumpbin -verifyrootsignature "$g/exact.rs" "$g/rect.dxil"
fi
if [[ -n ${XMEM_DLL:-} ]]; then
    flags+=(-DTEST_IM96_USED)
    extra+=("$(realpath -e "$XMEM_DLL")")
fi
for f in test-clear-device d3d12_vm queue/queue; do
    x86_64-w64-mingw32-gcc "${flags[@]}" -c "$s/../../native/$f.c" -o "$obj/${f##*/}.o"
done
x86_64-w64-mingw32-g++ "${flags[@]}" -std=c++17 -static -Wl,--no-insert-timestamp \
    -DTEST_IM96_EXACT_ROOT "$s/draw.cpp" "$obj"/{test-clear-device,d3d12_vm,queue}.o \
    "${extra[@]}" -ld3d12 -ldxgi -ldxguid -o "$g/game_scarlett_master.exe"
python3 -IB "$s/fixture.py" "$g"
python3 -IB "$s/cpu.py" "$g"
if [[ ${IM96_RECT:-0} == 1 ]]; then
    python3 -B "$s/rect.py" "$g"
    python3 -IB "$s/rect-cpu.py" "$g"
fi
