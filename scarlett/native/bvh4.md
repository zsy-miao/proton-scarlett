# BVH4Builder compute PSOs

The `+1f0` device factory accepts these three original containers only for a live `ROOT_BVH4` owned by the device. The registry is unchanged: UAVs u0–u7 followed by three b0 DWORDs. All three embedded RTS0 chunks (tag + length + payload, 248 bytes) have SHA-256 `87257c9c97787a55017e86006ead4818bf0a52b14459e99efba521558108b6b7` and match the chunk in the 284-byte canonical guest root. The supported builder DLL is pinned by `tests/bvh4.sh`.

| PSO | RVA / file offset | Bytes | Container SHA-256 |
|---|---|---|---|
| PartitionConstruct | 46880 / 45c80 | 5e60 | `9a4395f151a8b795f3edc0ce3c0c6b1388057f96f71e7d9a7f43f51fcb2650ab` |
| Construct | 54bd0 / 53fd0 | 7160 | `3c126475693cf47f8389b7f1a1ae0fdd99a3e72371a4ee92385968ee399b20e3` |
| Refit | 4c6e0 / 4bae0 | 84e4 | `c328d023a7c410ee9091045ef003de8cdeed079c93693019c1db5790351832be` |

Offsets and lengths above are hex. Host slot 0 is a bounded UAV table; slot 1 is three b0 DWORDs (flags, box16 heuristic, partition ID). For the two construct passes, u0/u1/u5 are raw; u2/u3/u4/u6/u7 use strides 4/8/4/24/4. Refit needs only raw u0/u1; slots 2 and 3 are a private read window (guest base lo/hi/byte length at b0, trusted backing SRV t0, space `0x5358`). The existing checked helper returns all zeros on a miss; host addresses derive from backing plus checked delta, never from a guest VA cast. Finite min/max/median lowering is enabled by size and backend hash. Construct/Refit require core export `ScarlettBoundedBVH4V1`. PartitionConstruct uses the unmodified compiler path.

Apply dxil-spirv `0010` after `0009` and vkd3d `0016` after `0015`, rebuild the core and adapter, then run `SCARLETT_TEST_GPU=radv scarlett/tests/bvh4.sh /path/to/XboxBvhBuilder.dll /path/to/tool` (or `nvidia`). The probe checks both staged and linked factories; only the latter exposes host handles to its test translation unit. The public root without a read window must fail to compile Refit. One expected compiler diagnostic comes from that negative test.

### GPU evidence and limits

Each case is one compute workgroup. Readback waits for a fence and compares every DWORD of a 12 KiB default buffer, including all view guards and padding, against a CPU expectation. Both RADV and RTX 3070 passed:

- PartitionConstruct: two reversed indices, two instance leaves; also partition ID -2 with one instance and one masked partition child. Checks unions, four root slots (including empty sentinels), tagged links, parent table and counters.
- Construct: two reversed instance leaves, normalization at 2x, 128-byte allocation from a nonzero counter, full Box32 root and back-links.
- Refit: translated instance referencing a remote triangle (optional fourth vertex) and a remote Box32. A synthetic guest address crosses a 32-bit carry; independently observable scalar, three-DWORD and four-DWORD misses. One case propagates through a local parent, updates its child box and increments the parent counter before updating scene bounds.

PSO creation and privately bound dispatch only. No guest command-list recorder, general VA resolver or workload synchronization implemented. A recorder still has to establish range provenance, view sizes, valid topology, allocation capacity, resource lifetime and ordering. No large/multi-workgroup trees, Box16 output, deep refit, nonfinite math or full application sequence qualified.
