#!/usr/bin/env bash
set -euo pipefail
umask 077
builder=$(realpath "${1:?usage: find-treelets.sh XboxBvhBuilder.dll [installed-tool]}")
shift
hash=$(sha256sum <"$builder")
[[ ${hash%% *} == 99f803ac22c01ef329dddb1d40be67238d2a812d01e8d040b3cb5135bc23707e ]] ||
    { echo "Unsupported XboxBvhBuilder.dll build" >&2; exit 1; }
s=$(realpath "$(dirname "$0")/../native")
w=$(mktemp -d "${TMPDIR:-/tmp}/scarlett-treelets.XXXXXXXX")
trap 'rm -rf -- "$w"' EXIT
mkdir "$w/game"
for src in test-find-treelets-device queue/queue; do
    x86_64-w64-mingw32-gcc -std=c11 -Os -Wall -Wextra -Werror \
        -DSCARLETT_DEVICE_BOOTSTRAP -DSCARLETT_STAGED_PLACED -c "$s/$src.c" -o "$w/${src##*/}.o"
done
x86_64-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -Werror -static \
    "$s/test-find-treelets.cpp" "$w/test-find-treelets-device.o" "$w/queue.o" -ld3d12 -o "$w/game/game_scarlett_master.exe"
dd if="$builder" of="$w/game/find.bin" bs=64K iflag=skip_bytes,count_bytes skip=$((0xae100)) count=$((0x4dd8)) status=none
TMPDIR="$w" SCARLETT_TEST_GPU="${SCARLETT_TEST_GPU:-radv}" "$s/../test-launch-nixos.sh" "$w/game" "$@" | tee "$w/result"
run=$(sed -n 's/^Test output (including prefix): //p' "$w/result")
log=$run/logs/steam-999001.log
grep '^PASS ' "$log"
[[ $(grep -c '^PASS staged factory' "$log") == 1 && $(grep -c '^PASS linked factory' "$log") == 1 ]]
[[ $(grep -c 'fenced bytes including every guard' "$log") == 6 ]]
grep -q '^PASS FindTreelets without private window rejected' "$log"
