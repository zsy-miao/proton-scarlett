# Write-combined graphics pool

For [ApexPointMapComputer](../native/bvh-apex.md), apply `0020-bounded-apex.patch` after `0019`. Reuse dxil-spirv through `0011`; run `scarlett/tests/apex.sh` with the matched adapter/core.

For [FindTreelets](../native/find-treelets.md), apply `0019-find-treelets.patch` after `0018`, with dxil-spirv through `0011`. Run `scarlett/tests/find-treelets.sh` with the matched adapter/core.

For [BuildSplits 0 and 1](../native/bvh-hierarchy.md), apply `0017-bounded-hierarchy.patch` after `0016`, with dxil-spirv through `0010`. Rebuild the adapter and core and run `scarlett/tests/hierarchy-pipelines.sh`.

For the [BVH4 construction stages](../native/bvh4.md), apply `0016-bvh4.patch` after `0015` with dxil-spirv through `0010`. Run `scarlett/tests/bvh4.sh` with the rebuilt core and adapter.

For the [four LoadPrimitivesPass PSOs](../native/bvh-load-primitives.md), apply `0015-bounded-load-primitives.patch` after `0014` with dxil-spirv through `0009`. Rebuild the adapter and core together; run `scarlett/tests/load-primitives.sh` against the pinned builder and staged tool.

For [SplitInstancesPass](../native/bvh-split-pass.md), apply `0014-bounded-split-pass.patch` after `0013`; it reuses dxil-spirv `0008`. Rebuild core and adapter together and run `scarlett/tests/split-pass.sh` with the pinned builder and staged tool.

Apply `0012` then `0013` after `0011`, and nested dxil-spirv `0007` then `0008` after `0006`, for [rearrange](../native/bvh-rearrange.md) and the distinct [LoadInstancesPass](../native/bvh-load-pass.md). Rebuild the adapter and core together. Use `scarlett/tests/rearrange-pipelines.sh` and `scarlett/tests/load-pass.sh` with the same arguments as the radix tests.

For the [radix constructor](../native/bvh-radix.md), apply `0011-radix-count.patch` and nested `../dxil-spirv-patches/0006-radix-count.patch` after the Morton/bitonic pair. Rebuild the core and adapter together. `scarlett/tests/builder-pipelines.sh` covers all ten radix PSOs.

For the two Morton and six bitonic pipelines, apply `0010-vkd3d-bounded-morton-bitonic.patch` after `0009` and `../dxil-spirv-patches/0005-bounded-morton-bitonic.patch` after `0004` inside dxil-spirv. Rebuild the core and adapter together. Use `scarlett/tests/morton-root.sh` (set `DXC` to the host compiler) and `scarlett/tests/bitonic-pipelines.sh`, each taking `XboxBvhBuilder.dll [installed-tool]`. The bitonic factory fixture loads the staged adapter; the sort fixture tests the original kernels against a CPU sort. Guest command-list dispatch is outside these fixtures.

For [BeginPass](../native/bvh-beginpass.md) and [InitHeaders](../native/bvh-init-headers.md), apply the incremental vkd3d patches `0008` and `0009` after `0007`, and nested dxil-spirv patches `0003` and `0004` after `0002`, in numeric order. Build the core and adapter together.

For [LoadInstances](../native/bvh-load-instances.md), additionally apply
`0007-vkd3d-bounded-load-instances.patch` after `0006` and nested
`../dxil-spirv-patches/0002-bounded-load-instances.patch` after `0001`.
Build the matching `d3d12core.dll` and native adapter together.

For `WriteInstances`, apply `0006-vkd3d-bounded-builder-memory.patch` after
`0005`, and apply `../dxil-spirv-patches/0001-bounded-builder-memory.patch`
inside vkd3d-proton's `subprojects/dxil-spirv` (`219d770799606f15abb3c57a8a9b9d19c2ae94ca`).
Rebuild both together. The [builder fixture](../native/bvh-write-instances.md)
checks the compiled shader's instance records and adjacent guards.

Patch `0005` adds a private query and factory for the tested RADV linear
3840×2160 RGBA8 primary image. They use the same layout: `0x1fb0000` bytes
per slice, 64 KiB alignment, UAV initial state and a transparent-black
optimized clear value. Creation leaves the caller's bytes untouched.
`test-linear-primary.c` renders, samples and presents the imported images;
compile it like `test-offscreen-present.c` and place its compute shader,
`dxc -T cs_6_0 -E main -Qstrip_debug -Fo image.dxil test-linear-primary.hlsl`,
beside the executable. Check `display.mp4` with `offscreen-pixels.py`.

For the retail descriptor arena, apply all Wine patches through `0007` and all vkd3d-proton patches through `0004` in numeric order. vkd3d patch `0004` supplies the write-back page heap; Wine `0006` and `0007` implement the descriptor mappings and shader-code snapshots. See the [arena layout and tests](../native/xmem/README.md#reservation-and-mapping-milestone).

For RADV 26.3.0 on `1002:164e`, also apply Wine patch `0005` and vkd3d patches `0002` and `0003` after the NVIDIA support patches. Build Wine against recent `libdrm_amdgpu` headers; additionally stage the x64 Windows `kernelbase.dll` alongside the Unix `ntdll.so`, `win32u.so` and x64 `d3d12core.dll` for `install.sh`. Only a qualified AMD control view accepts `VirtualFree(base, 65536, MEM_RELEASE)`. Run `test-control64.cpp` in `scarlett/native/xmem/` with the same build procedure as `test_wc_pages.cpp` and `SCARLETT_TEST_GPU=radv`. For write-back pools, `test_gpu.cpp` with `-DTEST_RADV_WB_RANGE` checks nine sizes from 563 to 1,172 MiB, including the retail 596 MiB request.

The allocator accepts the observed tuple `(NULL, 0x89700000, 0x20003000, 0x283, 0x2404)` on the tested RTX 3070 (PCI `10de:2484`, NVIDIA 595.112.07). The driver reported write-combined system memory. The meaning of flag bit `0x200` and physical page size are unverified.

Apply `scarlett/wine-patches/*.patch` in numeric order to the pinned Wine source. Apply `0001-vkd3d-wc-heap.patch` in this directory to the pinned vkd3d-proton source (`212991fc2c266bc0d59f4c4ce8f80f7126508d71`). Build with Vulkan, GL and DRM support. In the configured Wine build run `make dlls/ntdll/ntdll.so dlls/win32u/win32u.so`; in the 64-bit MinGW vkd3d-proton build run `ninja libs/d3d12core/d3d12core.dll`. Stage all three files by basename alongside the native PE DLLs before running `scarlett/install.sh`.

Wine owns the title's CPU mapping. Acquisition under Wine's VM lock duplicates a handle to the underlying GPU allocation. If the title releases its mapping, an existing GPU consumer keeps its own backing until its queue retires; a fresh lookup cannot acquire the released view. `test_gpu.cpp` with `-DTEST_WC_SECTION` exercises the exact allocation, bidirectional CPU/GPU access, release and address reuse. Link it against `xmem.dll` and `-ld3d12` and run it through `scarlett/test-launch-nixos.sh`. `-DTEST_WC_UNSUPPORTED` in addition tests rejection on a first-pool-only tool.

For [PostBuildInfoQuery](../native/bvh-postbuild.md), apply `0018-bounded-postbuild.patch` after `0017`. No new dxil-spirv patch. Run `scarlett/tests/postbuild-query.sh` with the matched adapter/core.

For the [outer decompressor](../native/bvh-decompress.md), apply `0021-bounded-leaf-rehydration.patch` after Apex `0020` and nested dxil-spirv `0012-bounded-leaf-rehydration.patch` after Treelets `0011`. Only the leaf stage requires those extensions. Use `scarlett/tests/decompress.sh` with the pinned builder and staged tool.

For the [RTX startup control page](../native/xmem/control-page.md), apply
`0022-vkd3d-rtx-cpu-control.patch` after `0021` with Wine patch `0009` and
the matching XMem provider. `ScarlettCpuControlViewV1` returns a CPU mapping
only. On RTX run `test-control-cpu.cpp` and `test-control-import.cpp`; on
RADV run `test-control64.cpp` to check the existing GPU-visible path.

For the RTX 185 MiB descriptor arena, apply `0023` after `0022` with Wine
patch `0010`. Rebuild the x64 graphics core and `win32u.so` together. Run
`test-descriptor-pages.cpp` on RTX and RADV to verify the five maps.

For the RTX primary textures apply `0024` after `0023` and rebuild the x64
core. `test-allocation-info.cpp` exercises the original backing and final
release on both GPUs.

For the RTX write-back page pool, apply `0025` after `0024` with Wine patch
`0011` and the matching XMem provider. The physical-page factory accepts
the pool's 1,024 MiB and 1,116 MiB chunks. `test-page-pool.c` checks all
six retained heaps after the CPU address is reused.

For the [4× MSAA color target](../native/msaa-color.md), apply `0026` after
`0025` with Wine `0012`. Rebuild the x64 core, `win32u.so`, and the graphics
provider together. The linked fixture checks sample contents, resolve, and
the original allocation on RTX; RADV checks explicit import refusal.

`0027` adds `ScarlettMsaaDepthV1` at ordinal 29 for the [RTX depth/stencil
target](../native/rtx-depth.md). Apply it after `0026` and rebuild the x64
core and graphics provider. It uses the same Wine `0012` import path.

`0028` adds the [directly mapped RADV pool](../native/radv-retained-pool.md).
Use it with Wine `0013` and the matching XMem, XG and graphics providers.
The RTX 3070 continues to use the `0026`/`0027` image imports.

Apply `0029` after `0028` for the 3840×2160 alpha render target. Rebuild the
x64 `d3d12core.dll` and the graphics provider together. Test the retained
RADV heap and RTX section imports with `scarlett/tests/wc-a8.sh radv TOOL`
and `scarlett/tests/wc-a8.sh nvidia TOOL`.

`0030` adds the two 2048² BGRA8 targets to the same memory paths. Apply it
after `0029` and run `scarlett/tests/wc-a8.sh GPU TOOL bgra` for each adapter.

`0031` adds the 1440p floating-point target after `0030`. Run
`scarlett/tests/wc-float1440.sh GPU TOOL` to check the different allocation
orders, cross-format overlap rejection, original backing and descriptor views.

`0032` reserves the tiled R32 buffer in the original pool. Apply it after
`0031`; run `scarlett/tests/rtx-r32-tile.sh TOOL radv` and repeat with
`nvidia` to verify original tiled words and release after GPU retirement.

Apply `0033` after `0032` for five-mip R11 placement on RADV and RTX.
Run `scarlett/tests/rtx-r32-tile.sh TOOL radv-r11` and repeat with `rtx-r11`
to check all mip views, padding and overlapping allocation reuse after retirement.

`0034` accepts a second RTX allocation order. The new R11 range must follow
two live tiled R32 buffers on the same device and section. The overlap check
and reservation happen under the same lock. Run `python3 scarlett/tests/test-r11-pair-cpu.py
PATH/TO/libs/vkd3d/scarlett_primary_image.h` on the patched source, then
`scarlett/tests/rtx-r32-tile.sh TOOL rtx-r11` for the GPU checks.

`0035` binds the observed 640×360 R8_UINT image to the original WC section
(RTX) or same-device native heap (RADV), at `+0xe610000`. The 256 KiB range
uses the same cross-format lease and final-resource retirement as `0034`.

`0036` binds the 2560×1440 D32S8 image at its observed offset in either
original WC backing. The reservation lasts through final host-resource
retirement and includes the full guest allocation on both adapters.

`0037` reuses the WC buffer slice and cross-format reservation for the
460,800-byte row-major buffer at `+0xe650000`. A row buffer does not count
as a tiled R32 predecessor. Run `wc-float1440.sh GPU TOOL row` on each GPU.

`0038` qualifies six single-sample RT-only textures at the measured WC
pool offsets after the read-only depth views. Run `wc-float1440.sh GPU
TOOL small` on both adapters; Vulkan byte spans may be smaller than the
reserved guest span.

`0039` reuses the WC buffer reservation for two measured 160×90 tiled
float textures. It adds no shader lowering. Test with `rtx-r32-tile.sh
TOOL rtx-small` and `radv-small` sequentially.

`0040` adds the measured 160×90 R32_UINT starts to the WC reservation
via a separate export. `rtx-small` / `radv-small` also cover its two
R8_UINT sizing-only siblings and rejection of the 720p alias.

`0041` extends the WC RT placement used by `0038` to the `+5b67c2c`
3840×2160 RGBA8 RT-only constructor. The 510/513-page lease follows
`+c8`/`+360`; the raw image occupies `0x1fe0000` / `0x2008000` bytes
on RTX/RADV. The placed wrapper has no display slice. Test with
`wc-float1440.sh GPU TOOL small`; the fixture identifies all 4096 distinct
upload words in the original WC backing after title VA reuse.

`0042` qualifies the 3840×2160 flags-2 D32/S8 WC target. Both adapters
placed it in state 6 with typed clear 0/0, then wrote a plane-zero SRV
and a writable DSV. `wc-float1440.sh GPU TOOL depth1440` tests both
sizes, including the 1440p stencil-7 contract. For 4K the guest stencil
SRV and read-only DSVs are denied; host-only fixture descriptors check
the underlying image independently. Next retail stop: sampler `+1e8`,
return `+12b981e`.

`0043` places the 16-byte flags-4 startup buffer at WC `+0xe5c6fc0`.
`wc-float1440.sh GPU TOOL row` checks its four R32_UINT SRV elements,
late-written original bytes, guards, lease and VA reuse, alongside the
larger structured row. Behavioral reference only: Marvisak/VIctory
`ffe310eb`, `crates/victory-gpu/src/resource.rs::create_at_address`
returns an identity backed by the title-supplied address. This patch
uses the project's own WC import and reservation; no upstream code copied.

`0044` retains the original linear `A8_UNORM` atlas at WC `+0x19d70000`
on RADV and `+0x1bfa0000` on RTX. The 2048² atlas and 1024² fallback
use the same start; a live overlapping placement is rejected. Retail
diagnostics observed one SRV on each returned object. The `TEST_ATLAS`
variant of `test-a8-allocation.cpp` uses `test-a8-atlas.hlsl` to check
every texel through a copied SRV after address reuse on both adapters.

Seven retail launches (five diagnostic, two ordinary). The final RADV
run stopped at pooled list `+418` (return `+156099a`), with tokens 1 and
2 issued. The final RTX run stopped earlier at `+5707b8c`, with token 1
issued. Both presented only token 1.

`0045` adds `ScarlettLinearSevenV1` for the RTX 3070's seven-level 128×72
`R32_FLOAT` texture at section offset `0x1c3eb000`. The [placement
fixture](../native/deferred-targets.md#small-linear-texture-size-queries)
verified the original bytes, padding and both guards after address reuse.
