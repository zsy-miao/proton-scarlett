#!/usr/bin/env bash
# Exercise the installed providers through the same direct Wine route as the retail PE.
set -euo pipefail
here=$(realpath "$(dirname "$0")/..")
tool=$(realpath "${1:-$HOME/.local/share/Steam/compatibilitytools.d/scarlett-proton}")
game=$(mktemp -d)
trap 'rm -rf -- "$game"' EXIT
check() {
    local module=$1 dll=$2 source=$3
    shift 3
    x86_64-w64-mingw32-gcc -O2 -Wall -Wextra -Werror "$@" "$here/native/$module/$source" \
        "$tool/scarlett/$dll.dll" -o "$game/game_scarlett_master.exe"
    "$here/test-launch-nixos.sh" "$game" "$tool"
    echo "PASS: $module/$source $*"
}
check chad chad test.c
check xapu xapu test.c
check acphal AcpHal test.c
check xg xg_xs test.c
check xmem xmem test.c
for mode in 0 1 2; do check xmem xmem test_alloc.c "-DHOOK_MODE=$mode"; done
