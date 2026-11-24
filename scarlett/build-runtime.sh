#!/usr/bin/env bash
# Build project-owned x64 PE libraries with MinGW, without game files.
set -euo pipefail
here=$(realpath "$(dirname "$0")")
out=${1:?usage: build-runtime.sh NEW-OUTPUT-DIRECTORY}
sdk=$(realpath "${SCARLETT_GAMEINPUT_HEADER:?Set SCARLETT_GAMEINPUT_HEADER to Windows SDK 10.0.26100 um/GameInput.h}")
[[ ${sdk##*/} == GameInput.h ]] && printf '%s  %s\n' c77b8621d3d503f5ddcb2a45f76f024f3e6f881e568133e7bf1a4fc05985d539 "$sdk" | sha256sum -c --status ||
    { echo "Expected the v0 GameInput.h listed in scarlett/native/gameinput/README.md" >&2; exit 1; }
mkdir "$out"
queue_flags=()
queue_libs=()
case ${SCARLETT_EXPERIMENTAL_TAGGED_BATCH:-0} in
    0) ;;
    1) queue_flags=(-DSCARLETT_EXPERIMENTAL_TAGGED_BATCH -DSCARLETT_EXPERIMENTAL_PRIMARY_RELEASE -DSCARLETT_EXPERIMENTAL_DIRECT_WAIT) ;;
    *) echo "SCARLETT_EXPERIMENTAL_TAGGED_BATCH must be 0 or 1" >&2; exit 2 ;;
esac
case ${SCARLETT_EXPERIMENTAL_BRIDGE:-0} in
    0) ;;
    1) [[ ${SCARLETT_EXPERIMENTAL_TAGGED_BATCH:-0} == 1 ]] || { echo 'The private bridge requires the tagged batch build' >&2; exit 2; }; queue_flags+=(-DSCARLETT_EXPERIMENTAL_BRIDGE); queue_libs+=(-ldxgi) ;;
    *) exit 2 ;;
esac
x86_64-w64-mingw32-gcc -DSCARLETT_DEVICE_BOOTSTRAP -DSCARLETT_STAGED_PLACED "${queue_flags[@]}" -shared -Os -s -Wall -Wextra -Werror -Wl,--no-insert-timestamp \
    "$here/native/d3d12_device.c" "$here/native/queue/queue.c" "$here/native/d3d12_vm.c" "$here/native/d3d12_xs.def" \
    -ld3d12 "${queue_libs[@]}" -o "$out/d3d12_xs.dll"
build_dll() {
    local source=$1 output=$2
    shift 2
    x86_64-w64-mingw32-gcc -std=c11 -shared -Os -s -Wall -Wextra -Werror \
        -Wl,--no-insert-timestamp "$here/native/$source/$source.c" "$@" -o "$out/$output.dll"
}
inc=$(mktemp -d)
trap 'rm -rf -- "$inc"' EXIT
x86_64-w64-mingw32-gcc -std=c11 -Os -Wall -Wextra -Werror -c "$here/native/xmem/xmem.c" -o "$inc/xmem.o"
x86_64-w64-mingw32-g++ -std=c++17 -Os -s -Wall -Wextra -Werror -shared -static \
    "$inc/xmem.o" "$here/native/xmem/graphics.cpp" -lntdll -Wl,--no-insert-timestamp -o "$out/xmem.dll"
build_dll chad chad
build_dll xapu xapu
build_dll acphal AcpHal
build_dll xg xg_xs
source "$here/native/xcrdapi/zlib-flags.sh"
build_dll xcrdapi XCRDAPI "${zflags[@]}" -luuid
build_dll gameruntime xgameruntime -lntdll
ln -s "$sdk" "$inc/GameInput.h"
x86_64-w64-mingw32-g++ -std=c++20 -Os -s -Wall -Wextra -Werror -Wno-unknown-pragmas \
    -I"$inc" -shared "$here/native/gameinput/gameinput.cpp" "$here/native/gameinput/gameinput.def" \
    -Wl,--no-insert-timestamp -luuid -static-libgcc -static-libstdc++ -o "$out/GameInput.dll"
if [[ ${SCARLETT_EXPERIMENTAL_BRIDGE:-0} == 1 ]]; then touch "$out/requires-wine-0016"; fi
