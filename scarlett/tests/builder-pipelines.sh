#!/usr/bin/env bash
# Compile the adapter once and run the GPU fixtures against a local copy of the builder.
set -euo pipefail
umask 077
builder=$(realpath "${1:?usage: builder-pipelines.sh XboxBvhBuilder.dll [installed-tool]}")
shift
hash=$(sha256sum <"$builder")
[[ ${hash%% *} == 99f803ac22c01ef329dddb1d40be67238d2a812d01e8d040b3cb5135bc23707e ]] ||
    { echo "Unsupported XboxBvhBuilder.dll build" >&2; exit 1; }
s=$(realpath "$(dirname "$0")/../native")
w=$(mktemp -d "${TMPDIR:-/tmp}/scarlett-builder.XXXXXXXX")
trap 'rm -rf -- "$w"' EXIT
mkdir "$w/game"
for src in d3d12_device queue/queue; do
    x86_64-w64-mingw32-gcc -std=c11 -Os -Wall -Wextra -Werror \
        -DSCARLETT_DEVICE_BOOTSTRAP -DSCARLETT_STAGED_PLACED -c "$s/$src.c" -o "$w/${src##*/}.o"
done
while read -r name offset size; do
    dd if="$builder" of="$w/game/private-$name.bin" bs=64K iflag=skip_bytes,count_bytes \
        skip=$((offset)) count=$((size)) status=none
done <<'SLICES'
builder 0xc8ab0 0x1410
offline 0xc6be0 0x1ec4
partitions 0xc5660 0x1580
instances 0xc0d20 0x493c
reset 0xb76e0 0xfe8
index 0xb3fd0 0x1004
SLICES
for fixture in pipeline partitions instances reset; do
    x86_64-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -Werror -static \
        "$s/test-bvh-$fixture.cpp" "$w/d3d12_device.o" "$w/queue.o" -ld3d12 \
        -o "$w/game/game_scarlett_master.exe"
    SCARLETT_TEST_GPU="${SCARLETT_TEST_GPU:-radv}" "$s/../test-launch-nixos.sh" "$w/game" "$@" | tee "$w/result"
    run=$(sed -n 's/^Test output (including prefix): //p' "$w/result")
    grep '^PASS' "$run/logs/steam-999001.log"
done
