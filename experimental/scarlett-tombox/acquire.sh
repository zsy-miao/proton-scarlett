#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
root=${1:-/tmp/opencode/tombox-provider}
mkdir -p "$root"
cd "$root"
root=$PWD
export CARGO_HOME="${CARGO_HOME:-$root/cargo}"
rev=f89e4106fe8a1728515afa34ceb5097132cadcda
target=x86_64-pc-windows-gnu
providers=(xmem acphal chad xapu xcrdapi xg_xs d3d12_xs)

get() {
    if [[ ! -f $1 ]]; then
        curl -fL --retry 3 "$3" -o "$1.part"
        mv "$1.part" "$1"
    fi
    echo "$2  $1" | sha256sum -c -
}
get source.tar.gz 4ac72a14464d4b5b87d94eb460da86d32065f3159d6bdcb1cd2f08e7fa157bba \
    "https://codeload.github.com/omonkeyz/xbox-emulator/tar.gz/$rev"
get dxc.tar.gz f2213da1fc99dc8778c8823078e16ba97c7f80f86a1d4520ab1adf4b462bc48c \
    https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.8.2505.1/linux_dxc_2025_07_14.x86_64.tar.gz
build=$(mktemp -d "$root/build.XXXXXXXX")
mkdir "$build/dxc" "$build/bin"
tar xf source.tar.gz -C "$build"
tar xf dxc.tar.gz -C "$build/dxc"
export TOMBOX_DXC="$build/dxc/bin/dxc"
export CARGO_TARGET_X86_64_PC_WINDOWS_GNU_RUSTFLAGS="--remap-path-prefix=$build=/tombox-build --remap-path-prefix=$CARGO_HOME=/cargo -C link-arg=-Wl,--no-insert-timestamp"
export SOURCE_DATE_EPOCH=1795435096
if [[ ! -f $(x86_64-w64-mingw32-gcc -print-file-name=libpthread.a) ]]; then
    get mingw.deb 0bf89cf7454cccb49cd2a46a6f7b33896f6ae3a1a2fe33e02c40be6155bbb385 \
        https://deb.debian.org/debian/pool/main/m/mingw-w64/mingw-w64-x86-64-dev_12.0.0-5_all.deb
    ar p mingw.deb data.tar.xz | tar xJ -C "$build" --strip-components=4 \
        ./usr/x86_64-w64-mingw32/lib/libpthread.a
    CARGO_TARGET_X86_64_PC_WINDOWS_GNU_RUSTFLAGS+=" -Lnative=$build"
fi
cd "$build/xbox-emulator-$rev"
export CARGO_TARGET_DIR="$build/target"
args=()
for p in "${providers[@]}"; do args+=(-p "$p"); done
cargo +1.95.0 build --locked --release --target "$target" "${args[@]}"
for p in "${providers[@]}"; do
    x86_64-w64-mingw32-strip -o "$build/bin/$p.dll" "$CARGO_TARGET_DIR/$target/release/$p.dll"
done
x86_64-w64-mingw32-gcc -Wall -Wextra -static -Os "$here/probe.c" -o "$build/bin/probe.exe"
cp LICENSE "$build/bin/COPYING.TomBox"
cp THIRD_PARTY/CREDITS "$build/bin/CREDITS.TomBox"
mkdir "$build/bin/licenses"
cp THIRD_PARTY/LICENSE* crates/compat/xcrdapi/src/bcpack/LICENSE* \
    crates/tombox-xshader/src/trans32/LICENSE* "$build/bin/licenses/"
cd "$build/bin"
sha256sum *.dll > SHA256SUMS
sha256sum -c SHA256SUMS
echo "Wine provider directory: $PWD"
