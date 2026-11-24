# The 64 KiB graphics control page

The RTX 3070 retail run stops at `+0x24786d1`, before these uses execute. Static call sites in build `1.0.2.1` identify three consumers of the page requested at `+0x24786b9`:

| Consumer | What the title does |
| --- | --- |
| 8-byte counter | Allocates at `+0x247931c`. The CPU stores to the cell at `+0x44b6c3`; command-list slot `+0x250` receives its address, `7`, a 64-bit counter and `1` at `+0x2484b0c`. This is consistent with a greater-or-equal wait; the flag semantics are unverified. |
| Progress markers | Allocates three banks of 18 DWORDs at `+0x5cabb50`. Slot `+0x238` receives a marker address and value at `+0x2490030`. The CPU reads the same memory at `+0x2473050` and clears it for reuse. The exact GPU pipeline stage is unknown. |
| Fence allocation | A temporary allocation callback around the shared fence constructor at `+0x1d0b2fa` can return a suballocation from this page. The internal layout of that allocation is unknown. |

Two RADV retail traces reached graphics pipeline creation (`+0x2479a8d`).
Five shared fences were created while the temporary hook was registered;
none advanced the page allocator, and no fence completion-address request
was recorded. The title then allocated an 8-byte counter at offset 0, set it
to 1, and allocated 54 marker DWORDs at offset 8. All markers were zero at
the stop. Watchpoints on the graphics initialization thread recorded no
subsequent counter access or known GPU wait/marker call before that point.
This bounds a CPU-only startup experiment on RTX; later GPU use remains untested.

The [marker test](../../tests/control-markers.cpp) exercises one possible host operation: D3D12 `WriteBufferImmediate` into mapped readback memory. A fence gate holds back a second write. While it is blocked, the CPU must observe the first marker and be able to clear it; after retirement only the second marker may be set. This passed on the RTX 3070. It tests CPU-visible ordering across two command lists, not the stage of the retail operation or the cache properties of the requested page.

```sh
work=$(mktemp -d)
x86_64-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -Werror -static \
  scarlett/tests/control-markers.cpp -ld3d12 -o "$work/game_scarlett_master.exe"
./scarlett/test-launch-nixos.sh "$work"
```

The [counter wait test](../../tests/control-wait.cpp) submits a GPU copy, a
fence wait, and a checkpoint to a private copy queue. A CPU observer polls
an aligned 64-bit counter. The test verifies that the first copy is visible
while the checkpoint is blocked, and that a later CPU store releases the
checkpoint and lets the second copy run. Timeout and destruction skip the
second copy, drain the queue, and release the counter. Build as above with
`control-wait.cpp` in place of `control-markers.cpp`. On the NixOS test host,
select one directory containing `mcfgthread/gthr.h` under
`/nix/store/*-mcfgthread-x86_64-w64-mingw32-2.5.1-dev/include` and pass
that directory with `-isystem`. This experiment covers
one monotonic counter on one queue; the retail command list has not used it.

A native CUDA probe on the RTX 3070 (595.112.07) also held a 64-bit stream
wait on a 64 KiB write-combined host allocation. Raising the CPU counter above
`0x100000003` released the wait; the CPU saw the subsequent GPU marker
`0x53ca1234`. Exporting that allocation with `cuMemGetHandleForAddressRange`
and `CU_MEM_RANGE_HANDLE_TYPE_DMA_BUF_FD` returned `CUDA_ERROR_NOT_SUPPORTED`.
The reverse direction worked in a Linux hardware test: Vulkan allocated a
64 KiB type-3 buffer, exported a DMA-BUF for the CPU mapping and an opaque
FD for CUDA. The wait stayed pending with the counter at zero; after a CPU
store through the DMA-BUF mapping, both CPU mappings saw the GPU marker.
This gives the wait and marker tests a shared backing. It does not establish
uncached access for arbitrary title commands or exact-size Wine release.

On driver 595.112.07, NVIDIA's resource manager reported GPU caching enabled
for 64 KiB Vulkan buffers allocated with host-visible types 3, 4 and 5,
including the BAR mapping. CUDA pinned write-combined host pages had the
requested CPU and GPU cache policies. NVIDIA DRM exported them as a DMA-BUF,
but `vkGetMemoryFdPropertiesKHR` returned a memory-type mask of zero. On the
same device a Vulkan-created DMA-BUF returned `0x11` for a buffer allowing
`0x1b`. Export alone therefore does not make the CUDA pages usable by this
Vulkan buffer. CUDA host-NUMA memory was GPU-uncached but CPU-cached.

A [combined wait and marker test](https://github.com/rcx2135/proton-scarlett/tree/3991f08a/scarlett/tests/control-translation)
also exercises exact-size release followed by immediate address reuse. A CPU
store to the replacement page did not satisfy the original wait. A store to
the retained original backing did, and the subsequent GPU marker left the
replacement untouched. This uses a synthetic allocation and known offsets;
the retail method's scope and pipeline stage have not been verified.
