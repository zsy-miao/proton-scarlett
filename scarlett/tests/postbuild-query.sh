#!/usr/bin/env bash
set -euo pipefail
umask 077
builder=$(realpath "${1:?usage: postbuild-query.sh XboxBvhBuilder.dll [installed-tool]}")
shift
hash=$(sha256sum <"$builder")
[[ ${hash%% *} == 99f803ac22c01ef329dddb1d40be67238d2a812d01e8d040b3cb5135bc23707e ]] ||
    { echo "Unsupported XboxBvhBuilder.dll build" >&2; exit 1; }
s=$(realpath "$(dirname "$0")/../native")
w=$(mktemp -d "${TMPDIR:-/tmp}/scarlett-postbuild.XXXXXXXX")
trap 'rm -rf -- "$w"' EXIT
mkdir "$w/game"
for src in test-bvh-postbuild-device queue/queue; do
    x86_64-w64-mingw32-gcc -std=c11 -Os -Wall -Wextra -Werror -DSCARLETT_DEVICE_BOOTSTRAP -DSCARLETT_STAGED_PLACED \
        -c "$s/$src.c" -o "$w/${src##*/}.o"
done
x86_64-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -Werror -static \
    "$s/test-bvh-postbuild.cpp" "$w/test-bvh-postbuild-device.o" "$w/queue.o" -ld3d12 -o "$w/game/game_scarlett_master.exe"
dd if="$builder" of="$w/game/query.dxbc" bs=64K iflag=skip_bytes,count_bytes skip=$((0x90d80)) count=$((0x14a4)) status=none
SCARLETT_TEST_GPU="${SCARLETT_TEST_GPU:-radv}" TMPDIR="$w" "$s/../test-launch-nixos.sh" "$w/game" "$@" | tee "$w/result"
run=$(sed -n 's/^Test output (including prefix): //p' "$w/result")
log=$run/logs/steam-999001.log
grep '^PASS ' "$log"
[[ $(grep -c '^PASS .* PostBuildInfoQuery factory and negatives' "$log") == 2 ]]
[[ $(grep -c '^PASS query .* all guards' "$log") == 10 ]]
