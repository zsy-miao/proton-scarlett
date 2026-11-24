# XDXR RadixSortPrefixSum

The seven stages use the same 140-byte root wire: u0, u1, then one DWORD at
b0 (space 0, v1.1, console flags 0x03000000). The utility wire also has 140
bytes; compare all bytes and check the root on the creating device. Select
the stage by full original SHA-256 after the size filter. PrefixSum1024 and
PrefixSum2048 are both 0x1644 bytes.

For N = 64, 128, 256, 512, 1024, 2048, 4096, the respective (threads, words
per thread) pairs are (32,2), (32,4), (64,4), (64,8), (256,4), (512,4),
(512,8). Each group performs an in-place exclusive scan on N consecutive u0
DWORDs, modulo 2^32. The group total is stored in u1[group.x] if b0.x bit
zero is set. Logical lanes use the last preceding block lane at steps
1/2/4/8/16, read lane 31 for the upper half, and add preceding wave64
totals in multi-wave groups. Group-shared indexing makes these semantics
independent of the host wave size.

`bvh_prefix_dxil.h` is generated solely from the project shader. Rebuild in
`scarlett/native` with the Linux DXC 1.9 build `b106a961`:

```sh
tmp=$(mktemp -d)
for n in 64 128 256 512 1024 2048 4096; do
    dxc -WX -T cs_6_0 -E main -D N=$n -Qstrip_debug -Qstrip_reflect \
        -Vn bvh_prefix${n}_dxil -Fh "$tmp/$n.h" bvh_prefix_sum.hlsl || exit 1
    sed -n '/^const unsigned char bvh_prefix/,$p' "$tmp/$n.h" |
        sed '1s/^/static /;s/\r$//'
done > "$tmp/all.h"
cmp "$tmp/all.h" bvh_prefix_dxil.h
rm -r "$tmp"
```

`scarlett/tests/builder-pipelines.sh` stages the seven local inputs. The
prefix fixture uses +0x80 and +0x1f0, submits the resulting PSOs and compares
every word, including the unused tail and guards, to a serial CPU scan. Each
dispatch has three full groups, one dimensional, with distinct nonoverlapping
UAV ranges. No general Xbox dispatch/VA recording path is implied.
