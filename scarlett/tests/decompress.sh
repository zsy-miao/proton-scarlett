#!/usr/bin/env bash
set -euo pipefail
umask 077
builder=$(realpath "${1:?usage: decompress.sh XboxBvhBuilder.dll [installed-tool]}")
shift
hash=$(sha256sum <"$builder")
[[ ${hash%% *} == 99f803ac22c01ef329dddb1d40be67238d2a812d01e8d040b3cb5135bc23707e ]] ||
    { echo "Unsupported XboxBvhBuilder.dll build" >&2; exit 1; }
s=$(realpath "$(dirname "$0")/../native")
w=$(mktemp -d "${TMPDIR:-/tmp}/scarlett-decompress.XXXXXXXX")
trap 'rm -rf -- "$w"' EXIT
mkdir "$w/game"
for src in test-bvh-decompress-device queue/queue; do
    x86_64-w64-mingw32-gcc -std=c11 -Os -Wall -Wextra -Werror -DSCARLETT_DEVICE_BOOTSTRAP -DSCARLETT_STAGED_PLACED \
        -c "$s/$src.c" -o "$w/${src##*/}.o"
done
x86_64-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -Werror -static \
    "$s/test-bvh-decompress.cpp" "$w/test-bvh-decompress-device.o" "$w/queue.o" -ld3d12 -o "$w/game/game_scarlett_master.exe"
while read -r name offset size; do
    dd if="$builder" of="$w/game/$name.bin" bs=64K iflag=skip_bytes,count_bytes skip=$((offset)) count=$((size)) status=none
done <<'SLICES'
header 0x34b20 0x1194
box16 0x28770 0x2e40
box32 0x25c40 0x2b2c
leaf 0x2b5b0 0x9568
SLICES
SCARLETT_TEST_GPU="${SCARLETT_TEST_GPU:-radv}" TMPDIR="$w" "$s/../test-launch-nixos.sh" "$w/game" "$@" | tee "$w/result"
run=$(sed -n 's/^Test output (including prefix): //p' "$w/result")
log=$run/logs/steam-999001.log
grep '^PASS ' "$log"
[[ $(grep -c '^PASS .* decompress factory' "$log") == 8 ]]
[[ $(grep -c '^PASS decompress stage=.* fenced complete arena' "$log") == 12 ]]
