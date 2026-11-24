# RearrangeElementsPass

`XboxBvhBuilder.dll` SHA-256 `99f803ac22c01ef329dddb1d40be67238d2a812d01e8d040b3cb5135bc23707e`, constructor RVA `0x1f530`:

| PSO | RVA | File offset | Span | SHA-256 of complete container |
| --- | ---: | ---: | ---: | --- |
| XDXR RearrangeTriangles | `a4780` | `a3b80` | `1c80` | `d29126593cb2d04e6b66333cb28775981eb3b8edebd38d0dc2cee9aae489a045` |
| XDXR RearrangeBVHs | `a2fe0` | `a23e0` | `1794` | `29633083515315f92233ce4b7cf056ec7a0e0adbf0dbd570e418eced3ca9f8f4` |

**Exact public root schema:** `XDXR RearrangeElementsPass`, v1.1, node 1, flags `0x03000000`, no static samplers. In order: seven root UAV descriptors `u0..u6`, then four DWORD constants at `b0`. Space 0, visibility ALL, descriptor flags NONE throughout. Serialized size 260; the 216-byte RTS0 payload matches both shaders. The wrapper must belong to the device creating the PSO. A root-schema table should associate both SHA-256 rows with this same schema, never with every 260-byte root.

Both original kernels launch 32 lanes in X. Let `H` be the guest VA in `b0.xy`, `i` the dispatch thread ID, and `N` the element count. For `i<N`, `j=u2[i]` (DWORD indices) and `dst=max(N,2)+i`. No wave ops or guest writes through `H` are needed.

* **Triangles:** `N=DWORD[H+12]`. Read `u0[j]` (64 bytes); when `b0.z!=0` also copy the entire record to `u3[i]`. Write a 24-byte AABB (lower then upper float3) to `u6[dst]`. When DWORD 15 is zero the bounds are already present at bytes 16..39. Otherwise take component-wise minima/maxima of the first three float3 vertices; include the fourth vertex if DWORD 12 has bit `0x80`. The partition selector is unused.
* **BVHs:** copy the 24-byte box `u6[j]` to `u6[dst]`. `b0.w=~0u` uses `DWORD[H+12]`. Otherwise the kernel reads DWORDs at `H+64` and `H+68` (the second value is unused). For an ordinary selector `p=b0.w`, `N=DWORD[H+(80+28*p)]`. For `b0.w=~1u`, `p=DWORD[H+64]` and add `popcount(DWORD[H+76])` to that count. The offset expression and count addition are unsigned DWORD arithmetic. Live indices must reference the source half; the two halves do not overlap.

**Private host bindings:** slot 0 is a descriptor table of seven structured UAV views with explicit extents. Used strides: `u0/u3=64`, `u2=4`, `u6=24`; `u1/u4/u5` are unused. Slot 1 is a root CBV containing the original 16 bytes of `b0` (the shader loads `xy` as i64). Slot 2 is three private DWORDs `(guest base lo, hi, byte length)` at `b0,space0x5358`; slot 3 is the corresponding host header SRV `t0,space0x5358`. The bounded converter remaps scalar/global DWORD loads; a miss returns zero. No guest VA becomes a host pointer. Future recording must resolve and retain all views, validate dispatch/index storage and disjoint gather regions, provide a coherent header window, and retire resources after the fence.

Apply `vkd3d-patches/0012-vkd3d-bounded-rearrange.patch` and `dxil-spirv-patches/0007-bounded-rearrange.patch` to the Morton/bitonic backend before building the core. The `ScarlettBoundedRearrangeV1` export gates the two exact PSOs. `scarlett/tests/rearrange-pipelines.sh XboxBvhBuilder.dll [installed-tool]` builds a fixture against the adapter source. It exercises public root/PSO creation and dispatches those exact host PSOs on bounded fixture resources. It does not implement guest command-list recording.
