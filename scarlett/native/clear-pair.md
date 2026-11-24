# Guest graphics stream +2b0: one clear pair

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

**AMD test.** Put the local `stream.bin`, `vs.bin`, `ps.bin` captures in
a scratch `game` directory. Build from the patched tree:

```sh
n=/path/to/scarlett/native
flags=(-DSCARLETT_DEVICE_BOOTSTRAP -DSCARLETT_STAGED_PLACED -Os -Wall -Wextra -Werror)
for f in "$n/test-clear-device.c" "$n/queue/queue.c" "$n/d3d12_vm.c"; do
  x86_64-w64-mingw32-gcc "${flags[@]}" -c "$f" -o "$(basename "$f" .c).o"
done
x86_64-w64-mingw32-g++ -std=c++17 -Wall -Wextra -Werror -Os -static \
  "$n/test-clear-pipeline.cpp" test-clear-device.o queue.o d3d12_vm.o \
  -ld3d12 -o game/game_scarlett_master.exe
TMPDIR="$PWD" SCARLETT_TEST_DISPLAY=sway SCARLETT_TEST_GPU=radv \
  "$n/../test-launch-nixos.sh" game /path/to/scratch-tool
```

The fixture replaces the process-specific pointers, calls the guest factory,
and draws using the returned PSO's host state. A nonzero root VA offset, two
strided attribute arrays, distinct provoking/nonprovoking colors and analytic
triangle boundaries check byte addressing and flat color. Two channels cross
UNORM thresholds if converted directly or via nearest half instead of RTZ.
Three fenced readbacks check every channel (384 pixels total), including black
with the wrong color tag and no coverage with the wrong position tag.

**RTX 3070:** The same fixture created the PSO but failed the strict RADV
pixel oracle. For the first triangle, the input green value was exactly 0.5;
21 pixels read back green 127 instead of 128. A diagnostic comparison found
no other byte differences across the three draws. RTX retail has not reached
this PSO.
