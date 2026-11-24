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
