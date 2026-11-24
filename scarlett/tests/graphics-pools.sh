#!/usr/bin/env bash
# Test the three retail-size pools through the installed XMem and graphics stack.
set -euo pipefail
here=$(realpath "$(dirname "$0")/..")
tool=$(realpath "${1:-$HOME/.local/share/Steam/compatibilitytools.d/scarlett-proton}")
game=$(mktemp -d)
trap 'rm -rf -- "$game"' EXIT
for mode in TEST_MARKED_SECTION TEST_WC_SECTION TEST_WB563_SECTION; do
    x86_64-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -Werror -static "-D$mode" \
        "$here/native/xmem/test_gpu.cpp" "$tool/scarlett/xmem.dll" -ld3d12 \
        -o "$game/game_scarlett_master.exe"
    "$here/test-launch-nixos.sh" "$game" "$tool"
    echo "PASS: $mode"
done
