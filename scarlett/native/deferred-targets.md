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
