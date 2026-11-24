# XDXR InitHeaders

The constructor loads `XboxBvhBuilder.dll` RVA `+b6e50` at `+205db` and sets
the length to `0x1488` (5,256 bytes). This is not `InitPartitionedHeaders`.
The factory checks the original container's SHA-256, the known device-owned
root and the `ScarlettBoundedInitHeadersV1` compiler marker. It submits the
original bytecode. The compiler opts in for key `ddcdabacd472f524`, CS, and
the private bounded-write root layout.

List helper `+20ea0` allocates `40 * count` bytes aligned to 16, binds the
allocation at parameter 0 and writes count at parameter 2. It dispatches
`ceil(count/32),1,1`. The shader indexes t0 by `DispatchThreadID.x` below
b0.x. Each entry is two guest VAs (header, optional extra box) followed by
six DWORDs: flags, CRC, element count, remap offset, elements offset and
parents offset. Four x4 stores initialize 64 header bytes. A nonzero second
VA receives two x3 stores (24 bytes). No shader access goes through guest u0.

`xs_record_bvh_headers` accepts up to 64 records. It snapshots the complete
input and rejects a misaligned, out-of-bounds or overlapping destination
before recording. The private root binds t0, one DWORD at b0, and three
range DWORDs plus a root UAV in space `0x5358`. The compiler translates only
complete in-range stores. One mapped window can contain both outputs. With
two disjoint windows it runs the same batch once per window: each store
executes exactly once, and the original addresses remain unchanged. The
caller supplies same-device, nonaliased UAV-state mappings and releases the
returned hold only after retirement.

`test-bvh-headers.cpp` runs through the public factory and the private
recorder. It tests a 33-record split-window batch (including null boxes,
a 32-bit address carry and exact end-of-view stores) and a single-window
record. All 1,024 seeded DWORDs are checked after the fence. Use
`scarlett/tests/builder-pipelines.sh` with the matching private DLL, or
compile with `-DTEST_NO_HEADERS_COMPILER` and use an older core to check
refusal. Guest command-list dispatch is not qualified by this test.
