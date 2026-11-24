# Small R8 allocation and the next retail call

After the post-clear sampler, `34413f` calls device `+c8` with the 16-byte
output first, then This, node mask 1, count 1 and the resource description.
The 52-byte caller-written prefix is `3 0 1000 0 100 0 80 10001 3d 1 0 0
300000` (hex DWORDs; DWORD 1 is padding). It describes a single-sampled,
single-mip/layer 256×128 R8_UNORM texture. The translated host description
retains requested alignment and UNKNOWN layout, with resource flags NONE.
`34414d` checks alignment: if it isn't 4096, `344157` clears the request and
`344182` queries again. The final size must fit a DWORD (`344190`).

On RADV 26.3.0 the native small query returns `{32768,4096}` and default
returns `{65536,65536}`. Only these shapes/results are qualified. For the
recognized query, an invalid or unqualified host aggregate returns the
non-MSAA D3D12 error sentinel `{UINT64_MAX,65536}`. In particular an
all-ones size with alignment 4096 cannot pass the title's small probe.
The internal helper distinguishes this handled failure (`S_FALSE`) from
unrecognized inputs (`FAILED`, fail closed at the public slot).

`test-r8-allocation.cpp` exercises the public sret ABI against the native
query, with an input ending at a no-access page and canaries around the
output. The fixture also injects failed/malformed host aggregates. A single
128 KiB default heap holds three R8 images placed at 4, 36 and 68 KiB.
The two outside images are filled first, then the middle; fenced readback
checks all 98,304 texels against three distinct patterns.

```sh
n=/path/to/scarlett/native
mkdir -p game
for f in "$n/test-r8-device.c" "$n/queue/queue.c" "$n/d3d12_vm.c"; do
  x86_64-w64-mingw32-gcc -DSCARLETT_DEVICE_BOOTSTRAP -DSCARLETT_STAGED_PLACED \
    -Os -Wall -Wextra -Werror -c "$f" -o "$(basename "$f" .c).o"
done
x86_64-w64-mingw32-g++ -std=c++17 -Os -Wall -Wextra -Werror -static \
  "$n/test-r8-allocation.cpp" test-r8-device.o queue.o d3d12_vm.o -ld3d12 \
  -o game/game_scarlett_master.exe
SCARLETT_TEST_GPU=radv SCARLETT_TEST_DISPLAY=sway \
  "$n/../test-launch-nixos.sh" game /path/to/scratch-tool
```

For explicit `0x105` with the same descriptor and alignment 4096, `+c8`
returns `{32768,4096}` directly: the converter addresses eight 64×64
one-byte tiles. It does not query a host image or round up its size. The
explicit mode with default alignment, or on the primary descriptor, is
unsupported. The original UNKNOWN-layout host probe remains unchanged.

**Before the R8 recommendation.** The two observed queries both receive small alignment
and succeed. Between them `4eccca2` calls XGSuggestSwizzleMode with default
alignment and layout 0. It does not select a verified swizzle. `16f7141`
then calls XGCreateTextureComputer with layout still 0 and alignment 4096.
Its `E_NOTIMPL`/null result is ignored; the uninstrumented title faults
loading the vtable at `16f71ea` before method `+40` runs. The would-be
arguments comprise a title destination, two zeros, the source, row pitch
256 and slice pitch 32768. `193df37` obtained the destination from the
title allocator. In a separate metadata-only probe it was offset `0x2000`
into a MEM_MAPPED / PAGE_READWRITE|PAGE_WRITECOMBINE allocation. Source
was a distinct MEM_MAPPED / PAGE_READWRITE allocation. The generic section
query rejected both ranges (`0xd0000019`). A follow-up retail trace acquired
the full 32,768-byte destination with the WC query (`0x53430005`), at offset
`0x2000` in the `0x89700000`-byte pool. The WB query (`0x5343000c`) acquired
the source in the `0x49400000`-byte pool. Swapping the queries failed for both.

The section handles identify the original allocations. See the
[XG copy and original-backing tests](xg/README.md) for the byte order
and current retail stop.
