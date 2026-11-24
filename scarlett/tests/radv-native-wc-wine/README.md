# Wine-hosted native WC pool feasibility (scratch; not an installer patch)

On `1002:164e` RADV 26.3.0, this PE test uses the *actual* VkDevice and
queue returned by vkd3d's `ID3D12DXVKInteropDevice`. The queue is locked for
submission. The original device reference is released; the queried COM
interface and queue keep it alive until after the fence and resource teardown.
The `0x89700000`-byte type-2 allocation has no export or import chain. A whole-pool
buffer and a 1×1 4× RGBA8 sampled attachment bind that same allocation at
0 and `0xe800000`. No process or driver setting is overridden.

Apply the included Wine patch in an isolated source tree after the project's
Wine 0012, and build **both** x64 unix ntdll.so and win32u.so. Keep the
matched vkd3d-proton PE binaries. The fixture opts in only the calling
thread's next device creation and pool publication; unmodified retail does
not issue those test commands. The map/unmap function pointers are supplied
only by Wine unix modules. No PE-provided target address replaces a host map:
ntdll chooses and reserves a Wine view under the VM mutex before calling the
ICD's placed map. The same lock covers exact unmap and retirement of the VA.
No fence wait is made under the VM mutex.

```
for stage in vs ps cs; do
    dxc -spirv -fspv-target-env=vulkan1.1 -WX -T ${stage}_6_0 -E $stage -Fo $stage.spv ../radv-native-msaa/draw.hlsl
done
x86_64-w64-mingw32-gcc -std=c11 -Os -Wall -Wextra -Werror -Wno-cast-function-type \
    -I/path/to/vulkan-headers/include pe.c -static -ld3d12 -o game_scarlett_master.exe
```

Place the three shaders beside the exe and run `test-launch-nixos.sh` on
RADV/headless sway with the scratch tool. `VirtualQuery` reported one
committed mapped view, size `0x89700000`, protection `0x404` (RW/WC).
Interior release, partial decommit and protection changes failed. The test
submitted gated GPU work, observed `VK_NOT_READY`, called public
`VirtualFree(base,0,MEM_RELEASE)`, observed `MEM_FREE`, reused the exact VA,
and verified the fence was **still** unsignaled. A staged write through the
old identity failed. Only then did it open the gate. All four samples read
through the MSAA SRV matched (`f0800c04 e08c1408 d0981c0c c0a42410`); both
64 KiB guards and the new VA were intact and a buffer-side marker survived.
The allocation was destroyed after the fence. No new cross-process or
general simultaneous-alias ABI is implied.

## Which CPU views matter in the read-only retail run?

* XMem's `validate_import` temporarily held three CPU VAs at once: the
  original buffer Map, the rebound buffer Map, and the published title VA.
  The first two are allocator validation, not VAs returned to the title.
* XG opened 14 independent WC views in this trace: the 32 KiB tiled R8 write
  at `+0x2000`, then one DWORD for each of 13 4 KiB tile slots at
  `+0xe5c7000..+0xe5d3000`. Each transient alias is released by XG before
  returning. It protects against target VA reuse; no second WC address is
  returned to the title here. The PE fixture instead exercises a bounded
  generation-checked staged write (32 KiB or four bytes), serialized with
  public release. It is a candidate replacement for these writes, not a
  replacement for all section aliases. The R8 source remains independently WB-backed.
* The descriptor provider acquired *GPU buffer* ranges at offsets 0,
  `0x1800`, `0x1b80`, `0x2000` and the tile slots. Its implementation currently
  validates retained section handles and reimports the full buffer. It needs
  a retained same-device buffer/offset contract, not another CPU pointer.
* Retail stopped at `+cc2936` without a pool-release or GPU-retirement
  observation at that boundary. Those are real fixture tests, not title events.

## Remaining integration work

XMem, XG, descriptor resolution and placed image creation still use the old
fd-backed registry in retail. A production version needs an exact pool owner
that survives the initial XMem device release, independently pins the buffer,
image and descriptor GPU lifetimes, and forbids stale acquisitions by
incarnation. It must preserve fd-backed paths for other allocations and
adapters. This experiment does not establish concurrent multiple-owner teardown,
process-detach/device-loss behavior, or all future title view requests.
