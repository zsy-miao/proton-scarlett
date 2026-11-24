# Outer BVHDecompressor pipelines

Builder SHA-256 `99f803ac22c01ef329dddb1d40be67238d2a812d01e8d040b3cb5135bc23707e`. Root factory call RVA `1458a` → `c2d0`, subobject `+0`, name `XDXR BVHDecompressor`. All four modules embed the same 128-byte RTS0 chunk (SHA-256 `58971e940ec4a276feed390bfbb395fdb3b400bbcc6d208f4615694fed95b641`). It matches the existing 164-byte v1.1 guest signature: node 1, flags `03000000`, b0[19] then root UAV u0, UAV u1, SRV t0; all space zero. There is no shared root with CopyAndCompact.

| `XDXR` PSO | Call RVA → `c470`, member | File / RVA / bytes | SHA-256 |
|---|---|---|---|
| DecompressHeader | `1462c`, `+8` | `34b20 / 35720 / 1194` | `44d603a8a1318282e7bca47a3412aa2e1957e48403f7b3178838622707d42157` |
| LeafRehydration | `14659`, `+10` | `2b5b0 / 2c1b0 / 9568` | `550c003e20ffc172f6eb07a8b8df4bc34fd6d37758d4845b861eb6410afd1eb0` |
| Box16Rehydration | `14686`, `+18` | `28770 / 29370 / 2e40` | `31c7f0594667b0d5cf42759cd974359fa8763de42eb6eab290d3b6eda7a61092` |
| Box32Rehydration | `146b3`, `+20` | `25c40 / 26840 / 2b2c` | `64ec8e3ea7710ddc270f9ccd142c9252769e919c42619dbfb69d8266b77143de` |

## What runs

* **Header:** original compute module, groups of 32. Copies `b0[0]` sixteen-byte records from u0 to u1. Thread zero clears flag bit 24 and invalidates one parent-table slot, using source byte offsets 48/52/56 (tagged node ID, node-base offset, table offset).
* **Boxes:** private root uses two bounded raw UAV views and b0 constants; no core extension. The guest modules use packed half min/max and wave size/count/prefix operations assuming a full wave of 64. One project-owned HLSL port serves both widths. Every group decodes 64 mask words; the high byte is the child count, the low 24 bits indicate literal bounds replacing child-derived coordinates. Child IDs and overrides are packed in separate streams. The port scans the masks in lane order independent of host wave size, merges triangle or internal child bounds, applies overrides, emits parent links, and rounds half bounds outwards. Box16 writes 64-byte nodes; Box32 writes 128-byte nodes with a zero trailer. b0[0:3] are section offset, parent table offset, child base and triangle-link enable.
* **Leaf:** original 1,024-thread module, two iterations per lane, up to 2,048 leaves per group. Decodes packed columns in u0 and 48-byte geometry entries at t0; writes 64-byte leaf records in u1. `0021` selects compute size `9568`/FNV-1 `3ba08d9e2f0a0afe`; `0012` handles the two format-65 byte loads and the 4-aligned i64 stream loads via bounded DWORD accesses on the u0 SSBO. Returning just one byte from the former and reading consecutive DWORDs for the latter matters when a field straddles a word. Descriptor t0 is a bounded structured SRV; u0/u1 are bounded raw UAVs. Additional private slots are b0[3] and a root SRV in space `5358` (hex): aligned guest base low/high and byte extent, mapped to one authorized host suballocation. Cached scalar, x2, x3 and x4 reads reuse the checked range helper; rejected reads return zero. The leaf has no atomic window. Missing `ScarlettBoundedLeafRehydrationV1` declines just this PSO.

## Evidence and limits

`scarlett/tests/decompress.sh XboxBvhBuilder.dll [installed-tool]` checks staged and fixture-linked factories, exact embedded roots, mismatched root/schema/device, size, tail, digest and naming. **12 fenced dispatches per GPU**, RADV and RTX 3070. Every case compares the entire 36 KiB arena, including source, destination and guards:

* Header: 37 distinct records across a group boundary, computed parent invalidation, a shortened u1 and zero work.
* Both box widths: lanes 1, 33 and 63 reconstruct nodes from a four-vertex triangle, compact box and full-precision box; three distinct overrides and parent IDs. Nonrepresentable FP32 extrema check outward half rounding. A second run removes the parent table from the output view but leaves the backing bytes in place.
* Leaf: three paired triangles, nonzero stream and geometry offsets, swizzle/metadata, nonmonotonic indices, and guest addresses across a low-DWORD carry. Cases include R32 and unaligned R16 index triples, a 3×4 transform, a one-byte-short x3 vertex read, clipped output and a one-byte-short x2 index window with vertex zero still valid.

To regenerate only the project box code use DXC `-WX -O3 -T cs_6_0 -E main -Qstrip_debug -Qstrip_reflect -D COMPACT=N -Fo port.dxil bvh_rehydrate_boxes.hlsl`, with N=1 for Box16 (`6f89c179ef11aeac82c7cc638c86226ef1a287d4cbda94201a5532ec0823eab3`), N=0 for Box32 (`2f2f943c84565040155b9bd1a1cc802cc35aa1e219d83a4217607758d7b7b191`). Encode those bytes as the corresponding static arrays in `bvh_rehydrate_boxes_dxil.h`.

**Recorder boundary:** Fixture-owned bindings only. Validate live resources, nonwrapping extents, permissions, complete records, ≤4 children per node, valid child IDs/swizzles, disjoint writes, dependency order and barriers before guest dispatch. Box nodes must read already rehydrated children; this fixture uses separate input and output regions. The mask scan favors correctness over throughput and has not been benchmarked. Malformed streams, NaN payloads, alternate geometry encodings, multi-level dispatch sequencing, and a full acceleration-structure round trip are unqualified. Bounded partial output is not a successful decompression.
