#!/usr/bin/env bash
set -euo pipefail
umask 077
builder=$(realpath "${1:?usage: hierarchy-pipelines.sh XboxBvhBuilder.dll [installed-tool]}")
shift
hash=$(sha256sum <"$builder")
[[ ${hash%% *} == 99f803ac22c01ef329dddb1d40be67238d2a812d01e8d040b3cb5135bc23707e ]] ||
    { echo "Unsupported XboxBvhBuilder.dll build" >&2; exit 1; }
s=$(realpath "$(dirname "$0")/../native")
w=$(mktemp -d "${TMPDIR:-/tmp}/scarlett-hierarchy.XXXXXXXX")
trap 'rm -rf -- "$w"' EXIT
mkdir "$w/game"
for src in test-bvh-hierarchy-device queue/queue; do
    x86_64-w64-mingw32-gcc -std=c11 -Os -Wall -Wextra -Werror \
        -DSCARLETT_DEVICE_BOOTSTRAP -DSCARLETT_STAGED_PLACED -c "$s/$src.c" -o "$w/${src##*/}.o"
done
x86_64-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -Werror -static \
    "$s/test-bvh-hierarchy.cpp" "$w/test-bvh-hierarchy-device.o" "$w/queue.o" -ld3d12 -o "$w/game/game_scarlett_master.exe"
while read -r name off size; do
    dd if="$builder" of="$w/game/$name.dxbc" bs=64K iflag=skip_bytes,count_bytes skip=$((off)) count=$((size)) status=none
done <<'SLICES'
split0 0x5d1d0 0x1e68
split1 0x5b130 0x2094
inner64 0x3c530 0x2094
SLICES
SCARLETT_TEST_GPU="${SCARLETT_TEST_GPU:-radv}" "$s/../test-launch-nixos.sh" "$w/game" "$@" | tee "$w/result"
run=$(sed -n 's/^Test output (including prefix): //p' "$w/result")
grep '^PASS both exact hierarchy factories, owners and root schemas' "$run/logs/steam-999001.log"
[[ $(grep -c '^PASS hierarchy .* output and guards' "$run/logs/steam-999001.log") == 14 ]]
