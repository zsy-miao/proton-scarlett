# Deferred target sizing in the two startup groups

Both adapters call device `+c8` (return `+344188`) with node/count `1/1`. The complete 13-DWORD record is `[3,0,0,0,width,0,height,0x10001,format,1,0,0,flags]`: 2D, default alignment/layout, one layer/mip/sample, quality zero. Format 87 is BGRA8_UNORM; 19 is R32G8X24_TYPELESS. Order and consumers matched in bounded retail diagnostics:

| Extent | Format | Guest flags | Owner ctor | Factory return | Group field(s) | RTX bytes | RADV bytes |
|---|---:|---:|---:|---:|---:|---:|---:|
| 994×624 | 87 | 0xc00005 | 0xa8 | +1da825d | +0 | 0x280000 | 0x290000 |
| 994×624 | 87 | 0x300001 | 0x28 | +1da8298 | +0x28 | 0x280000 | 0x290000 |
| 994×624 | 19 | 0x800002 | 0x48 | +1da8354 | +0x18, +0x20 | 0x4f0000 | 0x350000 |
| 994×624 | 87 | 0xc00001 | 0x28 | +1da83f0 | +0x30 | 0x280000 | 0x290000 |
| 3840×2160 | 87 | 0xc00005 | 0xa8 | +1da825d | +0 | 0x1fe0000 | 0x2010000 |
| 3840×2160 | 87 | 0x300001 | 0x28 | +1da8298 | +0x28 | 0x1fe0000 | 0x2010000 |
| 3840×2160 | 87 | 0x300001 | 0x28 | +1da82e6 | +8 | 0x1fe0000 | 0x2010000 |
| 3840×2160 | 19 | 0x800002 | 0x48 | +1da8354 | +0x18, +0x20 | 0x3fc0000 | 0x28f0000 |
| 3840×2160 | 87 | 0xc00001 | 0x28 | +1da83f0 | +0x30 | 0x1fe0000 | 0x2010000 |

Every result is 64 KiB aligned. The size feeds the owner constructor (`+164dee`): `owner+8` is the size divided by the recorded stride (4 color, 8 depth). The two identical 4K queries have distinct owners; the second is the optional group `+8` member, absent in the smaller group. Each depth group member at `+0x18` and `+0x20` is a distinct wrapper with the *same* owner at wrapper `+8`. When the list reached 12 entries, all nine owners still had null resource fields at `+0x20`. No corresponding `+360`, placed or committed creation, guest VA, D3D view or GPU use was observed through that snapshot. A different thread then failed the tiled 1280×720 R32 placement. One RTX diagnostic delayed the tiled query by 500 ms so the initializer thread could be observed; timing and the first failure vary.

`+c8` asks the live host using only the qualified low flags (RT+UAV, RT or DS). It requires matching per-resource and aggregate results, zero offset, default placement alignment and a measured page count. It does not synthesize a size from pixel count. `test-deferred-targets.cpp` exercises the five 4K calls in order, checks rejected neighbors/short records and `+360`, then uses the sizes for five adjacent desktop images. Fenced readbacks check every color pixel and both depth/stencil planes. These are fixture resources, independent of the deferred guest owners.
## Installed retail boundary

The initializer stored the last 4K wrapper, returned, and published its
manager and ready byte on both adapters. All nine owners in the two groups
still had null resource fields at the last sample. On a different thread,
the failed 720p tiled placement led to breakpoint `+1647d03`. Both installed
runs reproduced that breakpoint and logged completion of the first clear.

## RTX 2560×1440 B5G5R5A1_UNORM

After both tiled R32 descriptor triples, device `+c8` (return `+344188`)
queries one format-86 texture, one mip and sample, layout 0, flags `0x300005`.
The size-only answer is `0x780000` bytes aligned to `0x10000`. Before the
next query, the owner from `+164dee` had `+8 = 0x3c0000` elements, stride 2,
and a null resource at `+0x20`. The next `+c8` asks for 2560×1440 format 26,
six mips, layout `0x102`, flags `0x300004`. No format-86 placement or GPU
consumer was seen in between.

RTX 3070 reports format support `0xe4d3f1/0x200`: RTV and texture shader
load, no typed UAV view/load/store. Both UAV-only and RT+UAV desktop
creation failed with E_INVALIDARG. A committed RT control and a separate
test-only import of a marked WC section both returned the exact packed
pixels and decoded SRV colors. The WC test also checked overlap rejection,
the original mapping and both guards after replacing the guest VA. Its
`+0x12600000` is a fixture offset, not a retail location for this target.
Only `+c8` is admitted; `+360`, placement and guest views are not enabled.

RADV 26.3.0 (`1002:164e`) accepts the combined RT+UAV host descriptor:
`0x790000` bytes, `0x10000` alignment. Support is `0x2e4d3f0/0x2c0`
(texture, RTV, shader load, typed UAV load/store). Plain and UAV-only images
used `0x780000`; RT and RT+UAV images used `0x790000`. A desktop fixture
read all 3,686,400 packed RTV pixels, then checked every typed UAV load and
coordinate-dependent store (all 65,536 packed values) and two SRV colors.
In staged AMD retail the exact `+c8` returned `0x790000`; its owner then
held `0x3c8000` elements, stride 2, resource null, at the next query on the
same thread. That query also requests format 26, six mips, layout `0x102`,
flags `0x300004` at return `+344188`. No format-86 resource or view is
qualified by these host tests.

## 2560×1440 deferred depth after the five-mip R11 placement

On RTX and RADV the constructing thread called `+c8` (return `+344188`)
for format 19, flags `0x800002`, one mip/layer/sample, zero alignment and
layout. Native DS requirements were `0x1e00000/0x10000` on RTX and
`0x1300000/0x10000` on RADV; the single-resource detail had zero offset
and matched the aggregate. The existing deferred sizing table admits only
the depth query for this extent. The guarded fixture clears and reads back
all 2560×1440 depth and stencil values on adjacent desktop placements.

Both retail traces went through the `0x48` owner constructor at `+164dee`
and factory return `+25cc6c9`. At the next query the wrapper published at
image `+76503e0` referenced that owner at `+8`; the owner held `0x3c0000`
(RTX) or `0x260000` (RADV) elements of stride 8, and a null resource at
`+0x20`. No depth placement was observed before the next `+c8` from
`+25cc757`: 640×1440 R32_UINT, flags `0x300005`, layout 0, one mip.

## Single-channel owners following the 1440p depth query

| Factory return | Extent and format | Flags | RTX bytes | RADV bytes | Wrapper globals (RVAs) |
|---|---|---:|---:|---:|---|
| `+25cc757` | 640×1440 R32_UINT | `0x300005` | `0x3c0000` | `0x3c0000` | `+7650418`, `+7650420` |
| `+25cc84c` | 2560×1440 R32_FLOAT | `0xc00001` | `0xf00000` | `0xf10000` | `+76503d8` |

Both `+c8` calls return to `+344188`. The two distinct UINT wrappers point
at the same `0xa8` owner (wrapper `+8`); the FLOAT wrapper points at a
separate `0x28` owner. Each has stride 4 and count equal to its returned
size divided by four. All resource fields at owner `+0x20` were still null
at the next query. On that thread neither placement (`+428`, `+3f8`) nor
object/address descriptor writes intervened. The next `+c8` is 160×90
R16_FLOAT, five levels, `0x1000` alignment, layout `0x102`, flags `0x300004`,
factory `+25cc8da`, query return `+344145`.

Two 160×90 `R16_FLOAT` five-level owners asked for `0x1000` alignment,
then retried with zero. Both accepted `0xa400` bytes at natural alignment
`0x100` (164 tiles of 16×8 halfwords). Their resource pointers were still
null at the next `+c8`: 2560×1440 `R32G32_FLOAT`, flags `0xc00005`.
Only sizing is enabled for this chain.
