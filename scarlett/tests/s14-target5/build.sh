#!/usr/bin/env bash
set -euo pipefail
[[ $# == 2 && ($1 == fence || $1 == dispatch || $1 == queued4 || $1 == layouts) ]] || {
    echo "usage: bash build.sh {fence|dispatch|queued4} BANK3_OUTPUT | layouts COMBINED_OUTPUT" >&2; exit 2;
}
mode=$1
here=$(cd "$(dirname "$0")" && pwd)
base=$(realpath "$2")
check(){ sha256sum --strict -c "$here/$1.sha256"; }
if [[ $mode == layouts ]]; then (cd "$base" && check combined-preimages); else (cd "$base" && check preimages); fi
out=$(mktemp -d "${TMPDIR:-/tmp}/s14-$mode.XXXXXXXX")
echo "Build directory: $out"
cd "$out"
n=scarlett/native
mkdir -p scarlett obj rtx
cp -RL "$base/$n" "$n"
ln -s "$base/inputs" inputs
apply(){ patch --batch --forward --fuzz=0 -p1 < "$here/$1.patch"; }
cc=x86_64-w64-mingw32-gcc
f=(-Os -Wall -Wextra -Werror -DSCARLETT_DEVICE_BOOTSTRAP -DSCARLETT_STAGED_PLACED
   -DSCARLETT_EXPERIMENTAL_DIRECT_WAIT -DSCARLETT_EXPERIMENTAL_PRIMARY_RELEASE
   -DSCARLETT_EXPERIMENTAL_TAGGED_BATCH -DSCARLETT_EXPERIMENTAL_BRIDGE -DSCARLETT_PAIR_SUBMIT
   -DXS_IM96_FACTORY -DXS_RTX_RETURNED_TABLE -DXS_RTX_ATTRIBUTED_FIVE -Iinputs -DXS_TRACE_FRESH_PUBLICATION)
fixture(){
    "$cc" "${f[@]}" -c "$n/queue/$1.c" -o "obj/$3.o"
    "$cc" "${f[@]}" -DTEST_DISTINCT_PRIMARY -DTEST_CARRIED_TARGET "${@:5}" -static -Wl,--no-insert-timestamp \
        "$n/$2.c" "obj/$3.o" inputs/d3d12_vm.o -ld3d12 -ldxgi -ldxguid -o "$4/game_scarlett_master.exe"
}
provider(){
    local dir=$1
    "$cc" "${f[@]}" -c "$n/d3d12_device.c" -o "$dir/device.o"
    "$cc" "${f[@]}" -c "$n/queue/queue.c" -o "$dir/queue.o"
    "$cc" -shared -s -Wl,--no-insert-timestamp,--image-base,0x24d560000 \
        "$dir/device.o" "$dir/queue.o" inputs/d3d12_vm.o "$n/d3d12_xs.def" -ld3d12 -ldxgi -o "$dir/d3d12_xs.dll"
}
if [[ $mode == layouts ]]; then
    mkdir off on original fixture
    f+=(-DXS_TRACE_S14_SUCCESSOR -DXS_RTX_S14_JOB -DXS_RTX_S14_QUEUED4 -DXS_AMD_RETURNED_FIVE)
    apply alt-cpu
    provider off
    cmp off/d3d12_xs.dll "$base/combined/d3d12_xs.dll"
    f+=(-DXS_RTX_S14_ALT_CPU)
    provider on
    fixture test-job-entry test-job-entry queue original -DTEST_SR_REUSE
    apply fixture-r2
    fixture test-job-entry test-job-entry queue fixture -DTEST_SR_REUSE
    sha256sum -c <<'EOF'
a694bc9d95c34a0832a47ba365ea3028d5e185703e08089e5a537859923e48f7  on/d3d12_xs.dll
ccc684b5854f8b0e06e4072dfe62f18621313ffb81e8651426dd579bffbe8cc0  original/game_scarlett_master.exe
baeccf0bfd4e33699bfe2d7d6880852142a63cd71dd290d401b605f0afcaf7c3  fixture/game_scarlett_master.exe
EOF
    exit 0
fi
cp "$n/test-s14-release.c" "$n/test-s14-target5.c"
apply target5
check target5-sources
fixture test-s14-ledger test-s14-target5 bank3-queue rtx
echo 'c3d49f74bef64d83ac9e46f8a1449ecd5790b9a9248f28985b539f6b70aa03c3  rtx/game_scarlett_master.exe' | sha256sum -c -
if [[ $mode == fence ]]; then exit 0; fi

apply observer
check dispatch-preimages
apply dispatch
cp "$n/test-s14-target5.c" "$n/test-job-entry.c"
cp "$n/queue/test-s14-ledger.c" "$n/queue/test-job-entry.c"
cp "$n/queue/test-s14-bank3.c" "$n/queue/test-s14-job.c"
apply dispatch-fixture
check dispatch-sources
mkdir off on fixture
f+=(-DXS_TRACE_S14_SUCCESSOR)
for dir in off on; do
    if [[ $dir == on ]]; then f+=(-DXS_RTX_S14_JOB); fi
    provider "$dir"
done
fixture test-job-entry test-job-entry fixture-queue fixture -DTEST_SR_REUSE
sha256sum -c <<'EOF'
041095b6518809419446ecd1d26a529ef96773efe9231d6d2f807083990c3b83  off/d3d12_xs.dll
df32e6f18d50c36b8bcc1831f68fa2adc3a9eec94b79a4ad3379063391675452  on/d3d12_xs.dll
49441389f675b6e42dfb0ee0847648f32fc508e269239833a589ec2fdf4d6893  fixture/game_scarlett_master.exe
EOF
if [[ $mode == queued4 ]]; then
    apply queued4
    mkdir queued-off queued-on queued-fixture
    provider queued-off
    cmp queued-off/d3d12_xs.dll on/d3d12_xs.dll
    f+=(-DXS_RTX_S14_QUEUED4)
    provider queued-on
    fixture test-job-entry test-job-entry fixture-queue queued-fixture -DTEST_SR_REUSE
    sha256sum -c <<'EOF'
4998adf03b8fab9e8c5d3de4048f23e4adcfc7e31268db36dc3c317d42cc77e9  queued-on/d3d12_xs.dll
45c41e82f307d9cf6b60ab91af422b67c03dae6015eab864daf6e5952dbdefe1  queued-fixture/game_scarlett_master.exe
EOF
fi
