#!/usr/bin/env bash
# Build the separate Windows host and stage the x64 shader compiler beside it.
set -euo pipefail
out=$(realpath -m "${1:?usage: build-runtime.sh NEW-OUTPUT-DIRECTORY}")
mkdir -p "$(dirname "$out")"
[[ ! -e $out && ! -L $out ]] || { echo "Output already exists: $out" >&2; exit 1; }
work=$(mktemp -d "$(dirname "$out")/.build-scarlett.XXXXXXXX")
trap 'rm -rf -- "$work"' EXIT
cd "$work"
git clone -q --no-checkout https://github.com/ivall/xcl.git xcl
git -C xcl fetch -q origin 52015d5019b6c7f9353a75abf812b034fcf9c752
git -C xcl checkout -q --detach 66e8e974601791dee66bb60adfbeb4852ba88a25
git -C xcl cherry-pick --no-commit 52015d5019b6c7f9353a75abf812b034fcf9c752
test "$(git -C xcl write-tree)" = 55dd0c7ca88c142b0e1a9a9af002377146713653

# Some MinGW toolchains (including the Nix host used for testing) lack static winpthreads.
deb=mingw-w64-x86-64-dev_12.0.0-5_all.deb
curl -fsSLO "https://deb.debian.org/debian/pool/main/m/mingw-w64/$deb"
echo "0bf89cf7454cccb49cd2a46a6f7b33896f6ae3a1a2fe33e02c40be6155bbb385  $deb" | sha256sum -c -
libs=./usr/x86_64-w64-mingw32/lib
ar p "$deb" data.tar.xz | tar -xJ "$libs/libpthread.a" "$libs/libwinpthread.a"
export CARGO_TARGET_DIR="$work/xcl/target"
(cd xcl && cargo +1.91.1 rustc --locked --release --target x86_64-pc-windows-gnu -p xcl --bin xcl -- \
    -L "native=$work/$libs" -C link-arg=-Wl,--no-insert-timestamp)
mkdir runtime
install -m755 "$CARGO_TARGET_DIR/x86_64-pc-windows-gnu/release/xcl.exe" runtime/
cp xcl/{LICENSE,THIRD_PARTY.md} runtime/
curl -fsSLo dxc.zip https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.9.2609/dxc_2026_09_29.zip
echo "ad31b1fc8443175d204f77a611fdb3ef2ec42759bdc2f1167368de24a4a7e7f1  dxc.zip" | sha256sum -c -
python3 - <<'PY'
from pathlib import Path
from zipfile import ZipFile
with ZipFile("dxc.zip") as archive:
    for name in (r"bin\x64\dxcompiler.dll", r"bin\x64\dxil.dll", "LICENCE-MIT.txt", "LICENSE-LLVM.txt", "LICENSE-MS.txt"):
        Path("runtime", name.split("\\")[-1]).write_bytes(archive.read(name))
PY
mv -Tn runtime "$out"
test ! -e runtime
