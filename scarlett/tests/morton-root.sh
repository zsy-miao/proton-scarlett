#!/usr/bin/env bash
set -euo pipefail
umask 077
builder=$(realpath "${1:?usage: morton-root.sh XboxBvhBuilder.dll [installed-tool]}")
shift
hash=$(sha256sum <"$builder")
[[ ${hash%% *} == 99f803ac22c01ef329dddb1d40be67238d2a812d01e8d040b3cb5135bc23707e ]] ||
    { echo "Unsupported XboxBvhBuilder.dll build" >&2; exit 1; }
s=$(realpath "$(dirname "$0")/../native")
w=$(mktemp -d "${TMPDIR:-/tmp}/scarlett-morton.XXXXXXXX")
trap 'rm -rf -- "$w"' EXIT
mkdir "$w/game"
for src in d3d12_device queue/queue; do
    x86_64-w64-mingw32-gcc -std=c11 -Os -Wall -Wextra -Werror \
        -DSCARLETT_DEVICE_BOOTSTRAP -DSCARLETT_STAGED_PLACED -c "$s/$src.c" -o "$w/${src##*/}.o"
done
x86_64-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -Werror -static \
    "$s/test-bvh-morton-root.cpp" "$w/d3d12_device.o" "$w/queue.o" -ld3d12 -o "$w/game/game_scarlett_master.exe"
"${DXC:-dxc}" -T cs_6_0 -E main -Fo "$w/game/morton-test.dxil" "$s/test-bvh-morton-root.hlsl"
while read -r name off size; do
    dd if="$builder" of="$w/game/$name.bin" bs=64K iflag=skip_bytes,count_bytes skip=$((off)) count=$((size)) status=none
done <<'SLICES'
morton32 0x8d9e0 0x33a0
morton64 0x8a5e0 0x33fc
reset 0xb76e0 0xfe8
sortroot0 0x43cc4 128
sortroot1 0x4090c 128
sortroot2 0x41d64 128
sortroot3 0x3e6e4 128
sortroot4 0x3b0fc 128
sortroot5 0x3c644 128
SLICES
SCARLETT_TEST_GPU="${SCARLETT_TEST_GPU:-radv}" "$s/../test-launch-nixos.sh" "$w/game" "$@" | tee "$w/result"
run=$(sed -n 's/^Test output (including prefix): //p' "$w/result")
grep '^PASS Bitonic root:' "$run/logs/steam-999001.log"
grep '^PASS Morton root:' "$run/logs/steam-999001.log"
