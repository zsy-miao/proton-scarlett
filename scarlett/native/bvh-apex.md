# ApexPointMapComputer

Pinned `XboxBvhBuilder.dll` SHA-256 `99f803ac22c01ef329dddb1d40be67238d2a812d01e8d040b3cb5135bc23707e`. After FindTreelets, the root call at RVA `df88` names `XDXR ApexPointMapComputer`. Calls below pass `[r14]` to `c470`, with PSO destinations relative to `r14`. All three original containers contain the same 104-byte RTS0 chunk. The 140-byte canonical guest serialization is v1.1, node 1, flags `03000000`, no samplers, root UAVs u0,u1 and six b0 DWORDs (space 0, visibility ALL, descriptor flags NONE).

| XDXR stage | Constructor call → slot | RVA / file offset / size (hex) | Whole-container SHA-256 |
|---|---|---|---|
| ResetKDOP | `e00d` → `+8` | `3ab00 / 39f00 / 10d0` | `f40bd5e038ad2613575582e1af36d0a2035fdc70884a71e2e26ab3d46e34c05c` |
| BuildKDOP | `e038` → `+10` | `370c0 / 364c0 / 1d5c` | `5a20f13b9d180a6fc75f5faead2caed291439d715f04dbceb7f897ea2979f55d` |
| BuildApexPointMap | `e063` → `+18` | `38e20 / 38220 / 1ce0` | `a1f7d60c9f0a73aab8523ca34cdba940d6d1cd87da9afa282f2599a805bdb447` |

All groups are 32×1×1. Constants encode BVH and KDOP bases in units of 256 bytes, tessellation `k`, stage-specific element count `N`, and a 64-bit optional element VA.

- **Reset:** store `(-FLT_MAX,+FLT_MAX)` to `u0[i]` for `i<N`. Private slot 0 is a bounded structured UAV table (stride 8), slot 1 is the six DWORDs. No compiler change for this shader.
- **Build:** read `H+44`, where `H=uint64(b0.x)<<8`. If nonzero or `N==0`, use the count at `H+12`. The optional element VA defaults to `H+DWORD[H+52]`. Each lane reads one 64-byte record. Type at `+60` selects box (lower/upper float3 at `+16,+28`) or triangle (three float3 from `+0`, optional fourth at `+36` for header-selected paired leaves). For `3*(k+1)^2` directions, reduce the projections and atomically accumulate conservative max/min pairs at `uint64(b0.y)<<8`. Private slot 0 is a CBV preserving mixed-width loads; slot 1 is six DWORDs at b0 space `0x5358` (read and atomic base lo/hi/length); slots 2 and 3 are their separate resolved SRV and UAV. Reuses checked triangle scalar/triple reads and float atomics: a short triple returns three zeros and a short atomic is suppressed.
- **Map:** read the four KDOP max/min pairs bordering each cell. Each three-plane intersection gives one float3 apex; two triangles per cell, both max and min sets. The min-set base is `6*k*k` float3s. Private slot 0 is a bounded u0/u1 table with strides 8/12; slot 1 holds constants. Reuses BVH4 finite median lowering.

The adapter checks exact root, device ownership, size and SHA. Only the two build PSOs require `ScarlettBoundedApexV1`; the core selects existing lowering by compute stage, size and hash. No new dxil-spirv patch.

`scarlett/tests/apex.sh XboxBvhBuilder.dll [staged-tool]` checks staged and linked factories, rejects BuildKDOP with the public host root alone, and fences every private dispatch. Reset covers two groups, zero count and a clipped UAV. Build covers mixed triangle/box reduction, header-selected fourth vertex, a one-byte-short upper-box read across a low-word carry, a one-byte-short final atomic and no work. Map covers all three faces, empty and clipped output. Every output point must satisfy its three independent plane equations; extrema are compared to CPU projections. Other arena DWORDs, including guard regions and input tables, must match exactly.

**Limits:** isolated host dispatches, finite inputs and `k=2` for both build stages. No retail recording, consecutive pass synchronization, contention across active waves, general nonfinite inputs, or extreme tessellation qualified. Recording must validate the count, `k>0`, products and offsets without wrap, the 40-bit base encoding, actual table and leaf capacities, and coherent read/atomic mappings with permissions and fence lifetime. Separate header and leaf allocations that cannot fit one authorized read window must be rejected, not bridged by inventing a span.
