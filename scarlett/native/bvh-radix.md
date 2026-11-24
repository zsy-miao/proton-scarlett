# Radix constructor (`+1da20`)

The builder creates CountAndAlloc (236 bytes: u0..u5, b0/3), then PrefixSum (140 bytes: u0, u1, b0/1). Both are root descriptors/constants in space zero, v1.1, visibility ALL, no descriptor flags or samplers, console signature flags `0x03000000`. The helper passes CountAndAlloc for Reset, Count and Alloc; only the seven prefix PSOs take PrefixSum. Compare all serialized bytes: Utility is also 140 bytes. Match the device's live wrapper and exact shader digest before choosing a desktop layout.

| Stage | Source RVA / length | Host shader and contract |
|---|---|---|
| Reset | `92e30/fec` | Original. 32 threads/group, zero 16 bytes at `u0 + 16*x` for `x < b0.x`. |
| Count | `93e20/1ffc` | Original. 512 threads/group, four DWORD keys/thread. Reads 2048 keys/group from u0, writes the 256 digit frequencies to `u1[digit*b0.x + group]`, with digit `(key >> b0.y) & 255`. No input tail predicate. |
| Prefix | [seven variants](bvh-prefix.md) | Size and full digest select each project-owned host kernel. |
| Alloc | `95e20/3f38` | Project-owned stable scatter, 2048 key/payload pairs per group. For `cell=digit*b0.x+group`, writes pair at `u2[cell]+u3[cell>>(b0.z&31)]+rank`, with `rank` the number of preceding equal-digit keys in the group. |

Reset and Count use the validated translated root itself. Count uses a size/hash-selected compiler quirk and requires `ScarlettRadixCountV1`. All ballots in this exact shader are workgroup-uniform; 16 scratch words per static ballot and workgroup barriers produce the two DWORDs for each logical wave of 64 local IDs, independent of native subgroup size. Prefix and scatter do not use this quirk. Scatter uses a private six-descriptor raw UAV table to obtain real view extents. Its groupshared histogram computes stable rank, validates both prefix levels and bounds both output writes. A future guest recorder must still validate the six resources/owners, nonaliasing, prefix allocation consistency, group shape, arithmetic, barriers and fence lifetimes.

`tests/builder-pipelines.sh` runs the guarded reset/count, prefix and scatter fixtures. For scatter, regenerate the project-owned bytecode with DXC `-WX -T cs_6_0 -E main -O3 -Qstrip_debug -Qstrip_reflect -Vn bvh_radix_alloc_dxil -Fh port.h bvh_radix_alloc.hlsl`; keep the `const unsigned char` array from `port.h` and mark it `static`. The DXIL SHA-256 is `c7cbfcfc107eff76654c174b78e489f452539b2c2dc23618fb9660434811e05a`.
