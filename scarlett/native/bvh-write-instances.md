# PT_WriteInstances

The fourth reported retail factory stop is the `0x493c`-byte shader at
XboxBvhBuilder.dll RVA `+c1920`. Static constructor call `+2053d` uses
`XDXR WriteInstances`. The adapter requires the existing descriptor/root
checks, the full shader SHA-256, and the `ScarlettBoundedInstanceReadsV1`
marker on the loaded D3D12 core before submitting the original module.
There is no fallback on an unqualified compiler or a failed host PSO.

The command stride is 96 bytes. The guest performs eight 16-byte stores
at `UAV[52] + 128 * InstanceIndex` (offsets in bytes; `UAV[52]` denotes a
DWORD load). For non-null BLAS it reads header words at +48/+60 and
optionally the two float3 bounds at +16/+28. The same original guest VA
is packed into the instance as `uint(va >> 8)`, including when the
actual buffer is mapped at an unrelated host address.

`xs_record_bvh_instances` accepts a coherent 80-byte TLAS header and an
immutable command prefix, up to 32 commands with unique indices below
the TLAS element count. Inputs must remain synchronized while snapshotted.
The recorder preflights the entire batch and resolves each non-null,
256-byte-aligned BLAS VA to 64 readable bytes of the same-device upload
mapping. Commands and resolved headers are copied to a private upload.
Each dispatch gets one command, a bounded destination UAV, and a
64-byte private source window. Keep the returned hold until retirement.
The caller owns transitions, header coherence and physical non-aliasing.

The compiler uses the bounded-range DXIL-to-SPIR-V lowering for ops
356/358; ordinary matrix arithmetic and the original orientation table
go through the existing compiler. Private root layout: UAV table u0,
root SRV t0, 16 DWORDs b0, then three DWORDs b0 and root SRV t0 in
space `0x5358`. The final SRV points only to the checked header snapshot;
the private constants describe the original guest base and byte extent.

Build `test-bvh-instances.cpp` like `test-bvh-pipeline.cpp`, using the
original matching span as `private-instances.bin`. The synthetic test
records four instances: null singular, translated/scaled with asymmetric
BLAS bounds, null identity, and orientation-table index one. It checks
all 192 destination DWORDs after the fence (both zero signs accepted only
for computed inverse floats), with the source overwritten/released before
execution. For the stock-core refusal check build with
`-DTEST_NO_INSTANCE_COMPILER`. Guest command-list binding is unqualified.
