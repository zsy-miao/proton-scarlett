#!/usr/bin/env bash
# Cross-build the clear → rectangle recorder fixture. Inputs are supplied locally.
set -euo pipefail
game=game
if [[ $# == 4 && $1 == --qualified ]]; then
    shift
    game=returned
    umask 077
fi
[[ $# == 3 ]] || { echo "usage: $0 [--qualified] PRIVATE-INPUT-DIR MATCHED-XMEM-DLL NEW-OUTPUT-DIR" >&2; exit 2; }
n=$(realpath "$(dirname "$0")/../native")
inputs=$(realpath "$1")
xmem=$(realpath "$2")
mkdir -- "$3"
cd -- "$3"
mkdir obj "$game"
cp -- "$inputs"/{stream,vs,ps,emitter,colour-stream,colour-VS,colour-PS}.bin "$game/"
(cd "$game" && sha256sum --check --strict <<'HASH'
15c7477fed4e9f4a404108cb420de12ffca7197e824f8e6250a1532879be7693  stream.bin
8574a817bd2754b2603987e45c8451ddc2ec1564a04e7e547f68a5c634a46bd9  vs.bin
c9f99cce129603afe795042cced590b5f25eb664150abff191f1498539aab819  ps.bin
0123f60081e006082caef74a714c479bc801fddbbd17dcbee708c20a187599bf  emitter.bin
242fa193610695ae761bca0d356b94ca1e400a992fcc6669753485b432fd4cc7  colour-stream.bin
672f389b67f217e8cf805442d2c77fab415b0b01f69241d18386639a313e1298  colour-VS.bin
da4829b14afdf3d0f6dc8d44c8fdbf3cfb8b84bcd7b8dacdf403c0d456a811ea  colour-PS.bin
HASH
)
if [[ $game == returned ]]; then
    mkdir provider amd-first
    cp -- "$xmem" returned/xmem.dll
    sha256sum --check --strict <<< 'c58c784b78930268a26498a3af1148adfbbb1c3cea97bb66d26763999323cfa0  returned/xmem.dll'
    python3 -IB "$n/../tests/im96/factory-shaders.py" obj/im96_host_dxil.h
    cc=x86_64-w64-mingw32-gcc
    f=(-Os -Wall -Wextra -Werror -DSCARLETT_DEVICE_BOOTSTRAP -DSCARLETT_STAGED_PLACED
       -DSCARLETT_EXPERIMENTAL_DIRECT_WAIT -DSCARLETT_EXPERIMENTAL_PRIMARY_RELEASE
       -DSCARLETT_EXPERIMENTAL_TAGGED_BATCH -DSCARLETT_EXPERIMENTAL_BRIDGE
       -DSCARLETT_PAIR_SUBMIT -DSCARLETT_GUARDED_PAIR_DISPATCH -DXS_IM96_FACTORY -Iobj)
    for file in queue/queue d3d12_device d3d12_vm; do
        "$cc" "${f[@]}" -c "$n/$file.c" -o "obj/${file##*/}.o"
    done
    "$cc" -shared -s -Wl,--no-insert-timestamp,--image-base,0x24d560000 \
        obj/{d3d12_device,queue,d3d12_vm}.o "$n/d3d12_xs.def" -ld3d12 -ldxgi -o provider/d3d12_xs.dll
    for mode in returned amd-first; do
        branch=(-DTEST_NATIVE_FIRST180 -DTEST_FIRST180)
        if [[ $mode == returned ]]; then
            branch=(-DTEST_FIRST180 -DTEST_RTX_TRIP -DTEST_FRESH190 -DTEST_MEASURED_B27
                    -DTEST_CARRIED_REPLACEMENT -DTEST_CARRIED_TARGET)
        fi
        ff=("${f[@]}" -DTEST_RESTORED_CLEAR -DTEST_CARRIED_60 "${branch[@]}" -DTEST_LATE_COLOUR
            -DTEST_INLINE_B27 -DTEST_SECOND_VB -DTEST_DISTINCT_PRIMARY -DTEST_DEFER_RELEASE)
        for file in queue/test-clear4-bind test-clear-device; do
            "$cc" "${ff[@]}" -c "$n/$file.c" -o "obj/${file##*/}.o"
        done
        x86_64-w64-mingw32-g++ -std=c++17 -static -Wl,--no-insert-timestamp "${ff[@]}" \
            "$n/test-clear-pipeline.cpp" obj/{test-clear4-bind,test-clear-device,d3d12_vm}.o \
            returned/xmem.dll -lbcrypt -ld3d12 -ldxgi -ldxguid -o "$mode/game_scarlett_master.exe"
    done
    cp -- returned/*.bin returned/xmem.dll amd-first/
    sha256sum --check --strict <<'HASH'
792e68b9a81361eecb37c8491a106d29e05ea413a3ca94ebc733e52c7bc4d1e9  provider/d3d12_xs.dll
6ac48ea6160de3b42974a4bead65d08a802c696dd43954249e939877a8825f4c  returned/game_scarlett_master.exe
1c2b07e83f537c5cf3fe97d393770fda94012a4ca98994391e34dc81f81bbc21  amd-first/game_scarlett_master.exe
HASH
    exit 0
fi
flags=(-Os -Wall -Wextra -Werror -DSCARLETT_DEVICE_BOOTSTRAP -DSCARLETT_STAGED_PLACED
    -DSCARLETT_EXPERIMENTAL_TAGGED_BATCH -DSCARLETT_EXPERIMENTAL_PRIMARY_RELEASE
    -DSCARLETT_EXPERIMENTAL_DIRECT_WAIT -DSCARLETT_EXPERIMENTAL_BRIDGE
    -DTEST_LATE_COLOUR -DTEST_INLINE_B27 -DTEST_SECOND_VB -DTEST_DISTINCT_PRIMARY -DTEST_DEFER_RELEASE)
for f in test-clear-device queue/test-clear4-bind d3d12_vm; do
    x86_64-w64-mingw32-gcc "${flags[@]}" -c "$n/$f.c" -o "obj/${f##*/}.o"
done
x86_64-w64-mingw32-g++ "${flags[@]}" -std=c++17 -static -Wl,--no-insert-timestamp \
    "$n/test-clear-pipeline.cpp" obj/{test-clear-device,test-clear4-bind,d3d12_vm}.o \
    "$xmem" -lbcrypt -ld3d12 -ldxgi -ldxguid -o game/game_scarlett_master.exe
