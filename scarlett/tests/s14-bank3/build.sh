#!/usr/bin/env bash
# Cross-build only, from the retained split-six replacement source stage.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
base=$(realpath "${1:?usage: bash build.sh GUARDED_STAGE}")
(cd "$base" && sha256sum --strict -c "$here/preimages.sha256")
n=scarlett/native
for new in fresh-publication.h test-s14-bank3.c; do
    [[ ! -e "$base/$n/queue/$new" ]] || { echo "Already present: $new" >&2; exit 1; }
done
out=$(mktemp -d "${TMPDIR:-/tmp}/s14-bank3.XXXXXXXX")
echo "Build directory: $out"
cd "$out"
mkdir -p scarlett obj control provider rtx
cp -RL "$base/$n" "$n"
ln -s "$base/inputs" inputs
cc=x86_64-w64-mingw32-gcc
f=(-Os -Wall -Wextra -Werror -DSCARLETT_DEVICE_BOOTSTRAP -DSCARLETT_STAGED_PLACED
   -DSCARLETT_EXPERIMENTAL_DIRECT_WAIT -DSCARLETT_EXPERIMENTAL_PRIMARY_RELEASE
   -DSCARLETT_EXPERIMENTAL_TAGGED_BATCH -DSCARLETT_EXPERIMENTAL_BRIDGE -DSCARLETT_PAIR_SUBMIT
   -DXS_IM96_FACTORY -DXS_RTX_RETURNED_TABLE -DXS_RTX_ATTRIBUTED_FIVE -Iinputs)
provider(){
    "$cc" "${f[@]}" -c "$n/queue/queue.c" -o obj/queue.o
    "$cc" -shared -s -Wl,--no-insert-timestamp,--image-base,0x24d560000 \
        inputs/d3d12_device.o obj/queue.o inputs/d3d12_vm.o "$n/d3d12_xs.def" -ld3d12 -ldxgi -o "$1"
}
provider control/d3d12_xs.dll
echo '72ab3eb4d8902de4ffddd622e779ae993be0cec606df76050ab09385dbfecc3f  control/d3d12_xs.dll' | sha256sum -c -
patch --batch --forward --fuzz=0 -p1 < "$here/recorder.patch"
f+=(-DXS_TRACE_FRESH_PUBLICATION)
provider provider/d3d12_xs.dll
"$cc" "${f[@]}" -c "$n/queue/test-s14-bank3.c" -o obj/bank3-queue.o
"$cc" "${f[@]}" -DTEST_DISTINCT_PRIMARY -DTEST_CARRIED_TARGET -static -Wl,--no-insert-timestamp \
    "$n/test-s14-release.c" obj/bank3-queue.o inputs/d3d12_vm.o -ld3d12 -ldxgi -ldxguid -o rtx/game_scarlett_master.exe
sha256sum -c <<'EOF'
becb01a3d36fdb0294d4d778087dfd25269bc6cf80566e90465807cff9a3f0d3  provider/d3d12_xs.dll
a47929647e075d21d0e5f5d1db3d13a6df30df5aaf3d571de8bfd4ee6053ede5  rtx/game_scarlett_master.exe
EOF
