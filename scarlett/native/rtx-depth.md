# RTX 3070: original 4× D32_FLOAT_S8X24_UINT placement

Retail passed `+428` a typeless 1×1 R32G8X24 Texture2D, four samples,
quality zero, depth/stencil flag, unknown layout, state argument 6, typed
D32_FLOAT_S8X24_UINT clear value (0,0), IID_ID3D12Resource. The full 4 MiB
slot starts at `0xec00000` in the marked `0x89700000` WC pool; color is
still live immediately before it. The private importer reuses the color
path's tagged fd validation, section-handle lifetime and overlap registry.
Only its Vulkan format/usage, resource description, typed clear value and
initial desktop state differ. The image is optimal D32_SFLOAT_S8_UINT,
DEPTH_STENCIL_ATTACHMENT plus sample/transfer usage. It requires 4×,
non-dedicated external import and type-zero compatibility; the wrapper
cannot silently enter the color SRV path. Windows aliases are not repinned
by title address on use. Creation performs no clear.

After applying the shared depth-query patch, apply core patch `0027` on top
of `0026`; rebuild the matched x64 d3d12core and device adapter. No new
Wine patch is required. For `test-msaa-depth.cpp` build the depth shaders
as for the shared fixture; define `TEST_NV_DEPTH` and link `test-msaa-device.o`
(compiled with both staged bootstrap defines) instead of `d3d12_device.o`.

The RTX fixture compares native depth and stencil SRVs per sample after four
sample-masked draws. Through a retained CPU alias the sample depth words
are at +0, +4, +0x10 and +0x14; stencil bytes at +0x200, +0x201, +0x204,
+0x205. The fixture checks every other byte of the reserved 4 MiB, adjacent
guards, neighboring depth surfaces and the replacement allocation. It drops
the wrapper, releases/reuses the exact title VA and retains the native image
through its fence. A duplicate live slice fails; release then recreation
preserves all prior depth/stencil bytes. The color fixture still passes.
These offsets describe this tested driver/format only. No depth/stencil
resolve is implied.

Retail confirmed successful placement and `+90` on the same depth wrapper,
prefix `15 6 1688 0 0 0 0 0` (hex): R32_FLOAT_X8X24_TYPELESS, Texture2DMS,
default mapping. The existing writer retains the device-local native image
in the slot. The color format requires a color wrapper; neither the typed
DSV nor the stencil-only format qualifies for this depth SRV.

In the RTX fixture, the middle placement's four depth loads use the resolved
guest slot. Only the neighboring stencil descriptor is written natively for
that placement. After wrapper release, title VA reuse and the fence, the slot
still excludes duplicate placement until overwritten. Retail's next failure
is `+a0` on the **color** wrapper, RTV prefix `1c 6 0 0` (hex), return
`+cc29ea`. Retail did not execute the fixture's draws or sample reads.
