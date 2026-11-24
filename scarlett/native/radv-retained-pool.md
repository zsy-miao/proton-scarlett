# Retained 2,199 MiB WC pool on RADV

Tested on `1002:164e`, RADV 26.3.0. XMem opts the exact pool into
placed host mapping when creating its default device. Wine reserves and tracks
the whole CPU view, then maps one nonexported host-visible/coherent type-2
allocation. The D3D12 device, heap and whole-pool GPU buffer remain owned by
XMem. An administrator does not need a driver setting. The private backend
entry returns `S_FALSE` on other adapters. An existing device without placed
mapping cannot publish such a view and stays on the previous WC section path.

XG stages only the bytes it writes (32 KiB or a DWORD). Under Wine's VM lock,
commit rechecks the live allocation generation. Address descriptors take a
reference to the same device's buffer and validate their offset and span. The
color and depth resources each bind a qualified optimal 4× image to a 4 MiB
slice of that heap. Images hold their heap until native resource destruction;
live overlapping slices are refused. Wine alone owns the title mapping. The
retained buffer denies desktop `Map`, including after title address reuse.

`VirtualFree(base,0,MEM_RELEASE)` withdraws the view under the VM lock and
signals a private retirement event. The owner thread removes its entry under
the registry lock, then releases the registry's references outside that lock.
Captured GPU resources still own their backing. A stale address cannot be
acquired merely because the retirement thread has not run. A failed Vulkan
unmap does not release the VA. New GPU captures propagate the D3D12 removed
reason; there is no device-reset or recovery scheme here.

## Scratch reproduction

Apply Wine `0013` after `0012` and vkd3d `0028` after `0027`. Rebuild the
matched x64 ntdll.so and win32u.so with **the running Wine server's generated
protocol header**, not another checkout's `include/wine/server_protocol.h`.
Rebuild d3d12core.dll including heap.c and resource.c, then staged d3d12_xs,
xmem and xg_xs. The provider and consumer binaries are a matched set.

Use the existing MSAA color/depth shader fixtures. Compile test-msaa-color.cpp
and test-msaa-depth.cpp as before with `TEST_NATIVE_POOL` plus `TEST_NV_IMPORT`
(color) or `TEST_NV_DEPTH` (depth) to reuse the accepted-placement tests.
Run test-r8-read.cpp with `TEST_NATIVE_POOL`. Build test-native-pool.cpp and
test-native-pool-race.cpp with x64 MinGW C++17, `-static -ld3d12`. Launch
these as private titles using the read-only RADV harness. The original RTX
fixtures retain their old flags and fd-backed assertions.

On RADV the fixture read four distinct colors and their expected resolve.
Per-sample depth and stencil and the mixed depth/stencil test matched. After
public free the retained buffer exposed 73,880 changed color bytes and 8,220
changed depth bytes in the respective original slots; both guards and both
replacement mappings stayed intact. Two gated R8 reads after XG writes
verified every X and Y coordinate. The acquire/free race held eight old
references across free and then attempted 4,096 acquisitions/writes against
VA reuse. The loss test exercises vkd3d's validation-driven removed reason;
it is not a physical GPU reset. Read-only retail on both hosts stopped at
`+344145` on the pre-layered-texture base. With the six- and twelve-layer
providers integrated, AMD stopped at `+344188` and RTX at `+344145`.
AMD previously stopped at `+cc2936`.
