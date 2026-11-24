#!/usr/bin/env bash
# Cross-build only. No Wine, adapter or retail data required.
set -euo pipefail
src=$(cd "$(dirname "$0")/../native" && pwd)
out=${1:?usage: build-blit-colour-rect.sh output-directory}
mkdir -p "$out"; cd "$out"
compile(){
    "${DXC:-dxc}" -Ges -Gis -WX -O3 -Qstrip_debug -Qstrip_reflect \
        -T "$1"_6_0 -E "$2" -Fo "$2.dxil" "$src/$3.hlsl"
}
compile gs rect_gs blit_colour_rect
compile vs vs blit_colour
compile ps ps blit_colour
compile ps probe test-blit-colour-rect
sha256sum -c <<'HASH'
94f16a8a8b3bd5248096b40fff415fe19af43d4d42200b2eedf0c3f8cf0adbeb  vs.dxil
637fcc104ec0b585736ddb958265ceb1b4fa4cbeeeafee61662668a628b812d2  ps.dxil
HASH
python3 - <<'PY'
from pathlib import Path
def array(name, file):
    b = Path(file + ".dxil").read_bytes()
    lines = ["    " + ",".join(f"0x{x:02x}" for x in b[i:i+16]) + "," for i in range(0,len(b),16)]
    return f"static const unsigned char {name}[]={{\n" + "\n".join(lines) + "\n};\n"
Path("gs-rebuilt.h").write_text("/* Generated from blit_colour_rect.hlsl; see build-blit-colour-rect.sh. */\n" + array("blit_colour_rect_gs","rect_gs"))
Path("rect-test-stages.h").write_text("".join(array(k,k) for k in ("vs","ps","probe")))
PY
cmp gs-rebuilt.h "$src/blit_colour_rect_dxil.h"
x86_64-w64-mingw32-g++ -std=c++17 -static -O2 -Wall -Wextra -Werror -I. \
    "$src/test-blit-colour-rect.cpp" -ld3d12 -o game_scarlett_master.exe
