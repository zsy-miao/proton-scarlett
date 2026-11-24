# Device +0xc8: startup color texture allocation info

On RTX 3070 / 595.112.07, the linear image reports row pitch `0x3c00` and
size `0x1fa4000`. Rounding to 64 KiB gives the retail slot size `0x1fb0000`.
Both images use host-cached type 4, at offsets 0 and `0x1fb0000` of the same
original allocation. Fenced clears changed every expected pixel and left the
last 640 KiB untouched. The optimal image size (`0x1fe0000`) does not fit
this slot.

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
uses `ScarlettPrimaryImageInfoV1` to query the RT+UAV linear external-memory
image that the factory actually imports. This does
not assert binary layout compatibility or authorize use of raw guest tiles.
Only the 52-byte prefix ending at flags is consumed.

`test-allocation-info.cpp` calls the public slot with a guarded input and
checks the returned `0x1fb0000` size and `0x10000` alignment on qualified
RADV. The ordinary desktop optimal-image allocation-info result is not
used for this pair of calls.

### Committed offscreen color (`+0x3f8`)

The `247aeb6` primary ring path uses DEFAULT properties `{1,0,0,1,1}`,
display flag `8`, guest state `3`, optimized transparent-black RGBA8,
extensions `{NULL,0,NULL}` and the resource IID. `193e089` copies the
optimized color from the owner's table. `48d8e4` selects internal state
5 for this alternate loop; `4dbffb0` maps it to guest enum 3. This path
uses desktop UAV as its initial working state; it does not cast guest 3
to desktop resource-state bits. Both allocation query and creation use
the same translated RT+UAV description.

Each call requests the queried number of bytes synchronously via
`XMemAlloc(size,0x30808000)`. The hook supplies one of two distinct slices
from a single 64 MiB arena. XMem pins the original base; `ScarlettPrimaryImageV1`
imports the slice as a real offscreen texture. There is no committed-image
fallback. Each wrapper calls `XMemFree` on its returned slice at final title
Release, while the backend owns an independent IUnknown pin on the whole arena
until after Vulkan image/memory destruction. `xs_display_wrap` requires a verified `GetBuffer(index)` identity;
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
reference. The fixture then calls the retail sampler writer (device `+0x1e8`)
with the startup preset: linear min/mag/mip filtering, wrap U/V/W, LOD 0–12.
Device `+0xc0` copies one sampler from the CPU-only heap to the visible heap.
After releasing the source heap, the fixture resolves the checked destination,
samples the green and red texel centers and samples the wrap boundary. Fenced
readback returns `ff00ff00`, `ff0000ff`, and the red/green midpoint `ff008080`.

**Post-clear sampler.** After the clear PSO, `5804569..580459f` sets up
the same 36-byte compact record, looks up filter `0x15`, then changes only
the WORD at `+0xa` to `0` and calls again (return `58045a4`). The latter
goes through `12b95a6` to the expander `12b96b0`; `12b9818` calls device
`+1e8` with RDX pointing to its zero-initialized 92-byte record and R8 =
`[75e7428] + index * [75e7438]`. These are title RVAs.

| Expanded byte offset | Compact input and observed value |
|---|---|
| `00` | zero |
| `04,08` | mag/min: 1 when compact filter bits `4,10` (hex masks) respectively are set, otherwise 2 on this path |
| `0c,10` | mip/mag: 2 when masks `1,4` are set, otherwise 1 |
| `14` | table at `6172fa0` indexed by `filter >> 7`: 0 |
| `18,1c,20` | zero-extended address mode bytes `+1,+2,+3`: wrap (1) |
| `24,28` | floats from `+1c,+20`: min/max LOD 0 and 12 |
| `2c,30,34` | zero; float bias from `+4` (0); low five bits of `+8` (anisotropy 1) |
| `38,3c,40` | zero, zero, comparison func NEVER (1) |
| `44..53,54..5b` | border RGBA from `+c` (zero); zero tail |

Linear's four filter words are `1,1,2,2`; point's are `2,2,1,1`.
No other filter/address combination is qualified. A retail trace saw
linear at `1ea20000`, point at `1ea20010`: slots 0 and 1 of the same
128-entry CPU sampler heap. `12b982b` jumps to `4ecc830`, which calls
`+c0` for one type-1 descriptor. Trace destinations `1ea10000` and
`1ea10010` are the corresponding slots in the shader-visible heap.

The fixture binds both copied samplers after releasing the source heap.
At the same seam `u=1`, point selects wrapped green `ff00ff00` while
linear blends red and green. Both sample centers still return green/red.
Single-word mutations across the point record, unreadable input, misaligned,
past-end and visible write destinations must all fail.

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
