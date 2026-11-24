# Write-combined graphics pool

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
