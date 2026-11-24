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

## Five RT+UAV size queries after the R16 chain (0034)

The constructing thread requested one layer, mip and sample, layout and
alignment zero, flags `0xc00005`, at `+c8` (return `+344188`). Sizes in
64 KiB pages, in retail order:

| Extent | Format | RTX 3070 | RADV 1002:164e |
|---|---|---:|---:|
| 2560×1440 | R32G32_FLOAT | 480 | 462 |
| 2560×1440 | R8_UNORM | 60 | 61 |
| 2560×1440 | R32G32_UINT | 480 | 460 |
| 1280×720 | R32_UINT | 60 | 60 |
| 640×360 | R8_UINT | 4 | 4 |

Only the last texture requested `+360` (return `+abfe0d`): offset zero,
size `0x40000`, alignment `0x10000` on both adapters. Its next call was
placement (`+428`, return `+193e2db`), state 3, resource IID and a non-null
R8_UINT clear value with words `0,0,0,0x3f800000`. Placement returned
`E_NOTIMPL` and the title broke at `+cc2834`. No view intervened.

The guarded fixture checks both sizing slots against native allocation
info, rejects descriptor-field and node/count neighbors and tests short
input at a page boundary. It does not place these five guest resources.

### 640×360 R8_UINT placement after 0035

Both title VAs resolved to `+0xe610000` in the `0x89700000` WC pool:
RTX `0xd59810000` in the marked section at `0xd4b200000`, RADV
`0xda2c10000` in the native heap mapped at `0xd94600000`. The other
backing lookup failed on each adapter. Vulkan required `0x3c000` bytes
on both, with alignment `0x400` / `0x1000` and memory type bits `3` /
`0x7ad` respectively. Reserve the full queried `0x40000`, not just the
Vulkan requirement. Both runs wrote SRV `3e 4 1688 0 0 1 0 0`, RTV
`3e 4 0 0`, and null-counter UAV `3e 4 0 0` to distinct slots. Next
came device `+230`, return `+4ecc821`: an address SRV of a 192-byte
buffer (6 structured elements of stride 32, word 9 `0x800100`). It is
not a view of the placed R8 image.

`wc-float1440.sh radv|nvidia TOOL r8uint` tests the original backing after
VA replacement, all texels, both guards and unused allocation tail. Its
shader samples, UAV clear and RTV stripe are fixture operations.

### Sizing after the six-element structured SRV

Both adapters issued these `+c8` queries in order, returning to `+344188`.
Each was one 2D layer/mip/sample with zero requested layout and alignment.
Sizes in 64 KiB pages:

| Extent | DXGI format | Flags | Host sizing flags | RTX | RADV |
|---|---|---:|---|---:|---:|
| 2560×1440 | R8_UNORM | 0x300005 | RT+UAV | 60 | 61 |
| 2560×1440 | R32_UINT | 0xc00005 | RT+UAV | 240 | 240 |
| 2560×1440 | R32G8X24_TYPELESS | 0x80300006 | DS | 480 | 304 |
| 3840×2160 | R32G8X24_TYPELESS | 0x80300002 | DS | 1020 | 655 |
| 1280×720 | R16G16_FLOAT | 0x300001 | RT | 60 | 61 |
| 2560×1440 | R32G8X24_TYPELESS | 0x4800002 | DS | 480 | 304 |

For `0x80300006` the literal low DS+UAV flags yielded `UINT64_MAX` size
on both desktop devices. The table admits DS-only sizing for this exact
request, without qualifying a DS+UAV image. No placement or view from
this group was observed before the final query. Only the last row requested
`+360` (return `+abfe0d`), with zero offset and matching size/alignment.
The next call was `+428` (return `+193e2db`), state 6, resource IID, typed
D32_FLOAT_S8X24_UINT clear of depth 0 and stencil 7. Guest VAs were
`0xd5f3d0000` (RTX) and `0xda8740000` (RADV). The call returned
`E_NOTIMPL`; both ordinary runs stopped at title `+5707b8c`.

### Original-backed 2560×1440 depth target (0036)

With flags `0x4800002`, state 6 and typed clear depth 0/stencil 7, RTX
resolved `0xd5f3d0000` to offset `0x141d0000` in the marked `0x89700000`
WC section; RADV resolved `0xda8740000` to `0x14140000` in its retained
native heap. The other lookup missed on each adapter. The image reservations
are `0x1e00000` / `0x1300000`; actual Vulkan requirements were `0x1e00000`
bytes, alignment `0x400`, memory mask `3` / `0x12fc008` bytes, alignment
`0x10000`, mask `0x7ad`. Both use the shared cross-format range lease.

Retail published depth SRV `15 4 1688 0 0 1 0 0`, then DSV `14 3 0 0 0 0`
(hex). Both ordinary runs next rejected device `+c8` at `+344188` for a
2560×1440 R32_FLOAT, one mip, layout `0x102`, flags `0x300004`.

`wc-float1440.sh radv|nvidia TOOL depth1440` checks every depth/stencil
texel after independent rectangular clears. A fixture-only stencil SRV
beside the guest depth SRV checks shader loads and comparison results.
Fenced GPU commands execute after replacing the guest VA; both adapters
retain the original storage, preserve both 64 KiB guards and reject
partial MSAA color overlap in both orders. RADV also preserves the unused
allocation tail beyond the Vulkan requirement.

### Deferred tiled R32_FLOAT after depth1440

After the depth SRV and DSV, two distinct `0x88` owners requested `+c8`
(return `+344188`), layout `0x102`, flags `0x300004`, one layer/sample,
zero requested alignment. The independent XG texture computer returned
256-byte alignment for both. Every coordinate address and copied word
matched a complete 8×8 tile (byte offset within the tile is four times
`(x&3)|((y&3)<<2)|((x&4)<<2)|((y&4)<<3)`); there were no padding words.

| Extent | Mips | Mip byte offsets in API order | Bytes | Owner elements × stride |
|---|---:|---|---:|---|
| 2560×1440 | 1 | `0` | `0xe10000` | `0x384000 × 4` |
| 1280×720 | 2 | `0xe1000`, `0` | `0x465000` | `0x119400 × 4` |

Factories returned to `+1edbc4a` and `+1edbd15`. On both adapters both
resource pointers at owner `+0x20` were null at the next `+c8`: 2560×1440
R32G8X24_TYPELESS, one mip, flags `2`, alignment/layout zero, from
`+1edbe1c`. No detail query, placement or view was observed on that thread.

### Plain 1440p depth through the row buffer (`0037`)

The next two +c8 calls requested D32S8 sizing, flags `2` at 2560×1440
(480 RTX / 304 RADV pages), and `0x80300002` at 1280×720 (120 / 77).
XG independently measured the subsequent 1280×720, **one**-mip R32_FLOAT
`0x102` layout at `0x384000` bytes / 256 alignment. All 921,600 32-bit
words matched the 8×8 coordinate permutation and the reference copy.
No +360 or texture placement intervened on the constructing thread.

Instead +428 (return `+6dd8b`) requested a 460,800-byte row-major UAV
buffer, flags 4, state `UINT_MAX`, clear null, resource IID. At offset
`+0xe650000` in the `0x89700000` WC pool it landed immediately after
the earlier R8_UINT image. The marked section at `0xd4b200000` (RTX)
and native heap at `0xd94600000` (RADV) resolved the complete span.
The public wrapper published SRV then null-counter UAV on that same
object: 115,200 structured elements of stride four. The store/load shader
in `wc-float1440.sh GPU TOOL row` proves original bytes after VA reuse,
range guards, duplicate reservation, bad descriptors and slot preservation.

Next came seven single-image +c8 queries (64 KiB pages):

| Extent | Format | Flags | RTX / RADV |
|---|---|---|---|
| 2560×1440 | R10G10B10A2_UNORM | 0xc00005 then 0x300005 | 240 / 241 |
| 3840×2160 | R11G11B10_FLOAT | 0x300001 | 510 / 513 |
| 3840×2160 | R8G8B8A8_UNORM | 0xc00001 | 510 / 513 |
| 2560×1440 | R8G8B8A8_UNORM_SRGB | 0xc00001 | 240 / 241 |
| 2560×1440 | R8G8B8A8_UNORM | 0xc00001, twice | 240 / 241 |

Both adapters then wrote `+90` plane-1 stencil SRV `16 4 1688 0 0 1 1 0`
(hex) on the previously placed depth1440 object. The depth1440 fixture now
uses this guest SRV, alongside its depth SRV, for shader reads. Next came
`+a8` DSV `14 3 3 0 0 0` on the same object (read-only depth and stencil),
unsupported at title return `+14a69e8`.

After the stencil SRV, the title requested three single-sample D32S8 DSVs
on the same resource at `+14a69e8`, adjacent to its writable DSV. Flag
words were `3`, `1`, `2` (both planes, depth only, stencil only read-only);
all three mip/union words were zero. Both ordinary runs then rejected
`+c8` for 256×256 R16G16B16A16_FLOAT, flags `0x300001`, one mip/layer/
sample, alignment/layout zero, return `+344188`.

`wc-float1440.sh GPU TOOL depth1440` binds both-plane read-only DSV and
both SRVs simultaneously in a pixel shader, compares the samples against
full depth/stencil readbacks, and clears only the writable aspect through
each partial DSV. The original pool remains pinned across VA recycling;
invalid writes leave the descriptor intact. This draw is fixture evidence.

### Six small WC render targets (0038)

The same constructing thread made six +c8 / +360 / +428 sequences, each
one sample, layer and mip, layout/alignment zero, flags `0x300001`, state
6, resource IID and same-format zero clear. Native detail offset was zero,
alignment 65536, size equal to the full reservation. Original WC pool
offsets matched across RTX marked-section imports and RADV native heaps.

| Extent | DXGI format | Pool offset | Reserved | Vulkan bytes RTX / RADV |
|---|---|---:|---:|---:|
| 256×256 | R16G16B16A16_FLOAT | 0xe6d0000 | 0x80000 | 0x80000 / 0x80000 |
| 256×256 | R11G11B10_FLOAT | 0xe750000 | 0x40000 | 0x40000 / 0x40000 |
| 256×256 | R16G16_FLOAT | 0xe790000 | 0x40000 | 0x40000 / 0x40000 |
| 64×2 | R11G11B10_FLOAT | 0xe7d0000 | 0x10000 | 0x800 / 0x2000 |
| 127×128 | R16G16_UNORM | 0xe7e0000 | 0x10000 | 0x10000 / 0x10000 |
| 64×64 | R16G16B16A16_FLOAT | 0xe7f0000 | 0x10000 | 0x8000 / 0x8000 |

Each wrapper received a 2D SRV (identity mapping, mip zero, one level),
then an RTV. No UAV was observed. The following `+c8` (return `+344188`)
requested 160×90 R32G32_FLOAT, one level, layout `0x102`, flags `0x300004`.
During qualification the earlier row-buffer SRV also had a nonzero DWORD
at byte 12, padding before its 64-bit FirstElement. The writer now ignores
that DWORD while checking all active fields and the source object.

`wc-float1440.sh GPU TOOL small` checks guarded original backing after
VA reuse, exact sizing, duplicate placement and incompatible descriptors,
all RTV pixels and two shader SRV loads across each clear boundary.

### 160×90 tiled float views (0039)

Both formats requested one mip, layout `0x102`, flags `0x300004`, 256-byte
natural alignment. The separate XG reference coordinate and linear-to-tiled
copy queries agreed at every visible texel, without touching padding:

| Format | Tile | Total bytes | Padding texels | Offset RTX / RADV |
|---|---|---:|---:|---|
| R32G32_FLOAT | 8×4 | 0x1cc00 | 320 | 0x15fd0000 / 0x15440000 |
| R32_FLOAT | 8×8 | 0xf000 | 960 | 0xe6c0800 / 0xe6c0800 |

R32_FLOAT first tried input alignment 4096 on both sizing slots, then
retried with zero; the RG query used only zero. Both were placed with
zero requested alignment, state 3, resource IID and null clear. Both used
the original marked WC section on RTX and same-device native heap on RADV.
The title wrote one 2D SRV and one null-counter UAV per object. These
records own the original range; ordinary shader resolution remains denied.
No sampled frame or retail shader consumer of either view was observed.
Next: `+c8`, one-mip 160×90 R32_UINT, alignment 4096, layout `0x102`,
flags `0x300004`, return `+344145`.

This batch took 11 retail launches: nine diagnostic runs and two ordinary
runs on the same staged build, one per adapter. RTX presented token 1
(count 0 → 1); RADV reached the producer fence and logged no completed
present. Neither ordinary run presented a second token.

`rtx-r32-tile.sh TOOL rtx-small|radv-small` checks both formats in a single
fixture, including distinct address fingerprints, handle generation,
descriptor copying, negative fields, allocation collisions, GPU read/write
of both original spans after VA reuse, padding and both 64 KiB guards.

### 160×90 tiled UINT (0040)

The `+1edc948` owner queried `+c8` then `+360`, each with input alignment
4096 then zero. Both returned `0xf000 / 0x100`. The independent reference
copy and all 14,400 coordinates matched the small R32_FLOAT 8×8 permutation
(`003d5566177d7e25`, 960 padding words). `+428` at `+193e2db` placed it
in the original WC pool, at offset `0x1545cc00` on RADV and `0x15fecc00`
on RTX. Its object SRV and UAV have distinct small-UINT record kinds;
the 720p address alias and shader table do not apply.

The two R8_UINT callers (`+1edca3f`, `+1edcd74`) each made only the `+c8`
probe/retry, returning `0x3c00 / 0x100`. Neither diagnostic reached the
predicted SNORM or later float queries. Next observed stop: `+344188` at
`+c8`, 3840×2160 RGBA8_UNORM, flags `0x300001`, layout and alignment zero.

Four retail launches: one diagnostic per adapter, then one ordinary run
per adapter on the same staged provider and core. RTX presented token 1
(count 0 → 1). RADV completed the producer fence with no logged count
increment. Neither ordinary run presented a second token.

### 4K RGBA8 color target (0041)

The constructor at `+5b67c2c` placed the `0x300001` RGBA8 target in state
6 with a zero clear, then wrote its SRV and RTV. RADV used 513 × 64 KiB
at `+0x15470000`; RTX used 510 × 64 KiB at `+0x16000000`. The fixture
verified all 8,294,400 texels and found 4,096 distinct test words in the
original allocation after VA reuse. The object did not qualify as a
display primary.

Six retail launches: four diagnostic and two ordinary. In three fixture
runs, the initial RADV check assumed raw clear colors would be present;
explicit texel writes verified the backing on both adapters. Both final
ordinary runs presented only token 1 (count 0 → 1) and stopped at
`+5707b8c` on the 4K depth/stencil placement path.

### 4K depth/stencil placement (0042)

Retail placed the texture in the original WC pool at `+0x17480000` on
RADV and `+0x17fe0000` on RTX, then wrote a depth SRV and a writable DSV.
The 4K target clears depth and stencil to zero; the 1440p target still
requires stencil 7. Both depth fixtures passed on both adapters after VA reuse.

Four retail launches: two diagnostic and two ordinary. Both ordinary
runs stopped at sampler write `+1e8` (return `+12b981e`). RTX presented
token 1; RADV completed its producer fence with no logged presentation.
Neither presented a second token.

### Null resource after sampler setup (0043)

The placed-buffer call (device `+428`, return `+6dd8b`) returned
`E_NOTIMPL` with a null output for a 16-byte row-major buffer at WC
`+0xe5c6fc0`. The title later called `+6eb740` with a null first argument
and hit `+6eb888`. Both adapters now place that buffer over its original
bytes and accept its four-element `R32_UINT` SRV. The null check is unchanged.

Five retail launches: three diagnostic and two ordinary. In both final
runs the next stop was a UAV write (device `+98`, return `+1647616`).
RTX presented token 1; RADV issued token 1 without a logged presentation.
Neither presented a second token.

The subsequent batch traced the 16-byte buffer's UAV at `+1647616` to
the same object as its SRV: four `R32_UINT` elements, null counter.
The row fixture verified typed writes and reads after VA reuse on both
GPUs. Retail publication alone does not establish shader execution.
Three retail launches (one RADV diagnostic, then one ordinary per GPU)
reached command-list `+238`, return `+4ed46aa`. Both ordinary runs
presented token 1. RTX issued token 2 on the same clock and epoch;
neither run presented it.

### Small linear texture size queries

For the 128×72 `R32_FLOAT` texture the game asks for aggregate and
per-resource sizes with requested alignment 4096, then 0. Both return
`0xd900` bytes, alignment `0x100`, offset zero. The seven mip starts, in
API order, are `0x4900, 0x2500, 0x1300, 0xa00, 0x500, 0x200, 0`.
The separate 128×1 `A8_UNORM` 1D texture requests only aggregate
sizing: `0x100` bytes, alignment `0x100`. Guarded tests passed on AMD
and RTX. The RTX diagnostic then encountered a null texture-computer
interface before placement. No placement or view was observed for the chain.

Five retail launches: three RTX diagnostics and two matched ordinary
runs. Separate runs: one reference layout check and two passing fixtures.
AMD issued tokens 1 and 2 and stopped at `+32820`; RTX issued only token
1 before the title breakpoint at `+5707b8c`. Each presented token 1 once.

RTX subsequently copied all seven R32 mips into the original pool at
offset `0x1c3eb000`. Device `+428` received the same address, state 6
and no clear value; it returned `E_NOTIMPL` and no resource. Both GPUs
passed fenced readback of every copied row, padding byte and end guard.

Backend patch `0045` adds placement for this RTX 3070 section and offset.
The placement fixture read all `0xd900` bytes through the new resource,
including all seven levels and their padding; both 256-byte guards stayed
intact. It saw a late write to the original allocation after the guest
address was reused, and the replacement stayed untouched. A second
overlapping placement and an unsupported view were rejected. Retail
creation with this patch and subsequent shader sampling remain unverified.

Four retail launches: one RTX diagnostic, two matched staged ordinary
runs and a separate RTX run through the installed `bdc4efab` baseline.
The two fixtures ran before ordinary verification. AMD stopped at
`+32820`. Staged RTX first hit a null texture computer at `+16f71ea`
for the separate 1D A8 texture; installed RTX hit the same instruction
first for the R32 chain. Each ordinary run issued tokens 1 and 2 and
presented only token 1.

The 128×1 A8 texture computer now writes the caller's 128 stack bytes
into its original 256-byte WC span. RTX traced the destination to pool
offset `0xe6cf800`; both GPUs passed fenced readback including padding
and adjacent guards. Resource creation and sampling remain unverified.

Three retail launches (one RTX diagnostic and two matched ordinary runs)
and four passing fixtures. AMD stopped at list `+178` (`+328be`); RTX
first hit `+f2cc4b`, before another thread reached `+4ecc821`. Both
ordinary runs issued tokens 1 and 2 and presented only token 1.
