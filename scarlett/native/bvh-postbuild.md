# PostBuildInfoQuery

The builder constructor calls `+1d400` at `+e070` on its `+8` member. The other call at `+21563` uses the same subconstructor. There is one root (`+1d4b2` → `+c2d0`, `XDXR PostBuildInfoQuery`) and one PSO (`+1d4fe` → `+c470`, `XDXR GetBVHCompactedSize`), stored at subobject offsets 0 and 8. Query modes select branches of this module.

| Identity | Value |
|---|---|
| XboxBvhBuilder.dll SHA-256 | `99f803ac22c01ef329dddb1d40be67238d2a812d01e8d040b3cb5135bc23707e` |
| Module file offset / RVA / length | `0x90d80` / `0x91980` / `0x14a4` |
| Module SHA-256 / vkd3d FNV-1 | `7c9375fc0c72f7ac09eda6d96b156452ff9b167697aafad524d36abbf4ee66a1` / `827443b28f6865ee` |
| Root chunk | At module `+0x12c`; 128 bytes including RTS0 header; SHA-256 `8f016626e013d5e0c0c6d783bbcbf1e2ba54f0e7641bef2d28fb550c88f2c7be` |
| Guest root | Existing exact 164-byte v1.1 DXBC, flags `0x03000000`, root UAVs u0/u1/u2 then four b0 DWORDs, all space 0 |

## Shader outputs

One thread, one group. b0[0:1] holds the little-endian guest header VA, b0[2] the mode; b0[3] is unused. Let `n = uint32(header[+4] + 1)` and `S = uint64(n) * 64` (offsets in bytes).

| b0[2] | Result |
|---|---|
| 1 | Compacted size: `u0[0] = S` as a QWORD. The recorder at `+1d540` writes mode 1, binds u0, writes the header VA and dispatches (1,1,1). |
| 2 | `r = (int32(header[+0]) < 0 ? 0 : header[+12])`. Two QWORDs: `u0[0] = S + uint64((8*r + 311) & ~255u)`, `u0[1] = r`. The shift, addition and mask inside the parentheses are 32-bit. |
| 3 | `u1[0] = 4*n`, `u2[0] = n`, both DWORDs. |
| Other | No output. |

No substitute shader. Private root layout: table of three bounded structured UAVs (strides 8/4/4), b0 as a CBV (the module loads it as both i64 and i32), three private DWORDs `[guestBaseLo, guestBaseHi, bytes]`, and a root SRV for the host start of that range. The last two use space `0x5358`. Core selector reuses `DXIL_SPV_OPTION_BOUNDED_XBOX_HEADER_READ` for exact size/hash; no dxil-spirv change. The helper treats opcodes 356 and 364 as DWORD reads, maps an aligned in-range delta to the host SRV, and otherwise returns zero. The factory requires the owned PostBuildInfoQuery root, exact shader digest, and `ScarlettBoundedPostbuildV1` on the loaded core.

## Qualification and limits

`scarlett/tests/postbuild-query.sh` checks the staged and fixture-linked factories, embedded root, wrong same-size schema, wrong registered root/owner, size, tail, digest and PSO name. Ten single-group dispatches check the entire guarded 512-byte output after a fence. Examples: 37 blocks yield 2368 bytes; with 37 serialized references the pair is `(2880,37)`; a negative header tag gives `(2624,0)`; mode 3 yields `(148,37)`. The cases cross a low-DWORD address carry, exercise a size above 4 GiB, restrict each cached load by one byte, test misalignment, truncate only the u0 descriptor, and check an unhandled selector. The expected failure using the public guest root alone occurs at the i64 constant load.

**Dispatch limit:** The fixture owns the host bindings; guest command recording and retail dispatch are unqualified. A recorder must resolve and retain same-device live nonwrapping read and write spans, prove capacities (8 or 16 bytes for u0, 4 each for u1/u2 as applicable), enforce alignment and resource states, and order the query after header writes. Zero on a rejected read can produce a plausible size (e.g. 64), and a short destination can yield partial output: reject incomplete bindings before dispatch rather than reporting these as successful queries.
