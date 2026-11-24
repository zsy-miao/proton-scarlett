#!/usr/bin/env bash
set -euo pipefail
umask 077
builder=$(realpath "${1:?usage: bvh4.sh XboxBvhBuilder.dll [installed-tool]}")
shift
hash=$(sha256sum <"$builder")
[[ ${hash%% *} == 99f803ac22c01ef329dddb1d40be67238d2a812d01e8d040b3cb5135bc23707e ]] ||
    { echo "Unsupported XboxBvhBuilder.dll build" >&2; exit 1; }
s=$(realpath "$(dirname "$0")/../native")
w=$(mktemp -d "${TMPDIR:-/tmp}/scarlett-bvh4.XXXXXXXX")
trap 'rm -rf -- "$w"' EXIT
mkdir "$w/game"
for src in test-bvh4-device queue/queue; do
    x86_64-w64-mingw32-gcc -std=c11 -Os -Wall -Wextra -Werror \
        -DSCARLETT_DEVICE_BOOTSTRAP -DSCARLETT_STAGED_PLACED -c "$s/$src.c" -o "$w/${src##*/}.o"
done
x86_64-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -Werror -static \
    "$s/test-bvh4.cpp" "$w/test-bvh4-device.o" "$w/queue.o" -ld3d12 -o "$w/game/game_scarlett_master.exe"
while read -r name off size; do
    dd if="$builder" of="$w/game/$name" bs=64K iflag=skip_bytes,count_bytes skip=$((off)) count=$((size)) status=none
done <<'SLICES'
PartitionConstruct.bin 0x45c80 0x5e60
Construct.bin 0x53fd0 0x7160
Refit.bin 0x4bae0 0x84e4
SLICES
TMPDIR="$w" SCARLETT_TEST_GPU="${SCARLETT_TEST_GPU:-radv}" "$s/../test-launch-nixos.sh" "$w/game" "$@" | tee "$w/result"
run=$(sed -n 's/^Test output (including prefix): //p' "$w/result")
log=$run/logs/steam-999001.log
grep '^PASS ' "$log"
[[ $(grep -c '^PASS staged factory' "$log") == 3 && $(grep -c '^PASS linked factory' "$log") == 3 ]]
[[ $(grep -c 'fenced bytes including every guard' "$log") == 9 ]]
grep -q '^PASS Refit without private window rejected' "$log"
