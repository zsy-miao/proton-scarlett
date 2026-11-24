# First XboxBvhBuilder utility root

The title's `+247944f` calls the imported constructor with the device and
`false`. `+2479455` is its return address. The native DLL shipped with the
title (April 2025 GXDK Update 5) takes the branch at `+21374`. Its call chain
is `+2139c -> +d850`, `+d89c -> +20270`, `+203b2 -> +c2d0`.
The helper ORs `0x03000000` into the flags and calls the forwarded desktop
`D3D12SerializeVersionedRootSignature` at `+c32f`. At `+c388` it invokes
device `+0x80` with `(This, UINT nodeMask, const void *data, SIZE_T bytes,
REFIID iid, void **out)`. On success it invokes root `+0x30` with the name
`XDXR UtilityShaders`. The helper releases its own serialized blob.

The first evidenced input is node 1, `IID_ID3D12RootSignature`, one 140-byte DXBC
RTS0 (SHA-256 `8cb102b5bdc14649e4945275bc672894443646b915e89fbc3b8a981a9ec81a4b`).
Match independent serialization of v1.1 SRV t0 / UAV u0 / 16 DWORDs b0,
space 0, visibility ALL, no root-descriptor flags or samplers.

The [root registry](bvh_root.h) covers all 16 distinct layouts in the pinned
builder. `+0x80` compares the complete canonical wire before serializing the
same ordered parameters with desktop flags zero. `test-bvh-root-registry.cpp`
checks the public slot against all 52 embedded RTS0 chunks and the matching
constructor name literals, including a correctly serialized but misordered
`ConstructHierarchyPass` rejection. Root creation alone does not establish
that the associated shaders can compile or dispatch on the host.

The synthetic `test-bvh-root.cpp` links the adapter objects like
`test-allocation-info.cpp`. Compile `test-bvh-root.hlsl` with `dxc -T cs_6_0
-E main -Fo test-bvh-root.dxil` and place the DXIL beside the fixture EXE.
The fixture needs no captured bytes. It dispatches one group, reversing 16
distinct SRV words and XORing each with a different root constant; the UAV
is copied back after a transition and read only after the fence.

The next retail call is a separate operation: DLL `+204b7 -> +c470`, then
`+c4e1` submits the first compute PSO to device `+0x1f0` with the root and
shader RVA `+c96b0`, length `0x1410` (`XDXR InitPartitionedHeaders`).
At this call the root matched the wrapper returned by `+0x80`; both extra
arguments were null, the IID was `ID3D12PipelineState` and no cached
pipeline was supplied. This identifies the first requested compute pipeline;
it does not establish shader compatibility or execution.

The executable DXIL is compute shader model 6.5, target `dxil-ms-xb2`,
wave32. Direct submission on RADV with the translated root returned
`0x80004005`, null pipeline, and dxil-spirv's `Unhandled instruction!`.
The two Xbox store operations take their destination from the first
64 bits of the 40-byte initializer at `t0`. The UAV `u0` is not that
destination. A scratch translation tested partition counts 0, 1 and 5 on
RADV and RTX 3070: all 96 destination words (including guards and
untouched fields) matched after retirement; all 96 decoy UAV words
remained unchanged. That test supplied a valid host address. A retail
dispatch still requires a checked translation of the embedded title address.

The installed `+0x1f0` creates the first PSO using [project HLSL](bvh_init.hlsl)
after matching the descriptor, root owner and SHA-256 of the guest shader.
The private recorder reads 40 immutable input bytes and uses a bounded UAV
for the resolved destination; the host shader never receives a guest address.
`test-bvh-pipeline.cpp` checks all 96 seeded words after dispatch, including
the untouched parts of each partition. Build it like `test-bvh-root.cpp`,
supplying the matching shaders as `private-builder.bin` and `private-offline.bin` beside the fixture outside the repository.
The guest command-list dispatch path is still unqualified.

Run `scarlett/tests/builder-pipelines.sh /path/to/XboxBvhBuilder.dll` on the
test host to rebuild the adapter and run the builder regressions. The script
verifies the DLL hash and uses private temporary shader inputs; the fixtures
do not run the retail executable. The default test GPU is RADV.

`bvh_init_dxil.h` contains only the compiled project shader. To regenerate
from `scarlett/native` with DXC 1.9 (b106a961):

```sh
dxc -T cs_6_0 -E main -Qstrip_debug -Qstrip_reflect -Fh /tmp/bvh-init.h -Vn bvh_init_dxil bvh_init.hlsl
sed -n '/^const unsigned char bvh_init_dxil/,$p' /tmp/bvh-init.h | sed '1s/^/static /;s/\r$//' > bvh_init_dxil.h
```

Retail next calls the same device slot with `XDXR WriteOfflinePartition`,
DLL shader RVA `+c77e0`, size `0x1ec4`. This second request occurs inside
the constructor, before any observed dispatch of the first pipeline.

`WriteOfflinePartition` uses addresses differently from the first shader:
each 16-byte command at `t0` supplies a partition ID, a node count, and
a read-only address for the 128-byte source nodes. Stores go through `u0`.
The parent-link address can lie below the parent-table anchor. With an
anchor at byte 4096, the tested internal child updated byte 4056. Bounds
checks must use the destination allocation rather than assuming all
links follow the anchor.

A scratch translation using known live host addresses passed fenced
readback on RADV and RTX 3070 for 0, 2 and 33 commands. All 1,280 words
of the destination and both unchanged inputs were checked. The installed
factory uses [project HLSL](bvh_offline.hlsl) for the exact second shader.
The private recorder validates the batch before recording and snapshots
source nodes from a resolved upload-buffer range. The public-slot test
also verifies the write at byte 4056. Retail passes creation and next
requests `WritePartitions` (RVA `+c6260`, 5,504 bytes).

### ResetCounters: verified ahead of retail

`ResetCounterValues` (builder RVA `+b82e0`, 4,072 bytes) compiles directly
with the pinned desktop backend. The factory submits the unmodified shader
after checking its digest and root identity. It writes four identical DWORDs
per counter using `u0` and `b0`. The private recorder bounds the destination
to `16 * count` bytes. In [`test-bvh-reset.cpp`](test-bvh-reset.cpp) the pinned
DLL's utility function at `+0x20760` writes a 13-DWORD recording into a private
list: a `u0` address, three `b0` writes and a `(2,1,1)` dispatch. The fixture
preselects the root and PSO, supplies private parameter tags, and resolves the
recorded address to one known buffer. It rejects mutations of the address,
tags, count and dimensions, as well as truncated and overlong recordings.
After the fence all 33 counters and both guards matched on RADV and RTX 3070.
The retail list's PSO and root binding paths are still unverified.

The tenth utility constructor call (DLL `+206f4`) selects `ResetToIndexBuffer`
at `+b4bd0` (`0x1004` bytes). It takes the same three constants as `ResetCounters`
but writes successive unsigned indices, four per active thread. The shared
recorder bounds the same `u0` footprint. The regression starts at `0xffffffc0`
to check rollover, all 132 generated indices and both guards.
