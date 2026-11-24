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
atg=https://raw.githubusercontent.com/microsoft/Xbox-GDK-Samples/b1e77b6ac10f95b07296025fda171ca2ddc42dbe/Samples/System/SimpleDirectStorageCombo/DirectStorageWin32
for header in dstorage_win32.h dstorageerr_win32.h; do curl -fsSL "$atg/$header" -o "$tmp/$header"; done
(cd "$tmp" && sha256sum -c <<'HASHES'
6408ecd093e4a794ba0cc9ba8e7d8c464ed5f2c4dbcbe7131108f7c57fe9092d  dstorage_win32.h
62fe4494a96e6a1c3ded3d25eff0788939c10b014a6265f1c8c7a0ea61b4e300  dstorageerr_win32.h
HASHES
)
x86_64-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -Werror -isystem "$tmp" test_console.cpp \
    -L"$tmp" -lxcrdapi -lole32 -luuid -static -o "$tmp/test_console.exe"
wine "$tmp/test_console.exe"
x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -Werror test_status.c -luuid -o "$tmp/test_status.exe"
wine "$tmp/test_status.exe"
x86_64-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -Werror test_queue.cpp \
    -L"$tmp" -lxcrdapi -luuid -static -o "$tmp/test_queue.exe"
wine "$tmp/test_queue.exe"
