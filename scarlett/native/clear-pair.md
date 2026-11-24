# Guest graphics stream +2b0: one clear pair

The retail call at `+0x19f51f7` prewarms this pipeline. In a controlled
test, the title's own `+0x122630` routine wrote 100 bytes for three
position/color vertices, bound one render target and no depth target, and
requested `DrawInstanced(3,1,0,0)`. The worker's submission path passes a
token and flags `3` to queue `+0x118`. No retail trace yet links the
prewarmed PSO to a submitted list or a completed draw.

The first observed call (return RVA `2479a8d`) uses a 688-byte stream.
After clearing the root, VS and PS pointers, SHA-256 covers every tag, size,
padding byte and all fixed state; the only active target is RGBA8_UNORM,
no depth, no blend, one sample, triangle topology. Capture shader spans are
992/964 bytes; both digests cover the entire native-only DXBC, not a stage
name or a DXBC header hash. The wrapper snapshots all three inputs, retains
the matching live same-device root and submits a private stream with host
root and translated DXIL to `ID3D12Device2::CreatePipelineState`. Only a
successful host creation returns a guest PSO. The wrapper does not implement
graphics command-list binding or decode a guest draw packet.

**Shader mapping.** VS PSV has one raw `t0,space3`; native row
`(0x100,0,0,1,0x804)`. The VS forms a raw buffer descriptor from s8:s9,
then `s_buffer_load_dword` at ISA `+38/+80` reads header words at byte
0/12. Each block masks the low nibble and skips its vector load on tag
mismatch (3 for position and 4 for color). The effective byte address is
`((header >> 7) & 0x1fffffc) + VertexID * ((header >> 3) & 60)`, using
32-bit arithmetic. `buffer_load_dwordx3/x4` at `+78/+c8` set position XYZ
and PARAM0.xyzw respectively; skipped paths leave zero, position W is one.
PS PSV interpolation mode 1 and four `v_interp_mov_f32 p0` instructions
select the leading vertex's PARAM0. Two `v_cvt_pkrtz_f16_f32` instructions
pack RG/BA before the compressed export. `clear_pair.hlsl` implements both
operations on actual loaded data.

`clear_pair_dxil.h` contains just the host bytecode. Rebuild each stage with
DXC `1.9(dev;4950-b106a961)` using
`dxc -Ges -Gis -O3 -Qstrip_debug -Qstrip_reflect -T ${stage}_6_0 -E $stage -Fo $stage.dxil clear_pair.hlsl`.
Use the resulting file bytes in the `clear_vs_dxil` and `clear_ps_dxil` arrays.

The second prewarm request changes just the active RTV to `RGBA8_UNORM_SRGB`.
Its checked PS passes half-rounded RGB to the attachment for sRGB conversion;
rounding RGB to linear bytes first would erase dark colors. Alpha stays
linear. Build `clear_srgb_dxil.h` with the same flags and `-T ps_6_0 -E ps_srgb`.
In the fenced fixture both GPUs stored red byte 3 for linear red `1/1024`
and alpha byte 128 for `0.5`. In the RTX retail trace the first two PSOs
were created. No retail draw using either one was established.

**GPU test.** Put the local `stream.bin`, `vs.bin`, `ps.bin` captures in
a scratch `game` directory. Build from the patched tree:

```sh
n=/path/to/scarlett/native
python3 "$n/../tests/clear-fallback/prepare.py" /path/to/pinned/game_scarlett_master.exe game/emitter.bin
flags=(-DSCARLETT_DEVICE_BOOTSTRAP -DSCARLETT_STAGED_PLACED -Os -Wall -Wextra -Werror)
flags+=(-DSCARLETT_EXPERIMENTAL_TAGGED_BATCH -DSCARLETT_EXPERIMENTAL_PRIMARY_RELEASE -DSCARLETT_EXPERIMENTAL_DIRECT_WAIT -DSCARLETT_EXPERIMENTAL_BRIDGE)
for f in "$n/test-clear-device.c" "$n/queue/test-clear4-bind.c" "$n/d3d12_vm.c"; do
  x86_64-w64-mingw32-gcc "${flags[@]}" -c "$f" -o "$(basename "$f" .c).o"
done
x86_64-w64-mingw32-g++ -std=c++17 -Wall -Wextra -Werror -Os -static \
  "$n/test-clear-pipeline.cpp" test-clear-device.o test-clear4-bind.o d3d12_vm.o \
  /path/to/scratch-tool/scarlett/xmem.dll -lbcrypt -ld3d12 -ldxgi -ldxguid -o game/game_scarlett_master.exe
TMPDIR="$PWD" SCARLETT_TEST_DISPLAY=sway SCARLETT_TEST_GPU=radv \
  "$n/../test-launch-nixos.sh" game /path/to/scratch-tool
```

**Clear #4 source timing control (cross-compiled only).** The clear-fallback
writer produces the same 100-byte record twice in one continuously mapped
`8404` generation. The fixture snapshots the first record for the left half
and records a bounded GPU copy from `ImportedReadSpan` for the right half.
After submission, while the Direct queue is fence-blocked, the writer changes
the color in place. After release the readback must be `20 60 9f df` on the
left and `df 9f 60 20` on the right, with both bound DSV planes unchanged
at every pixel. These are two explicit fixture policies, not evidence of when
the retail clear consumes its source.

The fixture replaces the process-specific pointers, calls the guest factory,
and draws using the returned PSO's host state. A nonzero root VA offset, two
strided attribute arrays, distinct provoking/nonprovoking colors and analytic
triangle boundaries check byte addressing and flat color. Two channels cross
UNORM thresholds if converted directly or via nearest half instead of RTZ.
Three fenced readbacks check every channel (384 pixels total), including black
with the wrong color tag and no coverage with the wrong position tag.

**UNORM export.** After half-RTZ, clamp to [0,1] and select the nearest
byte with ties to even. Multiplication by 255 is exact in float32 for
clamped half values. The only half exactly halfway between byte codes is
0.5: 127.5 rounds to 128. Returning the selected byte divided by 255 puts
the host export away from the rounding boundary. This relies on the pinned
RGBA8_UNORM target and disabled blending.

The unchanged 384-pixel fixture passes on RADV and RTX 3070 (use
`SCARLETT_TEST_GPU=nvidia SCARLETT_TEST_DISPLAY=xvfb` for RTX). A separate
128×128 draw supplied four successive binary16 encodings per pixel, widened
to float32 (NaNs canonicalized). The original RADV shader and the patched
shader on both GPUs produced identical bytes for all 65,536 channels. An
independent CPU oracle picked the closest byte by exact rational distance,
clamping out-of-range inputs and mapping NaN to zero; it matched every byte.
