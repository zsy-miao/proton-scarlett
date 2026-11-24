# Device +0xc8: startup color texture allocation info

`344182` calls with RCX = hidden 16-byte output, RDX = device, R8D = node
mask 1, R9D = count 1, and the description as the fifth argument. `344188`
copies both qwords to the caller; `344190` requires the size's high DWORD
to be zero. `51fbdb5` and `51fbdbb` forward the low DWORDs of size (bytes)
and alignment (bytes); `164ec2`–`164ece` and `164e2d` divide the size by
bytes per element (four here) for the texture owner's initialization.
There is no descriptor-slot write or resource pointer in this ABI. The
caller sets alignment zero at `344157` on the default-alignment path.

The trace `scarlett-run.49PD1ImQ/logs/steam-999001.log:3836` records one
3840×2160 `R8G8B8A8_UNORM` Texture2D, depth/mips/samples 1, quality 0,
unknown layout, flags `0x300005`. `4ecc9df` extracts the low three
capability bits; `4ecca2b`/`4ecca7e` select and add `0x300000` on a
format/options-dependent path. The low `0x5` translates to render-target
and unordered-access capability. The individual console high bits have
no established desktop meaning. For this exact size query the translator
asks the host about an RT+UAV texture using its native layout. This does
not assert binary layout compatibility or authorize use of raw guest tiles.
Only the 52-byte prefix ending at flags is consumed.

`test-allocation-info.cpp` calls the public slot with a guarded input and
checks both qwords against the native query. It creates a real heap of
exactly that size/alignment, places the matching texture at offset zero,
clears it, and verifies the bottom-right texel after a GPU fence.

### Committed offscreen color (`+0x3f8`)

The `247aeb6` primary ring path uses DEFAULT properties `{1,0,0,1,1}`,
display flag `8`, guest state `3`, optimized transparent-black RGBA8,
extensions `{NULL,0,NULL}` and the resource IID. `193e089` copies the
optimized color from the owner's table. `48d8e4` selects internal state
5 for this alternate loop; `4dbffb0` maps it to guest enum 3. This path
uses desktop UAV as its initial working state; it does not cast guest 3
to desktop resource-state bits. Both allocation query and creation use
the same translated RT+UAV description.

Each call creates an independent offscreen texture and returns a title COM
identity. `xs_display_wrap` requires a verified `GetBuffer(index)` identity;
this call has no swapchain or index. The first known texture-view builder
at `16474f9`–`1647531` supplies mip 0 and plane 0, then `1647610` calls
device `+0x98` with null counter. Only the first four DWORDs are active:
`{28,4,0,0}`. The descriptor retains the texture independently of the
wrapper, and the recorder retains it and both heaps through the last fence.
No host handle is written into the title's descriptor storage.

The synthetic invokes the public factory and UAV writer, releases the title
identity, clears the original texture green through the GPU UAV, transitions
it to render-target state, and clears only its last pixel red. Fenced
readback checks the two colors. This qualifies offscreen rendering, not
presentation. PresentX needs a synchronized copy to the actual swapchain
buffer, after identifying the producing queue, state and output.

### Primary texture SRV (`+0x90`)

`501f753` loads the committed interface from `texture+0x20`; `501f75f`
calls the SRV writer. The captured words at `scarlett-run.M28vpkMY`,
`steam-999001.log:3834`, are `{28,4,0x1688,0,0,1,0,0,0,0}`: RGBA8
UNORM Texture2D, default component mapping, mip 0/count 1, plane 0,
minimum LOD 0. Only the 32-byte prefix through the active Texture2D
fields is needed. The device matches the exact wrapper identity and the
descriptor record owns the underlying texture until overwritten or freed.

Build the sampling fixture next to `test-allocation-info.cpp`'s executable:

```sh
dxc -WX -T cs_6_0 -E main -Qstrip_debug -Fo "$game/test-color-sample.dxil" \
    "$src/test-color-sample.hlsl"
```

The public-slot test creates two color wrappers, writes the SRV for the
first, releases both, renders different colors into adjacent texels, and
retires that work. It then removes the UAV descriptor and direct resource
reference. The checked SRV resolves to its own bank; a point-sampling
dispatch reads back the green and red texels with only the SRV retaining
the source.

### Primary texture RTV (`+0xa0`)

`cc297a`–`cc2981` save a title RTV slot in the view wrapper. `cc29d8`
loads `texture[+0x20]`; `cc29e4` calls `void(This, resource, desc, slot)`.
The trace `scarlett-run.mkufaIhT/logs/steam-999001.log:3833,3975`
matches that pointer to the `+0x3f8` result, with `{28,4,0,0}` in the
active fields: `R8G8B8A8_UNORM`, Texture2D, mip 0, plane 0. The callee
returns without further inline edits to the descriptor.

The writer checks the original resource identity and a live slot in the
device's type-2 heap. The 32-byte title slot contains the usual 16-byte
tag; its private record retains the texture. `xs_resolve_color_rtv` checks
the tag and returns the actual host CPU descriptor plus an owned texture
reference. Hold the title heap and texture and synchronize rewrites through
queue retirement. `test-allocation-info.cpp` writes two distinct colors'
RTVs through `+0xa0`, replaces one slot, and consumes the resolved RTV
with a real GPU clear followed by readback and SRV sampling.
