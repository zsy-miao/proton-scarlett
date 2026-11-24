#!/usr/bin/env bash
set -euo pipefail
src=$(cd "$(dirname "$0")" && pwd)
root=${SCARLETT_ROOT:-$(cd "$src/../../.." && pwd)}
tool=${1:?pass the release-agent synthetic scratch tool}
out=$(mktemp -d "${TMPDIR:-/tmp/opencode}/control-proof.XXXXXXXX")
mkdir "$out/game"
headers=()
for h in /nix/store/*-mcfgthread-x86_64-w64-mingw32-2.5.1-dev/include; do
    if [[ -f "$h/mcfgthread/gthr.h" ]]; then headers=(-isystem "$h"); break; fi
done
count=3
if [[ ${2:-} == unqualified ]]; then headers+=(-DTEST_UNQUALIFIED); count=1; fi
x86_64-w64-mingw32-g++ "${headers[@]}" -std=c++17 -Wall -Wextra -Werror -O2 -static \
    -I"$root/scarlett/native/xmem" "$src/test.cpp" -ld3d12 -o "$out/game/game_scarlett_master.exe"
TMPDIR="$out" "$root/scarlett/test-launch-nixos.sh" "$out/game" "$tool"
log=("$out"/scarlett-run.*/logs/steam-999001.log)
grep '^PASS ' "${log[0]}"
test "$(grep -c '^PASS ' "${log[0]}")" = "$count"
