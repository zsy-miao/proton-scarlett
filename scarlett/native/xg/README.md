# xg_xs.dll — measured Windows x64 subset

`XGSuggestSwizzleMode` suggests layout **0x109** (64KB_S) for 4096×4096
2D BC1, BC3, BC4 or BC5 UNORM textures with one mip, one array element, one
sample, quality zero, unknown input layout, no resource flags, and default
placement alignment (0 or 65536). The second argument must be zero. It writes
one 32-bit layout on success, including when the output aliases `desc.Layout`.
For the runtime-created 256×128 R8 texture described below, the project
recommends `0x105` only for option zero, unknown input layout and alignment
zero. Other requests return `E_NOTIMPL` without modifying the output.
Null required pointers return `E_POINTER`. These failure codes are local policy.

`XGCreateTextureComputer` accepts **explicit** layout `0x105` (4KB_S),
alignment 4096, 256×128 Texture2D R8_UNORM, one layer/mip/sample,
quality 0 and flags `0x300000`. It reads only the 52-byte active prefix
and clears the output on failure. Its non-COM vtable implements AddRef,
Release and CopyIntoSubresource (`+40`). Unsupported methods stop.
The copy requires plane/subresource 0, row pitch 256, slice pitch 32768,
a bounded span in the marked 1172 MiB WB source pool and a 4096-aligned
bounded span in the marked 2199 MiB WC destination pool. It acquires
handles, creates private aliases by handle, writes the original backing in
16-byte 4KB_S segments, orders the writes and drops both aliases before
return. On RTX 3070 the WB source pool is a marked Wine system section.
The converter maps the source range read-only from its retained section
handle. Ordinary allocations do not qualify.

**R8 selection boundary.** The title chooses between an explicit mode and
a suggestion at `4eccb28`: for a mode byte other than `ff`, it stores
`0x100|mode`; for `ff`, it calls XGSuggestSwizzleMode with `Option=0` and
the same layout field as output. Separate RADV and RTX 3070 retail runs returned
`0x105` for that request and let `+c8` check size/alignment for explicit
R8. The copy succeeded and a second recommendation rebuilt the descriptor
for `+230`. Its resource had layout `105`, alignment 0, same R8 shape and
flags; view words were `3d 4 1688 0 0 1 0 0 0 0 94602000 d 0 0 0 3`.
That is the original texture backing on RADV at `0xd94602000`. On RTX the
corresponding backing was `0xd4b202000`; the view carried that address.
Before the R8 address view was implemented, both runs completed the copy before the buffer-only SRV writer stopped at
title return `4ecc821`. No shader sampled the view in these runs.

The recommendation applies to this runtime-created descriptor. Device `+c8`
sizes the explicit mode with alignment 4096 as eight 4 KiB tiles; the texture
computer writes them into the original WC backing. Creation with unknown
layout remains unsupported. The original-backing readback below qualifies
the byte order on both GPUs.

To probe a particular x64 library's recommendation,
build `test-r8-suggest.c` with `x86_64-w64-mingw32-gcc -std=c11 -municode
-Wall -Wextra -Werror test-r8-suggest.c -o test-r8-suggest.exe`.
Pass the DLL's full Windows path as the only argument. The probe prints the
HRESULT and resulting layout for the retail descriptor; it needs no game files.

## Evidence for the suggestion

Microsoft's [DirectStorageTextureShuffling sample][sample] selects 64KB_S in
`ShuffleTextures/ShuffleTextures.cpp` and serializes `0x100 | tileMode`.
`DirectStorageTextureShuffling.cpp` uses that field as `D3D12_RESOURCE_DESC.Layout`
with 64 KiB placement alignment, no flags, sample count 1 and array size 1.
The shipped `Assets/Textures/BC{1,3,4,5}_shuffled_compressed.bin` headers at
offset `0x04` each give width 4096, height 4096 and mip count 1; offsets
`0x10` and `0x14` give format (respectively 71, 77, 80, 83) and layout
`0x109` in little-endian. The next word is a **different** DirectStorage
shuffle operation. These are demonstrated legal layouts, not a measurement
of Microsoft's suggestion heuristic. No texture payload is needed for tests.

[sample]: https://github.com/microsoft/Xbox-GDK-Samples/tree/b1e77b6ac10f95b07296025fda171ca2ddc42dbe/Samples/Graphics/DirectStorageTextureShuffling

## ABI evidence from the title (static inspection only)

`game_scarlett_master.exe` SHA-256:
`e95020b2fa4a721db3df59fdecf75621730ac559d8835252f6c3b658d5a08993`.
Addresses below are RVAs; preferred image base `0x140000000`.

| Import | IAT RVA | Jump thunk RVA | Direct call RVAs |
| --- | --- | --- | --- |
| XGCreateTextureComputer | `06793f60` | `05f3ff40` | `00344235`, `00979c75`, `016f7141`, `05121a5e` |
| XGSuggestSwizzleMode | `06793f68` | `05f3ff50` | `00c4b3ce`, `04eccca2` |

* Suggest: `RCX` points at a resource description. Both calls zero `EDX`,
  clearing the whole register; this does not distinguish a null pointer from
  an integer option. `R8` is `RCX+0x2c`, the same 32-bit field into which the
  alternate branch writes `0x100 | explicitMode`. `EAX` goes to the signed
  status helper at `04e9b660`. Descriptor stores at `04ecc9b0`–`04ecc9e4`
  match the Win64 `D3D12_RESOURCE_DESC` fields; `016f7113`–`016f713a` copies
  56 bytes for Create. The implementation reads only the common fields.
* Create: `RCX` is the description, `RDX` addresses a pointer-sized output.
  `00979c75` and `05121a5e` branch on negative status. `00344235` and
  `016f7141` do not. Observed object calls use vtable byte offsets `0x08`,
  `0x10`, `0x40`, `0x50`. Neither a complete interface nor the values to write
  into the large layout structure follow from these observations.

## Reproduce the synthetic import test

From this directory with x86-64 MinGW-w64, binutils and Wine:

```sh
mkdir -p build
x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -Werror -shared xg.c \
  -Wl,--out-implib,build/libxg_xs.a -o build/xg_xs.dll
x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -Werror test.c \
  -Lbuild -lxg_xs -o build/test.exe
objdump -p build/xg_xs.dll | grep -E 'XGCreate|XGSuggest'
WINEPREFIX="$PWD/build/prefix" WINEDLLOVERRIDES=xg_xs=n wine build/test.exe
```

Expected: both names exported; `xg_xs synthetic import tests passed`. No
ordinal API, console SDK or external console compatibility layer is required.

For the R8 copy test, link `test-r8-copy.c` against the same import
library, name the executable `game_scarlett_master.exe`, and run with
`test-launch-nixos.sh` on AMD or RTX 3070 using the matching XMem and XG DLLs.
Two coordinate passes distinguish every destination byte; neighboring
guards, wrong pools, short ranges, misalignment, pitches and descriptor
mutations are negative controls.

For the shader readback, compile `test-r8-read.hlsl` with DXC
`-T cs_6_0 -E main -Fo sample.dxil`. Build `test-r8-read.cpp` with x64
MinGW C++17, `-I../xmem -static -Lbuild -lxg_xs -ld3d12`, then place
`sample.dxil` beside the test executable. Run as above on RADV or RTX.
The compute shader uses an 8,192-DWORD raw view of the original WC range.
Both passes wait for the CPU conversion after submission; the second
reads the original after exact address reuse. This checks in-bounds
integer coordinates at mip zero. It does not exercise a retail shader or
texture filtering.

In a separate mip-zero test, a shader reading the tiled buffer and a native
`Texture2D.SampleLevel` read the same coordinate grids. Point-wrap picked the
same R8 byte in all 393,216 comparisons per GPU, including negative coordinates
and wrap seams. Manual bilinear interpolation differed from native sampling:
the largest absolute differences on a [0,1] scale were 0.00229821 on RTX 3070
and 0.00128252 on RADV. This rules out assuming those paths are bit-identical.

Refreshing a native texture from the original buffer on the GPU immediately
before sampling gave a bitwise match at all 393,216 point-wrap samples and
393,216 linear-wrap samples per adapter. The test covered explicit mip zero
and kept the refreshed texture alive through the fence. It must be refreshed
again if the source changes before another draw.

## R8 address view

Device `+230` accepts the exact R8 resource and view above. A slot owns an
import of the original WC buffer and a checked 32,768-byte span. Device `+c0`
can copy one such record from a CPU heap to a visible heap; overwriting or
releasing either slot does not revoke the other's reference. Ordinary view
and range resolvers reject R8 records. A tag copied as bytes is insufficient.

An adapter must call `xs_refresh_r8` immediately before use on an open,
ordered direct or compute list with the WC buffer in COMMON. Each call records
2,048 contiguous 16-byte GPU copies into private linear rows and a footprint
copy into a private native R8 texture. The recorder returns its one-slot heap
only after recording the shader-readable transition. Bind that heap for the
following use; never cache its contents across writes. Keep the returned hold
until the queue fence retires. All fallible operations precede the first
recorded copy. No draw-state changes or refresh work occur at descriptor write.

Retail on RADV and RTX 3070 writes a visible slot from WC offset `0x2000`
and subsequently breaks at title RVA `+28c95ce`. This run did not sample
the texture. The ordinary D3D12 fixture samples the copied slot at all
32,768 coordinates after gated CPU writes, including release of the title
heaps and exact VA reuse. It checks the last valid span, clipped/misaligned
spans, resource/view mutations and tag tampering.

To reproduce, build `../test-r8-view.hlsl` with `dxc -T cs_6_0 -E main -Fo
sample.dxil`. Use the device, queue and VM objects and compiler flags in
[the allocation test](../r8-allocation.md), compiling `d3d12_device.c`
instead of the fixture-specific device file. Link `../test-r8-view.cpp` and
the XG import library. Place `sample.dxil` beside `game_scarlett_master.exe`.
Build with `-DTEST_R8_DIRECT` for a direct list or omit it for compute.
