#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
root=${1:-/tmp/opencode/scarlett-http-provider}
mkdir -p "$root"
cd "$root"
work=$(mktemp -d "$PWD/build.XXXXXXXX")
cd "$work"
mkdir src zlib bin

fetch() {
    curl -fsSL --retry 3 "$1" -o "$2.tar.gz"
    echo "$3  $2.tar.gz" | sha256sum -c -
    tar -xf "$2.tar.gz" -C "$2" --strip-components=1
}
fetch https://codeload.github.com/microsoft/libHttpClient/tar.gz/c5007fd9cbdc785518ea6dccc5751b125d451cc1 \
    src 1e8aadffcc04a6da062b973b57c45f287b3a896f333d7eb45239cf3d18295dca
fetch https://codeload.github.com/madler/zlib/tar.gz/da607da739fa6047df13e66a2af6b8bec7c2a498 \
    zlib b9258cf6254e7f7c37f1cd61dba943a1c5ea3cff5718c789834dac359094f5f7
patch --fuzz=0 -d src -p1 < "$here/mingw.patch"

target=x86_64-w64-mingw32
cxx=$target-g++
# A standalone Nix compiler wrapper links MCF but only propagates its headers in a dev shell.
support=$(dirname "$(dirname "$(realpath "$(command -v "$cxx")")")")/nix-support
if [[ -f "$support/propagated-target-target-deps" ]]; then
    for dep in $(cat "$support/propagated-target-target-deps"); do
        if [[ -f "$dep/include/mcfgthread/gthr.h" ]]; then
            export CPLUS_INCLUDE_PATH="$dep/include${CPLUS_INCLUDE_PATH:+:$CPLUS_INCLUDE_PATH}"
        fi
    done
fi

cd src/Build/libHttpClient.Win32.CMake
export SOURCE_DATE_EPOCH=1795226300
cmake -S . -B "$work/obj" -DCMAKE_SYSTEM_NAME=Windows \
    -DCMAKE_C_COMPILER="$target-gcc" -DCMAKE_CXX_COMPILER="$cxx" \
    -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=ON -DHC_SKIP_VCPKG_RESTORE=ON \
    -DHC_ZLIB_SOURCE_DIR="$work/zlib" \
    "-DCMAKE_CXX_FLAGS=-ffile-prefix-map=$work=/scarlett-http" \
    "-DCMAKE_SHARED_LINKER_FLAGS=-static -Wl,--no-insert-timestamp"
cmake --build "$work/obj" --parallel "${JOBS:-4}"
cd "$work"
"$target-strip" --preserve-dates -o bin/libHttpClient.GDK.dll obj/libHttpClient.GDK.dll
"$cxx" -std=c++17 -D_WIN32_WINNT=0x0601 -static -Os -s -Isrc/Include \
    "$here/probe.cpp" obj/liblibHttpClient.GDK.dll.a -o bin/probe.exe
cp src/LICENSE.md bin/LICENSE.libHttpClient
cp src/{NOTICE.txt,ThirdPartyNotices.txt} bin/
cp zlib/LICENSE bin/LICENSE.zlib
cd bin
sha256sum libHttpClient.GDK.dll > SHA256SUMS
printf 'Provider directory: %s\n' "$PWD"
