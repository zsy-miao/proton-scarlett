# Guest view and host sampling controls for the 128-byte A8 line

Build in scratch with the same XG/XMem providers as `xg/test-linear-seven.cpp`.
Put the matching executable and shader together in the fixture working directory:

```sh
n=/path/to/scarlett/native
dxc -T cs_6_0 -E main -DROW_WIDTH=128 -Fo a8-line.dxil "$n/test-srgb-row.hlsl"
x86_64-w64-mingw32-g++ -std=c++17 -Os -static -Wall -Wextra -Werror \
    "$n/test-a8-line-view.cpp" d3d12_device.o queue.o d3d12_vm.o \
    -ld3d12 -ldxgi -ldxguid -o test-a8-line-view.exe
```

Link the objects built from the current staged provider sources. The two
host control SRVs have DXGI format A8_UNORM;
the default mapping should yield `(0,0,0,A)` and the explicit control
should yield `(A,1,0,0)`. In each pass point-sample every texel at its
center through the copied shader-visible descriptors after overwriting and
releasing their CPU heap. Permuted even and odd byte patterns exhaust all
256 alpha codes. Queue a footprint copy from the retained original WC pool
before the XG `+40` stack write. Hold the queue until after that write; in
pass two reuse the title VA for a differently filled ordinary mapping.
Compare the shader outputs, original 128-byte padding, flanking 256-byte
guards and the replacement mapping. The lease pins the backing generation,
not the contents at record time.

Static reference: vkd3d `libs/vkd3d/utils.c` selects native
`VK_FORMAT_A8_UNORM_KHR` or emulated `VK_FORMAT_R8_UNORM` based on
maintenance5 and feature parity. `resource.c` applies `(ZERO,ZERO,ZERO,R)`
for the emulated A8 view, composes the requested component mapping, and
handles `D3D12_SRV_DIMENSION_TEXTURE1D`. The fixture prints the adapter's
format support and requires `TEXTURE1D|SHADER_SAMPLE` before creation.
Only a fenced readback qualifies these host expectations on that adapter.
The third output comes from the copied guest descriptor refreshed on the
consuming queue. The measured `+501f703` wire uses mapping `0x1688`; the
custom mapping is a host control only. Check all three outputs after the
original pool is released and the VA reused.
