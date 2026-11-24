# 1×1 R32G8X24_TYPELESS, 4× depth/stencil on RADV

On the Raphael/Mendocino RADV 26.3.0 host, the retail `+c8` (return
`344188`) and `+360` (return `abfe0d`) requests have node/count 1, one
layer and mip, sample quality 0, alignment/layout 0, format 19, and flags
2. `ID3D12Device4::GetResourceAllocationInfo1` returns size/alignment
**4 MiB/4 MiB** and per-resource offset 0 with the same size/alignment.

The native Vulkan format is `D32_SFLOAT_S8_UINT`. A 1×1, 4× optimal image
with sampled, depth/stencil attachment and transfer src/dst usage requires
128 KiB, 64 KiB alignment, memory type bits `0x7ad`, no dedicated
allocation. Those are Vulkan requirements, not D3D12 placement results.
With DMA-BUF external memory the linear and optimal format queries both
return `VK_ERROR_FORMAT_NOT_SUPPORTED`; this format has no DRM modifiers.
The existing WC D3D12 heap explicitly denies RT/DS textures.

Retail places the depth image at `+428` (return `193e2db`), with state
argument 6, `IID_ID3D12Resource`, and a typed `D32_FLOAT_S8X24_UINT`
clear value of depth/stencil 0/0. The rejected color and depth placements
use the same 4 MiB slot at offset `0xe800000` in the `0x89700000`-byte
marked WC view. The depth path validates the request, closes the acquired
section handle and returns `DXGI_ERROR_UNSUPPORTED` and null. It creates no
detached resource. With the game mounted read-only, retail then raises a
breakpoint at title RVA `cc2936` (exit 3). No retail depth draw was observed.

## Reproduce

Use the matched runtime described in `msaa-color.md`. Compile the staged
`d3d12_device.c`, `queue/queue.c`, and `d3d12_vm.c`. Link their objects to
`test-msaa-depth.cpp` with x64 MinGW `-std=c++17 -Os -Wall -Wextra -Werror
-static -ld3d12`. Compile `test-msaa-depth.hlsl` for `vs`, `ps`, `cs`
using DXC `-WX -E STAGE -T STAGE_6_0 -Fo STAGE.dxil`. Place all three shaders
beside the fixture named `game_scarlett_master.exe`; run `test-launch-nixos.sh`
with the scratch tool, `SCARLETT_TEST_GPU=radv` and `SCARLETT_TEST_DISPLAY=sway`.

The fixture checks a bounded wire descriptor and typed clear value, both
allocation slots, the native WC heap refusal, and 64 public placement
refusals with an unchanged section handle count (calibrated with an actual
duplicate). It releases the title mapping and reuses the slot VA; a retained
alias verifies all 4 MiB and both guards remain intact.

Separately it creates three adjacent placed resources on a host RT/DS heap,
clears them and draws four single-sample-mask passes on the middle one.
After a fence, separate depth and stencil `Texture2DMS.Load` views give
`(0.125,0x31), (0.25,0x3c), (0.375,0x47), (0.5,0x52)`. Every sample
on the left remains `(1,0x71)` and on the right `(0.25,0xe6)`.
