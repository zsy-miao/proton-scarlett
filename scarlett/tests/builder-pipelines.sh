#!/usr/bin/env bash
# Compile the adapter once and run the GPU fixtures against a local copy of the builder.
set -euo pipefail
umask 077
builder=$(realpath "${1:?usage: builder-pipelines.sh XboxBvhBuilder.dll [installed-tool]}")
shift
s=$(realpath "$(dirname "$0")/../native")
w=$(mktemp -d "${TMPDIR:-/tmp}/scarlett-builder.XXXXXXXX")
trap 'rm -rf -- "$w"' EXIT
mkdir "$w/game"
cp -- "$builder" "$w/game/private-builder-image.bin"
builder="$w/game/private-builder-image.bin"
hash=$(sha256sum <"$builder")
[[ ${hash%% *} == 99f803ac22c01ef329dddb1d40be67238d2a812d01e8d040b3cb5135bc23707e ]] ||
    { echo "Unsupported XboxBvhBuilder.dll build" >&2; exit 1; }
for src in test-bvh-radix-device queue/queue; do
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
load-instances 0xb97f0 0x752c
reset 0xb76e0 0xfe8
index 0xb3fd0 0x1004
copy-update 0xb4fe0 0x126c
copy-compact 0x24b00 0x1134
copycounters 0xb2ee0 0x10e8
beginpass 0xb86d0 0x1118
headers 0xb6250 0x1488
radix-reset 0x92230 0xfec
radix-count 0x93220 0x1ffc
radix-alloc 0x95220 0x3f38
prefix64 0xa1080 0x1360
prefix128 0x9fd00 0x1380
prefix256 0x9e930 0x13d0
prefix512 0x9d4d0 0x1454
prefix1024 0x9be80 0x1644
prefix2048 0x9a830 0x1644
prefix4096 0x99160 0x16cc
SLICES
for fixture in root-registry pipeline partitions instances load-instances reset copy-update copy-compact copycounters beginpass headers radix prefix radix-alloc; do
    x86_64-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -Werror -static \
        "$s/test-bvh-$fixture.cpp" "$w/test-bvh-radix-device.o" "$w/queue.o" -ld3d12 \
        -o "$w/game/game_scarlett_master.exe"
    SCARLETT_TEST_GPU="${SCARLETT_TEST_GPU:-radv}" "$s/../test-launch-nixos.sh" "$w/game" "$@" | tee "$w/result"
    run=$(sed -n 's/^Test output (including prefix): //p' "$w/result")
    log="$run/logs/steam-999001.log"
    case "$fixture" in
        radix) [[ $(grep -Ec '^PASS (reset|count) case=' "$log") == 8 ]] ;;
        prefix) [[ $(grep -c '^PASS N=' "$log") == 28 ]] ;;
        radix-alloc) [[ $(grep -c '^PASS RadixAlloc case=' "$log") == 10 ]] ;;
    esac
    grep '^PASS' "$run/logs/steam-999001.log"
done
