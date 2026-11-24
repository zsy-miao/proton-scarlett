#!/usr/bin/env bash
set -euo pipefail
umask 077
builder=$(realpath "${1:?usage: load-primitives.sh XboxBvhBuilder.dll [installed-tool]}")
shift
hash=$(sha256sum <"$builder")
[[ ${hash%% *} == 99f803ac22c01ef329dddb1d40be67238d2a812d01e8d040b3cb5135bc23707e ]] ||
    { echo "Unsupported XboxBvhBuilder.dll build" >&2; exit 1; }
s=$(realpath "$(dirname "$0")/../native")
w=$(mktemp -d "${TMPDIR:-/tmp}/scarlett-load-primitives.XXXXXXXX")
trap 'rm -rf -- "$w"' EXIT
mkdir "$w/game"
for src in test-bvh-primitives-device queue/queue; do
    x86_64-w64-mingw32-gcc -std=c11 -Os -Wall -Wextra -Werror \
        -DSCARLETT_DEVICE_BOOTSTRAP -DSCARLETT_STAGED_PLACED -c "$s/$src.c" -o "$w/${src##*/}.o"
done
x86_64-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -Werror -static \
    "$s/test-bvh-primitives.cpp" "$w/test-bvh-primitives-device.o" "$w/queue.o" -ld3d12 -o "$w/game/game_scarlett_master.exe"
while read -r name off size; do
    dd if="$builder" of="$w/game/$name" bs=64K iflag=skip_bytes,count_bytes skip=$((off)) count=$((size)) status=none
done <<'SLICES'
load.bin 0x802d0 0xa304
load.dxbc 0x7e110 0x21b4
reload.bin 0x72f20 0xb1e4
reload.dxbc 0x71350 0x1bc8
SLICES
SCARLETT_TEST_GPU="${SCARLETT_TEST_GPU:-radv}" "$s/../test-launch-nixos.sh" "$w/game" "$@" | tee "$w/result"
run=$(sed -n 's/^Test output (including prefix): //p' "$w/result")
log=$run/logs/steam-999001.log
grep '^PASS ' "$log"
[[ $(grep -c '^PASS staged factory' "$log") == 4 && $(grep -c '^PASS linked factory' "$log") == 4 ]]
[[ $(grep -c '^PASS triangle .* fenced all six complete buffers' "$log") == 14 ]]
[[ $(grep -c '^PASS procedural .* fenced all five complete buffers' "$log") == 12 ]]
