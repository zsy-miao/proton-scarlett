# ConstructHierarchyPass: BuildSplits 0 and 1

In `XboxBvhBuilder.dll` SHA-256 `99f803ac22c01ef329dddb1d40be67238d2a812d01e8d040b3cb5135bc23707e`, the BVH4Builder constructor returns at RVA `dc68`. The next member at builder `+1b0` is `XDXR ConstructHierarchyPass`. At `dd18` the constructor calls `c2d0` with this member as root destination and name at `cd388`. Both calls below pass that member's root to `c470`.

| Name | Call RVA → member slot | Container RVA / file offset / size (hex) | Whole-container SHA-256 |
|---|---|---|---|
| XDXR BuildSplits 0 | `dd7f` → `+8` | `5ddd0 / 5d1d0 / 1e68` | `8b441bd55ffe5a4f9ea6dda93ac4806a8bc8abf023f4762de793ad1f1158a873` |
| XDXR BuildSplits 1 | `ddaa` → `+10` | `5bd30 / 5b130 / 2094` | `303bf355c740562caf01faea8435a49cfce034a99ca9d7c1588d2a207db7e170` |

Both embedded RTS0 chunks at container offset `12c` match the same 164-byte serialized public root (compare the 128-byte chunk after the standalone DXBC header). Version 1.1, node 1, flags `03000000`, no samplers. In parameter order: **u1, u2, u0**, then three DWORDs at b0; space 0, descriptor flags NONE, visibility ALL. `BuildSplits 1` shares its size with `BitonicInnerSort 1`, not its digest or root.

Both originals dispatch 32×1×1. `b0.xy` is a uint64 header VA `H`. `b0.z = ~0u` selects the count at `H+12`. Otherwise the shader reads `H+64,+68` (the second result is unused), and loads the count at `H+(80+28*p)` with 32-bit offset arithmetic. Ordinarily `p=b0.z`; `~1u` selects `p=DWORD[H+64]` and adds `popcount(DWORD[H+76])` to the count.

For `N>1`, the kernels find binary radix splits over sorted Morton codes (`u0`, stride 4 or 8); duplicate codes use index bits to break ties. They write parent indices (`u1`, stride 4) and pairs of children (`u2`, stride 8). Internal nodes occupy `[0,N-2]`, leaves start at `N-1`, and parent[0] is 0. At `N=1`, parents are `(0,0)` and children are `(1,~0u),(~0u,~0u)`; zero count writes nothing.

**Private bindings:** slot 0 is a bounded structured UAV table in register order u0..u2; slot 1 is a CBV preserving the mixed-width b0 loads. Slots 2 and 3 are the existing checked header-read ABI: `(base lo,base hi,byte length)` at b0 and the resolved read-only buffer at t0, both in space `0x5358`. Backend patch `0017` selects the existing scalar/global DWORD lowering for these two additional exact size/FNV pairs; the public factory requires SHA-256, root kind, device ownership and `ScarlettBoundedHierarchyV1`. No nested compiler patch or guest-VA fallback.

`scarlett/tests/hierarchy-pipelines.sh XboxBvhBuilder.dll [staged-tool]` checks the staged and linked factories, then dispatches the linked private PSOs. Each fence is followed by a full arena comparison: keys, parents, both child words, untouched tails and guards. The oracle builds the ordered tree top-down, independent of the GPU's per-node searches. Cases cover zero, singleton, multi-group duplicate-rich keys (including differing high DWORDs), ordinary and special partitions, and short scalar/global read windows with low-word carry.

**Limit:** no guest recorder or retail constructor progression is established. A recorder must resolve and retain coherent, correctly permissioned spans; validate key order/domain, partition arithmetic and counts before dispatch; and size disjoint key, parent and child views for the effective count (including singleton). In particular, a rejected read returns zero, which does not guarantee an invalid `~1u` job is empty: the separately loaded popcount can still contribute. The fixture does not cover malformed keys, extreme counts or in-flight aliasing.
