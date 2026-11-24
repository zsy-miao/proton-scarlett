# PT_LoadInstances

Constructor call `XboxBvhBuilder.dll+20568` selects `XDXR LoadInstances`,
RVA `+ba3f0`, size `0x752c`. The adapter creates the original compute shader
after checking the entire span's SHA-256, the utility root's owner, and the
`ScarlettBoundedLoadInstancesV1` capability. The previous writer uses the
separate `ScarlettBoundedInstanceReadsV1` marker. Missing markers fail closed.

The guest bindings are SRV `t0` (TLAS), UAV `u0` (24-byte structured AABB
per active dispatch X), and 16 DWORDs `b0` (partition ID at byte 0, centroid
VA at 8, TLAS VA at 16). In addition the host compiler requires **both** a
private root SRV `t0` and a private root UAV `u0` in space `0x5358`. Six root
DWORDs `b0` in that space carry the `(lo,hi,extent)` of the read and atomic
windows, respectively. Cached guest loads map to the SRV; atomic min/max of
box and centroid bounds map only to the UAV. A rejected address yields zero
for the full load or suppresses the atomic. Only the first active lane of
each wave updates the two bounds records. The instruction lowering assumes
non-NaN bounds; console denormal, NaN and signed-zero equivalence is unverified.

There is **no fifth recorder**. Guest lists cannot bind the private root or
dispatch this PSO. The compiler guards embedded VAs, but does not validate
ordinary root descriptors or derive retail resource extents and ownership.

`tests/builder-pipelines.sh` extracts the matching local input for
`test-bvh-load-instances.cpp`. The fixture calls the staged DLL's public
factory, checking rejected mutations, cross-device ownership, name and
size. It then creates equivalent private host PSOs to check actual writes
without exposing objects from the adapter. It compares four complete
2,048-DWORD buffers after every fence: sparse global partitions, 33 triangles
across two groups, float/packed-half boxes, directional lookup, invalid
nodes, empty work, truncated reads/atomics, misalignment and guards.
`-DTEST_NO_LOAD_COMPILER` tests refusal on a fourth-only core.
