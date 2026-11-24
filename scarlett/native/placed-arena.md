# Device `+0x428`: startup arena buffer

`2478e22 -> 5c5ff00 -> 1e86440` creates a 185 MiB reservation. After the
five physical maps, `1e867e3` subtracts the first two extents' byte count.
`1e8684c` calls device `75df048`, using the **third** extent base in RDX,
buffer descriptor in R8, state `0xffffffff`, null clear, resource IID
`63fa690`, and a local out pointer. The logged descriptor has alignment
zero (set at `1e86806`), width `0xae00000`, height/depth/mips/sample count 1,
sample quality/format 0, layout 1, flags `0x20004`. Don't convert those
last flags/state to desktop values by truncation. On success `1e86870`–
`1e868bb` initializes owner `75df0e8` with width/4 elements, stride 4,
stores the result at owner `+20` (`75df108`) and the base at `+38`.

The address arithmetic agrees with saved trace `scarlett-run.4Xezp8O2`,
`steam-999001.log:3803,3945,4087,4229,4371,4513,4827–5395,5537`.
Ranges below are relative to **placement**, in MiB, not physical ID order.

| Range | Page IDs relative to start of run | Contract |
|---|---|---|
| `[-11,-1)` | `[2352,2512)` | excluded WB source |
| `[-1,0)` | `[2512,2528)` | excluded WC source |
| `[0,27)` | `[2528,2960)` | guest code/data source; shim currently provides code snapshots only |
| `[27,74)` | `[0,752)` | WC data heap |
| `[74,174)` | `[752,2352)` | WB data heap |

**Why not return a single resource?** `24792f4 -> 5bdb830` operates on
that very owner, clears the inline-view bit and feeds `{0,UINT_MAX,0}` to
both the SRV and UAV constructors. `5bdb950` builds a raw buffer SRV;
`6eb7ec`–`6eb800` clamps the UAV count to the owner's remaining elements.
`554e350` clamps the SRV, `6eb9fc` loads the placed interface for the UAV
writer. Both describe 45,613,056 raw dwords starting at zero; `50cc5c2` and
`50cc615` conditionally install their wrappers. These are whole-owner
views, not proof that every address was accessed. Even a read-only raw SRV
over the full owner cannot be produced from the three heaps; a whole-buffer
UAV would additionally expose the code store. Existing `xs_bind_view_buffer`
registers one actual resource, and `xs_write_view` accepts no non-null UAV.
A copy would cease to alias title/queue writes. The display ring is unrelated.

**Smallest tested consumer:** the paired indirect Dispatch paths in
`command-signatures.md` pass `[75df108]` and `A-[75df140]` to list `+1e8`.
Resolve the four-byte-aligned 12-byte operand against *one* data extent and
record its heap-relative offset. Reject crossing, code and out-of-bounds
operands before recording. Other `+1e8` callers use Draw/DrawIndexed/DispatchMesh
(16/20/12-byte records). They are not tested here.

**Copy-source blocker:** `24792f9 -> 247ac20` associates the first covered
extent with the chunk allocator at `75df1b8`. `4f458a`–`4f45fd` copies CPU
bytes into one such chunk, then passes the interface, chunk-relative-to-base
offset and byte length to list `+78`. `4f4379`/`4f4819` also use it in a
pitched copy location. This requires live GPU reads of `8404` *as data*.
Current `ImportedReadSpan` only accepts `1004/1404`; ordinary imports refuse
`8404` too. Shader snapshots (limited to 64 KiB) cannot substitute. A
bounded source-only copy operation must reference the retained original
backing through retirement, validate the destination/footprint too, and
expose no handle or GPU VA to callers. Until then fail these uses.

`xmem/test-descriptor-pages.cpp` tests this operand against the known live
WC/WB imports, with the code-store fixtures and the actual 2,960-ID layout.
It executes both records **after** unmap/release and exact-address reuse of
both data extents; the imports and PSOs stay alive through the queue fence.
It also rejects an undivided 174 MiB ordinary import and a crossing import.

**Factory blocker:** the real consumer must own a same-run logical resource.
Acquire its parts together under XMem's page lock at creation, retaining
backing identities (no code handle for buffer use); independent lookups
by address can mix map generations. Keep the logical view in the descriptor
record and lower validated loads/stores at binding or shader translation
to extent-local operations. Reject untranslatable shaders/operations,
including writes to code. Retain the bindings through list/queue retirement.
No such whole-view consumer exists on this baseline. The range test exercises
the operand translation, not the startup descriptor/command-list ABI;
the production factory should continue to fail with null output.
