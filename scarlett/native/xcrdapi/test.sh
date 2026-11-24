#!/usr/bin/env bash
set -eu
cd "$(dirname "$0")"
tmp=$(mktemp -d "${TMPDIR:-/tmp}/xcrdapi.XXXXXX")
export WINEPREFIX="$tmp/prefix" WINEARCH=win64 WINEDLLOVERRIDES='xcrdapi=n;mscoree,mshtml=' WINEDEBUG=-all
trap 'if test -d "$WINEPREFIX"; then wineserver -w; fi; rm -rf "$tmp"' EXIT
sdk=microsoft.direct3d.directstorage
curl -fsSL "https://api.nuget.org/v3-flatcontainer/$sdk/1.2.3/$sdk.1.2.3.nupkg" -o "$tmp/sdk.zip"
(cd "$tmp" && sha256sum -c <<< '00a555cbb5734b35b67f63c2c0a3878e3ab4a25fbef44351090721f4941867c0  sdk.zip' &&
    unzip -qj sdk.zip 'native/include/*.h')
# MinGW g++ discards DECLSPEC_UUID; read these two GUIDs from that same header.
guid() {
    sed -n "s/^DECLARE_INTERFACE_IID_($1, IUnknown, \"\\([0-9a-f-]*\\)\").*/\\1/p" "$tmp/dstorage.h"
}
mkdir -p build
x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -Werror -shared xcrdapi.c -luuid \
    -Wl,--out-implib,"$tmp/libxcrdapi.a" -o build/XCRDAPI.dll
cp build/XCRDAPI.dll "$tmp/"
x86_64-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -Werror -isystem "$tmp" \
    "-DFACTORY_GUID=L\"{$(guid IDStorageFactory)}\"" "-DFILE_GUID=L\"{$(guid IDStorageFile)}\"" \
    test.cpp -L"$tmp" -lxcrdapi -lole32 -luuid -static -o "$tmp/test.exe"
wine "$tmp/test.exe"
x86_64-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -Werror test_console.cpp \
    -L"$tmp" -lxcrdapi -lole32 -luuid -static -o "$tmp/test_console.exe"
wine "$tmp/test_console.exe"
x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -Werror test_status.c -luuid -o "$tmp/test_status.exe"
wine "$tmp/test_status.exe"
