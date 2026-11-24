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
The normal adapter rejects the first unsupported visible descriptor; it does
not include the diagnostic address/description responses or metadata writes.

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
