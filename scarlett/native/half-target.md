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
The exact 720p `+c8` query uses the qualified RT host size. In the same run
`+1edb1c1` requested the full-resolution 2560×1440 texture, guest flags
`0x300005`. Its allocation stack goes through `+34bff6`, unlike the 720p
owners. `+360` (return `+abfe0d`) queried this same descriptor with offset
zero and matching size/alignment. `+428` (return `+193e2db`) then requested
placement at `0xd5a200000`, offset `0xf000000` into the original WC section
(base `0xd4b200000`, span `0x89700000`), resource IID, guest state 3 and a
zero color. The factory failed with `E_NOTIMPL` and returned no resource.
Consequently no retail view or GPU use is established. The constructor would
store its result at global RVA `+7665b50`, then build a child at `+7665b58`;
these are disassembled continuations, not successful retail GPU allocations.

In the undelayed dual-GPU runs, the stack at `+cc2834` included the return
to this 1440p constructor at `+1edb1c6`. The title took the breakpoint after
its texture creation helper returned null (`+cc27e9`). Both logs separately
show completion of the first primary clear. The failed texture creation
does not by itself establish that either adapter ran out of memory.

In separate placement experiments the game constructed the 1440p texture
and its SRV, RTV and UAV on both adapters. Its next query described a
1280×720 `R32_UINT` texture with layout `0x102` and flags `0x300004`.
Its 160×90 tiles each hold 8×8 four-byte words: `0x384000` bytes at
`0x100` alignment. RTX optimal image allocation requires `0x3c0000`
bytes and would exceed the guest allocation. The title subsequently requested
placement at WC offset `0x10e00000` on RTX, state `3`, no clear value.
The [original-memory test](../tests/rtx-r32-tile.sh) reads and writes every
word using a buffer and explicit tile addressing after exact VA reuse.
It does not exercise a retail texture view.

Only these two size probes and the observed 1440p detail probe are enabled.
The latter uses a host RT+UAV optimal descriptor; nothing here maps a WC
section, changes `+428`, chooses an initial state, or interprets high flags.

Host control: two optimal single-sample RTs in the same DEFAULT RT/DS-only
heap at byte offsets zero and the queried size. Clear both, clear an interior
rectangle of the second, transition to COPY_SOURCE, then fence and read every
half-float channel of both. RTX 3070/595.112.07: size `0x780000`; RADV 26.3.0:
`0x790000`. Both report alignment `0x10000` and a 10240-byte *copy* row pitch.
At 2560×1440, RTX reported `0x1e00000` and RADV `0x1ce0000`, alignment
`0x10000`, copy pitch 20480. Both placements and all pixels passed on both
GPUs. The copy footprint is not the image's internal layout.

With a staged tool, from the project root:

```sh
w=$(mktemp -d); mkdir "$w/game"
for src in d3d12_device d3d12_vm queue/queue; do
    x86_64-w64-mingw32-gcc -Os -Wall -Wextra -Werror -DSCARLETT_DEVICE_BOOTSTRAP -DSCARLETT_STAGED_PLACED \
        -c "scarlett/native/$src.c" -o "$w/${src##*/}.o"
done
x86_64-w64-mingw32-g++ -std=c++17 -Os -Wall -Wextra -Werror -static \
    scarlett/native/test-half-target.cpp "$w"/*.o -ld3d12 -ldxguid -o "$w/game/game_scarlett_master.exe"
TMPDIR="$w" SCARLETT_TEST_GPU=nvidia scarlett/test-launch-nixos.sh "$w/game" "$tool"
TMPDIR="$w" SCARLETT_TEST_GPU=radv SCARLETT_TEST_DISPLAY=sway scarlett/test-launch-nixos.sh "$w/game" "$tool"
grep 'PASS .*RGBA16_FLOAT'  "$w"/scarlett-run.*/logs/steam-*.log
```
