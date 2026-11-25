#!/usr/bin/env bash
# Offline replay. Inputs are read-only; the output directory is always new.
set -euo pipefail
umask 077
here=$(cd "$(dirname "$0")" && pwd)
ready=$(realpath "${1:?supply the pinned READY96 stage}")
base=$(realpath "${2:?supply the qualified split-six OM stage}")
control="$ready/game/game_scarlett_master.exe"
control_id=$(stat -Lc '%d:%i:%h' "$control")
check_control() { (
    cd "$ready"
    echo '39bf44851fdcbd2e657df485243c324f65655d75a7772e8122e56e28759d6866  game.sha256' | sha256sum -c -
    sha256sum -c game.sha256
    test "$(stat -Lc '%d:%i:%h' "$control")" = "$control_id"
); }
check_control
out=$(mktemp -d "${TMPDIR:-/tmp}/returned-first0.XXXXXXXX")
cd "$out"
mkdir -p inputs game obj scarlett/tests/im96
cp -RL "$base/scarlett/native" scarlett/
cp "$base"/inputs/{d3d12_vm.o,im96_host_dxil.h} inputs/
cp "$ready"/scarlett/tests/im96/{used-proof.h,used-table.hpp} scarlett/tests/im96/
cp "$ready/scarlett/native/queue/im96-consume.h" scarlett/native/queue/
sha256sum -c "$here/preimages.sha256"
patch --batch --forward --fuzz=0 -p1 < "$here/fixture.patch"
cp "$here"/{list.c,main.cpp} .
cp "$ready"/game/{camera,object,stream,vs,ps,fixed96,vs96,ps96}.bin "$ready/game/xmem.dll" game/
n=scarlett/native
flags=(-Os -Wall -Wextra -Werror -DSCARLETT_DEVICE_BOOTSTRAP -DSCARLETT_STAGED_PLACED
       -DSCARLETT_EXPERIMENTAL_TAGGED_BATCH -DSCARLETT_EXPERIMENTAL_PRIMARY_RELEASE
       -DSCARLETT_EXPERIMENTAL_DIRECT_WAIT -DSCARLETT_EXPERIMENTAL_BRIDGE -DXS_IM96_FACTORY
       -Iinputs -DSCARLETT_PAIR_SUBMIT -DTEST_SR_REUSE -DTEST_CARRIED_TARGET -DTEST_IM96_USED
       -DTEST_IM96_CONSUME -DTEST_DISTINCT_PRIMARY -I"$n" -I"$n/queue" -Iscarlett/tests/im96 -I.)
for src in "$n/test-clear-device.c" list.c; do
    x86_64-w64-mingw32-gcc "${flags[@]}" -c "$src" -o "obj/$(basename "$src" .c).o"
done
x86_64-w64-mingw32-g++ "${flags[@]}" -std=c++17 -static -s -Wl,--no-insert-timestamp \
    main.cpp obj/{test-clear-device,list}.o inputs/d3d12_vm.o game/xmem.dll -lbcrypt -ld3d12 -ldxgi -o game/game_scarlett_master.exe
sha256sum -c "$here/outputs.sha256"
test ! game/game_scarlett_master.exe -ef "$control"
check_control
echo "Verified fixture: $PWD/game"
