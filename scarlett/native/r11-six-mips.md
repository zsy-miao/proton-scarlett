# R11G11B10_FLOAT deferred target

Device `+c8` requests format 26, layout `0x102`, flags `0x300004`, six mips,
one layer and one sample. Guest allocation size is `0x12c1400`, alignment
`0x100`. Byte offsets below are relative to the surface, not pool offsets.

|Mip|Width × height|Padded height|Offset|Span|Padding bytes|
|--:|:--|--:|--:|--:|--:|
|0|2560 × 1440|1440|0x4b1400|0xe10000|0|
|1|1280 × 720|720|0x12d400|0x384000|0|
|2|640 × 360|360|0x4c400|0xe1000|0|
|3|320 × 180|184|0x12c00|0x39800|0x1400|
|4|160 × 90|96|0x3c00|0xf000|0xf00|
|5|80 × 45|48|0|0x3c00|0x3c0|

All widths fill whole 8×8 tiles. Inside a mip, tiles are in row order;
`r11_mip_byte` maps local coordinates to the four-byte packed texel. Padding
occupies unused rows within the last tile row, not a single trailing range.
An independent texture-computer run verified every visible coordinate and
copied word, untouched padding and guards. `tests/deferred-targets.sh` checks
its six address fingerprints and bounds without needing the reference DLL.

On RTX the retail query returned to `+344188` from the construction path
through `+25cc4e6`. At the following query, global `+7650460` held a wrapper
whose `+8` pointed to the captured owner; owner `+8` was `0x4b0500` elements,
stride 4, and its resource at `+20` was null. The next request from `+25cc557`
was 1280×720, five mips, same format/layout/flags. No six-mip `+360`, `+428`,
descriptor use or dispatch was observed before it. Only the measured six-mip
size query is implemented here.

## 1280×720 five-mip follow-up

Mip zero of the five-level chain is mip one of the six-level table above.
An independent texture-computer creation with five levels gave the same
five byte offsets, packed spans and per-coordinate fingerprints. Total
`0x4b1400` bytes, alignment `0x100`; the 1,227,600 visible words leave
2,480 padding words, all untouched by the reference copy.

With the compressed archive provider the title queried two distinct owners
of this shape (`+25cc557`, `+25cc651`). The second went on to `+360`
(return `+abfe0d`, detail offset zero) and then `+428` (return `+193e2db`),
state 3 and the resource IID. In the captured run the VA was `0xd5ef14000`,
marked WC section offset `+0x13d14000`. This is a placement request, not a
shader use. Only the evidenced five-mip `+c8` and `+360` sizes are enabled.

## Five-level placement on RTX 3070

`0033` extends the 0032 resource-lifetime WC reservation. Retail `+428`
returned one buffer for `[0x13d14000,0x141c5400)` in the original marked
section (state 3, null clear). Mip offsets are relative to this buffer, not
independent reservations. In this run slots 68–78 named the same object:

* `+90` `1a 4 1688 0 0 5 0 0` (all levels), then
  `1a 4 1688 0 mip 1 0 0` for each mip 0 through 4.
* `+98` `1a 4 mip 0` for each mip 0 through 4, null counter.

The descriptor records retain the backing and mip range on copy. Ordinary
texture resolution rejects them. The explicit-address fixture reads native
R11G11B10 typed buffer SRVs and writes packed words through R32_UINT UAVs.
The full SRV uses a chain-relative tile index; a single-level view already
includes its level offset. The fixture checks every texel after VA recycle,
unchanged padding, guards, and bidirectional partial overlap with a live
MSAA color reservation at `+0x14000000`. Hold the lowered table to the fence.
Run `scarlett/tests/rtx-r32-tile.sh STAGED-0033-TOOL rtx-r11`.

The next unsupported call on the constructing thread was `+c8`, 2560×1440
format 19, one mip, flags `0x800002`, return `+344188`. Owner `+20` held the
returned wrapper. No consuming retail shader was observed before this query.

RADV on the same retail path requested five mips at VA `0xda8288000`,
`+0x13c88000` in the original native WC pool (base `0xd94600000`). Device
`+428` returned success at `+193e2db`, state 3, null clear, resource IID.
The full `0x4b1400` range uses the same cross-format reservation as the
other tiled buffers. `rtx-r32-tile.sh TOOL radv-r11` checks all five mips,
all 2,480 padding words and both 64 KiB guards using a fenced raw copy of
the original pool after exact VA replacement. A partial MSAA overlap is
rejected until fence retirement and release of the private mip table.
Retail wrote one full SRV, five single-level SRVs and five UAVs for that
same object before its `+c8` query for 2560×1440 format 19, one mip,
flags `0x800002`, layout zero, returned `E_NOTIMPL` (`+344188`).
