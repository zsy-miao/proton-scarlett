# 1×1 RGBA8_UNORM 4× render target

Both adapters saw node 1, count 1, Texture2D, alignment 0, one layer and mip,
samples 4/quality 0, unknown layout and render-target flag 1. `+c8` (return
`344188`) and `+360` (return `abfe0d`) pass the aggregate destination before
This. The latter also receives a per-resource output. An `ID3D12Device4`
query returns 4 MiB size and alignment, and `{offset 0, alignment 4 MiB,
size 4 MiB}`. Both interfaces check the exact descriptor and host answer.

At `+428` (return `193e2db`), the operation requests `IID_ID3D12Resource`,
state argument 6 and an RGBA8 optimized clear value of (1,1,1,1). On RADV
the address was `da2e00000`, on RTX `d59a00000`; in both, the original
2199 MiB marked WC pool contained the full 4 MiB slot at offset `e800000`.
The existing native WC heap denies RT/DS textures. A detached committed
texture would incorrectly leave that slot unchanged.

**RTX 3070 / 595.112.07:** the optimal external RGBA8 image supports 4× with
non-dedicated DMA-BUF import. Its memory requirement is 512 bytes/1024
alignment, bits `0x3`. The original exported WC buffer uses type 3; re-import
of its fd permits bits `0x11`, hence the image import uses type 0. The
private Wine selector checks the actual fd properties, allocation size and WC
tag. The image binds at the original slot offset without zeroing the memory.
The resource retains its import and section identity; overlapping live slices
of the same allocation are rejected. An optimized clear value does not issue
a clear. Unsupported title consumers still reject the wrapper. The observed
render-target workload in the fixture starts in desktop RENDER_TARGET state;
this does not establish a general guest state mapping.

**RADV 26.3.0:** linear and optimal external RGBA8 image queries for this
usage returned `VK_ERROR_FORMAT_NOT_SUPPORTED`; each advertised RGBA8 DRM
modifier reported only sample count 1. Placement returns
`DXGI_ERROR_UNSUPPORTED` and null; the original slot remains unchanged.

The fixture writes `SV_SampleIndex`-dependent colors, fences four separate
`Texture2DMS.Load` results and a `ResolveSubresource`, and checks neighboring
red and green placements. On RTX the tested middle image is the one created
by the public guest `+428`. The guest wrapper has already been released and
the exact title slot VA reused by an anonymous mapping. A separate retained
WC alias contains the sample DWORDs at offsets 0, 4, 16 and 20. Exactly 16
bytes change in the 4 MiB slice; neighboring guards and the replacement
remain intact. RADV renders the same fixture in an ordinary RT-capable heap;
this is a reference test, not successful guest placement on RADV.

Unchanged retail reached a further, GPU-specific boundary:
* RTX: `+428` succeeds; device `+90` writes a view of the same wrapper,
  format 28/dimension 6 (Texture2DMS), mapping `0x1688`, return `164f01`.
  Retail then issues the `+c8` depth query, also returning to `344188`.
* RADV: the caller tolerates the color placement failure and reaches `+c8`
  for 4× format 19 (R32G8X24_TYPELESS), flags 2, return `344188`.
For the subsequent depth queries and placement see [msaa-depth.md](msaa-depth.md).
No retail draw or resolve was observed. On RTX the fixture binds the visible
slot written through `+90` for all four Texture2DMS loads. It releases the
wrapper and direct texture references, checks the exact slice is still busy,
then overwrites the slot after the fence and checks that placement succeeds
again. Malformed records, a mismatched 2D view, an unregistered resource,
a misaligned slot, a truncated prefix and a modified slot tag are refused.

## Reproduce

Apply Wine `0012` after `0011`, vkd3d `0026` after `0025` and build the
matched x64 win32u.so, d3d12core.dll and staged d3d12_xs.dll. Use a scratch
tool. In `scarlett/native`, compile `test-msaa-color.hlsl` three times with
DXC `-WX -E STAGE -T STAGE_6_0 -Fo STAGE.dxil` for `vs`, `ps`, `cs`.
Compile `test-msaa-device.c`, `queue/queue.c`, `d3d12_vm.c` using the
build-runtime.sh staged-device defines. Link their objects and
`test-msaa-color.cpp` using x64 MinGW `-std=c++17 -Os -Wall -Wextra -Werror
-static -ld3d12`. Set `-DTEST_NV_IMPORT` for the qualified RTX configuration;
the RADV build requires explicit refusal. Place the shaders beside the test
executable named `game_scarlett_master.exe`, and run the private launch harness.
