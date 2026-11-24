#!/usr/bin/env bash
set -euo pipefail
umask 077
builder=$(realpath "${1:?usage: bitonic-pipelines.sh XboxBvhBuilder.dll [installed-tool]}")
shift
hash=$(sha256sum <"$builder")
[[ ${hash%% *} == 99f803ac22c01ef329dddb1d40be67238d2a812d01e8d040b3cb5135bc23707e ]] ||
    { echo "Unsupported XboxBvhBuilder.dll build" >&2; exit 1; }
s=$(realpath "$(dirname "$0")/../native")
w=$(mktemp -d "${TMPDIR:-/tmp}/scarlett-bitonic.XXXXXXXX")
trap 'rm -rf -- "$w"' EXIT
mkdir "$w/game"
while read -r name offset size; do
    dd if="$builder" of="$w/game/$name.dxbc" bs=64K iflag=skip_bytes,count_bytes skip=$((offset)) count=$((size)) status=none
done <<'SLICES'
pre 0x43bb0 0x20c8
outer 0x407e0 0x1468
inner 0x41c50 0x1f5c
pre64 0x3e5d0 0x2204
outer64 0x3afd0 0x1554
inner64 0x3c530 0x2094
SLICES
for fixture in factory sort; do
    x86_64-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -Werror -static "$s/test-bvh-bitonic-$fixture.cpp" \
        -ld3d12 -ldxguid -o "$w/game/game_scarlett_master.exe"
    SCARLETT_TEST_GPU="${SCARLETT_TEST_GPU:-radv}" "$s/../test-launch-nixos.sh" "$w/game" "$@" | tee "$w/result"
    run=$(sed -n 's/^Test output (including prefix): //p' "$w/result")
    grep '^PASS' "$run/logs/steam-999001.log"
    if [[ $fixture == factory ]]; then
        [[ $(grep -c '^PASS exact public +1f0' "$run/logs/steam-999001.log") == 6 ]]
    else
        [[ $(grep -c '^PASS bitonic .* guards and payloads' "$run/logs/steam-999001.log") == 14 ]]
    fi
done
