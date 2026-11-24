#!/usr/bin/env bash
# Cross-build the clear → rectangle recorder fixture. Inputs are supplied locally.
set -euo pipefail
[[ $# == 3 ]] || { echo "usage: $0 PRIVATE-INPUT-DIR MATCHED-XMEM-DLL NEW-OUTPUT-DIR" >&2; exit 2; }
n=$(realpath "$(dirname "$0")/../native")
inputs=$(realpath "$1")
xmem=$(realpath "$2")
mkdir -- "$3"
cd -- "$3"
mkdir obj game
cp -- "$inputs"/{stream,vs,ps,emitter,colour-stream,colour-VS,colour-PS}.bin game/
(cd game && sha256sum --check --strict <<'HASH'
15c7477fed4e9f4a404108cb420de12ffca7197e824f8e6250a1532879be7693  stream.bin
8574a817bd2754b2603987e45c8451ddc2ec1564a04e7e547f68a5c634a46bd9  vs.bin
c9f99cce129603afe795042cced590b5f25eb664150abff191f1498539aab819  ps.bin
0123f60081e006082caef74a714c479bc801fddbbd17dcbee708c20a187599bf  emitter.bin
242fa193610695ae761bca0d356b94ca1e400a992fcc6669753485b432fd4cc7  colour-stream.bin
672f389b67f217e8cf805442d2c77fab415b0b01f69241d18386639a313e1298  colour-VS.bin
da4829b14afdf3d0f6dc8d44c8fdbf3cfb8b84bcd7b8dacdf403c0d456a811ea  colour-PS.bin
HASH
)
flags=(-Os -Wall -Wextra -Werror -DSCARLETT_DEVICE_BOOTSTRAP -DSCARLETT_STAGED_PLACED
    -DSCARLETT_EXPERIMENTAL_TAGGED_BATCH -DSCARLETT_EXPERIMENTAL_PRIMARY_RELEASE
    -DSCARLETT_EXPERIMENTAL_DIRECT_WAIT -DSCARLETT_EXPERIMENTAL_BRIDGE
    -DTEST_LATE_COLOUR -DTEST_INLINE_B27)
for f in test-clear-device queue/test-clear4-bind d3d12_vm; do
    x86_64-w64-mingw32-gcc "${flags[@]}" -c "$n/$f.c" -o "obj/${f##*/}.o"
done
x86_64-w64-mingw32-g++ "${flags[@]}" -std=c++17 -static -Wl,--no-insert-timestamp \
    "$n/test-clear-pipeline.cpp" obj/{test-clear-device,test-clear4-bind,d3d12_vm}.o \
    "$xmem" -lbcrypt -ld3d12 -ldxgi -ldxguid -o game/game_scarlett_master.exe
