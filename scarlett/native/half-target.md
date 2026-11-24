# 1280×720 RGBA16_FLOAT allocation probe

RTX retail device `+c8` (return `+344188`) requests one 2D 1280×720
R16G16B16A16_FLOAT texture, node 1, depth/mips/samples 1, quality 0,
alignment/layout 0, guest flags exactly `0xc00001`. In an instrumented
experiment this exact query returned desktop optimal RT allocation info
on RTX, twice. `+1edb10f` and `+1edb14b` are separate calls to the same
constructor with half dimensions. Their returns are stored in globals at
RVAs `+7665be0` and `+7665be8`. Both were initially null and held distinct
title objects by the following full-resolution allocation query. The common
stack passes `+164dee`; it uses the queried size for texture-owner setup.
The `+1df0038` caller links a record containing the owner into a list.
Neither global establishes a native ID3D12Resource identity.

The next query on the same thread was 2560×1440, format 10, flags `0x300005`;
another thread independently rejected an A8 request. No 720p `+428` placed or
`+3f8` committed factory call was observed before fail-fast. No retail heap,
offset, optimized clear, first GPU use, or guest texel layout is qualified.
Do not return the host allocation unconditionally on this evidence.

Host control: two optimal single-sample RTs in the same DEFAULT RT/DS-only
heap at byte offsets zero and the queried size. Clear both, clear an interior
rectangle of the second, transition to COPY_SOURCE, then fence and read every
half-float channel of both. RTX 3070/595.112.07: size `0x780000`; RADV 26.3.0:
`0x790000`. Both report alignment `0x10000` and a 10240-byte *copy* row pitch.
The copy footprint is not the image's internal layout.

With a staged tool, from the project root:

```sh
w=$(mktemp -d); mkdir "$w/game"
x86_64-w64-mingw32-g++ -std=c++17 -Os -Wall -Wextra -Werror -static \
    scarlett/native/test-half-target.cpp -ld3d12 -ldxguid -o "$w/game/game_scarlett_master.exe"
TMPDIR="$w" SCARLETT_TEST_GPU=nvidia scarlett/test-launch-nixos.sh "$w/game" "$tool"
TMPDIR="$w" SCARLETT_TEST_GPU=radv SCARLETT_TEST_DISPLAY=sway scarlett/test-launch-nixos.sh "$w/game" "$tool"
grep 'PASS two 720p' "$w"/scarlett-run.*/logs/steam-*.log
```
