# PT_WritePartitions

After the second factory call the builder loads another
shader from its local table (`+203fe/+20409`: RVA `+c6260`, length `0x1580`).
`+204ec` selects that span, `+204f7` selects the UTF-16 `XDXR WritePartitions`
name, `+2050b` loads the same root, and `+20512` calls the `+c470` helper
for slot `+0x1f0`. Retail reached this request and, after creation succeeded,
advanced to shader `+c1920`. No retail dispatch was observed. Selection uses the entire span's SHA-256 and the existing
same-device root and descriptor checks.

The guest reads one 32-byte command per active X lane: partition ID at 0,
instance count at 16 and the 64-bit index-list address at 24. Translation
and reserved fields do not affect this pass. Let `N`, `M` be UAV DWORDs at
byte offsets 64 and 68. `p` is the input ID, except that `0xfffffffe` maps
to `N`. The shader writes:

- count at `80 + 28*p`;
- for ordinary partitions, atomic OR (nonempty) or AND-complement (empty)
  of `1 << (p & 31)` at 76;
- `count` source DWORDs to `108 + 28*N + 4*p*M`. Empty commands do not
  read the index address; the global partition never updates the mask.

`xs_record_bvh_partitions` follows the offline recorder's trusted snapshot
contract: one coherent 80-byte header, an immutable command prefix with
its byte count, one resolved/synchronized upload mapping if indices are
needed, and a resolved TLAS slice in UAV state. The caller establishes
coherence and absence of physical aliasing. Retain the returned hold until
retirement. The whole batch is validated before recording. At most 32
commands and 32 indices per command are supported. Ordinary ID/mask
collisions, repeated global commands, overflowing or unqualified ranges
are declined. The host shader receives only TLAS-relative byte offsets,
mask, count and copied DWORDs, never a guest or host VA.

`test-bvh-partitions.cpp` links like `test-bvh-pipeline.cpp`; place the
original matching input in the fixture directory as `private-partitions.bin`.
Its fenced readback covers all 192 seeded destination DWORDs with ordinary
set/clear and global writes, an ignored trailing command, and input mutation
before submission. The factory and recorder are internal synthetic paths;
guest command-list binding is not qualified.

Regenerate the project shader header (DXC 1.9 b106a961):

```sh
dxc -T cs_6_0 -E main -Qstrip_debug -Qstrip_reflect -Fh /tmp/partitions.h -Vn bvh_partitions_dxil bvh_partitions.hlsl
sed -n '/^const unsigned char bvh_partitions_dxil/,$p' /tmp/partitions.h | sed '1s/^/static /;s/\r$//' > bvh_partitions_dxil.h
```
