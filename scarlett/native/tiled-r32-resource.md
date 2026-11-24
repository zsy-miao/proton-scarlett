The RTX 3070 `+428` request names an original `0x384000`-byte WC range,
guest state 3, null clear and the resource IID. A float-only experiment used
offset `0x10e00000`. In combined retail traces RTX requested `0x12600000`
with one preceding BGRA target and placed the buffer at `0x13600000` with
both preceding targets. The test-only shader
uses a private reference to this buffer. It reads and updates all 921600
guest-swizzled words after the wrapper is released and the VA is reused.
Unqualified descriptor writes must leave both neighboring slots untouched.

A separate diagnostic retail run blocked further GPU submissions before
recording CPU descriptor metadata. Same guest wrapper, visible slots:

|Slot|Method|Wire (hex)|
|---|---|---|
|59|`+90` SRV|`2a 4 1688 0 0 1 0 0`|
|60|`+98` UAV|`2a 4 0 0`|
|61|`+230` SRV by address|`43 4 1688 0 0 1 0 0 0 0 VAlo VAhi 0 0 0 3`|

The actual CPU consumer is a second view constructor. `+1edb3c3` calls
through `+f7d342` and `+501f6b3`. It queries this resource's `+58`
(return `+4ecc936`), stores the address in the new view record, then calls
`+50` (return `+501f80e`) and overrides format and flags. Supplying the
captured descriptor and still-matching original guest VA in the diagnostic
reached device `+230`, return `+4ecc821`: format `0x43` (R9G9B9E5_SHAREDEXP),
flags `0x300000`, same extent, layout `0x102`, offset and section span.
No shader used those descriptors in this capture. Returning a native buffer
VA or texture snapshot cannot implement this aliased typed read/write path.
The RTX staged adapter now accepts these three exact wires. Other resource
methods and unknown descriptor shapes remain unsupported.

On RADV 26.3.0 the combined 0031 allocation order put this target directly
after the 1440p float image: WC base `0xd94600000`, VA `0xda7b80000`,
offset `0x13580000`. `+c8` and `+360` both returned `0x384000` / `0x100`;
`+428` used state 3, null clear, resource IID. Acquire the live native heap
and place the UAV buffer at that exact offset. The desktop optimal image
requires `0x3c0000` and cannot fit. `rtx-r32-tile.sh TOOL radv` checks all
921600 words against both a linear shader readback and a fenced raw copy of
the original native pool, plus surrounding guards and the replacement VA.

The RADV CPU-only trace observed the same SRV/UAV wires at slots 62/63;
`+58` returned the original VA after the native heap identity was rechecked.
`+50` then led to the same `+230` address SRV at slot 64, format `0x43`.
The ordinary provider still rejects the first texture SRV (return `+164f01`).

Patch `0032` keeps the exact range reserved in the same registry used by
the other color targets. On both adapters a gated GPU copy retained it after
the application released its last reference. An overlapping BGRA placement
failed until retirement; a neighboring range and a different pool succeeded.
Recorders using a raw GPU address must explicitly retain the buffer.

### RTX typed view lowering over the shared 0032 lease

`ScarlettTiledR32BufferV1` is the only import path. Combined captures put
the first RTX R32 at `+12600000` or `+13600000`, depending on the second
BGRA placement. A second R32 was observed at `+13990000` in the combined
no-submit trace. `+10e00000` was a different, isolated run. The tested RTX
fixture uses `+13600000`; the existing RADV control uses `+13580000`.

`xs_r32_texel_table` validates three adjacent, serial-tagged records in the
owner’s visible heap and requires one identical backing resource. It creates
a private three-entry table: BUFFER SRV R32_UINT, BUFFER UAV R32_UINT,
BUFFER SRV R9G9B9E5_SHAREDEXP. The table retains the exact 0032 resource
through a private-data COM reference. A recorder must hold this table until
its last queue fence completes. The private path supports integer mip-zero
loads and stores lowered to the verified tile index. It does not implement
arbitrary Texture2D sampling or publish a buffer as a guest-visible texture.

`scarlett/tests/rtx-r32-tile.sh STAGED-0032-TOOL rtx-typed` checks all
921600 UINT words and native RGB9E5 values, both before and after a typed
UAV rewrite. With the public wrapper released and VA reused behind a queue
gate, overlapping reacquisition fails; it succeeds after the fence and
release of the table. Both 64 KiB guards and replacement bytes are checked.

In a combined CPU-only trace, descriptor indices were 61–63 and 64–66 for
two adjacent groups rather than the isolated run's 59–61. The RGB view
resolved to the same original section as the UINT views. No table bind or
shader execution consuming either group was observed before the following
2560×1440 format-86 allocation query (`+c8`, return `+344188`). The provider
keeps ordinary guest binds disabled until the consuming shader ABI is known.

### Retail construction and consumption guard (RTX)

The wrapper keeps the section handle used for the 0032 placement. Writes
validate the current section identity, VA and byte offset; address writes
must find exactly one matching live wrapper. The evidenced `+50` and `+58`
calls return the guest descriptor and VA. Neither returns the host buffer VA.
The three descriptor kinds hold independent resource references. A checked
one-slot copy from staging takes a new serial and reference. Overwriting or
destroying a slot drops just that reference; the private lowered table takes
its own reference before leaving the registry lock.

The guest bank carries inert descriptors. Ordinary slot, range (including a
range starting before the tile), and view resolution reject tiled records;
they are not refreshable WC textures or placed raw buffers. Binding the
startup heap pair alone does not consume any slot. Neither guest list vtable
admits a shader table bind, draw, dispatch, bundle or indirect execute; the
unimplemented slots stop before host recording. Only the private test shader
uses the lowered table. Any future bindless, indirect, or raw-list recording
path needs a consumption guard before it can admit these heaps.

With the separate Present bridge enabled in scratch, the token-1 clear
batch retired and the title called PresentX at `+24824c7`. The two alias
triples were constructed with no submission freeze. The next failure on
the constructing thread was `+c8`, 2560×1440 format 86, flags `0x300005`,
return `+344188`. No consuming retail shader layout was observed by then.

### RADV retained heap and packed-color fixture

On the RADV 26.3.0 `1002:164e` path, 0032 returns a placed UAV buffer on the
original native heap. Keep that heap identity in the wrapper and verify it and
its byte offset on each new view/address request. Existing descriptor records
and private tables own the original buffer independently of the guest mapping.

RGB9E5 lacks texel-buffer support on this device (`Support1=0x2e4d3f0`,
`BUFFER=0`). The fixture requests a three-integer-descriptor table explicitly:
R32_UINT SRV, UAV, and another SRV of the same word. Its test shader lowers
integer coordinates to the guest tile index and decodes each 9-bit mantissa
with scale `2^(exponent-24)` and alpha 1. Ordinary guest table consumers
remain blocked. `scarlett/tests/rtx-r32-tile.sh TOOL radv-typed` compares
both reads before/after a UAV rewrite for every pixel, checks the retained
raw bytes and guards after exact-VA reuse, and exercises the reservation
through fence retirement. The original RTX float-returning table remains a
separate private entry point.

Unmodified RADV retail constructed two triples at offsets `+0x13580000` and
`+0x13904000`. All six writes succeeded, each triple sharing one original
buffer. The constructing thread next failed the `+c8` query (return `+344188`)
for 2560×1440, format 86, layout 0, flags `0x300005`, `E_NOTIMPL`.
