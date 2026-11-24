# xmem.dll — Windows x64 subset for Proton

This original DLL has nine named exports. It needs only Win32; there is no
console SDK, external compatibility runtime, or ordinal compatibility contract.

| Export | Supported behavior |
| --- | --- |
| [`XMemAlloc` and `XMemFree`](https://docs.xbox.com/reference/system/xmem/macros/xmem_set_allocation_hooks) | Dispatch to the main EXE's `__XMemAllocationHooks__` data export, or to the defaults when absent. Both callbacks are copied at DLL process attach; neither is called during binding. A present but incomplete pair fails dispatch with `ERROR_INVALID_DATA`. |
| [`XMemAllocDefault`](https://docs.xbox.com/reference/system/xmem/functions/xmemalloc_routine) and [`XMemFreeDefault`](https://docs.xbox.com/reference/system/xmem/functions/xmemfree_routine) | Cached CPU heap, ordinary pages, minimum 16-byte alignment. A tracking callback can call these directly without re-entering itself. |
| [`XMemIsToolsMemAvailable`](https://docs.xbox.com/reference/system/xmem/functions/xmemistoolsmemavailable) | Returns `FALSE`: there is no profiling-mode tools partition. |
| [`XMemVirtualAlloc`](https://docs.xbox.com/reference/system/xmem/functions/xmemvirtualalloc) | Untagged `XMEM_CPU` (`0x00000001`), ordinary 4 KiB pages, `MEM_RESERVE`, `MEM_COMMIT`, or both, with `PAGE_READWRITE` or `PAGE_READONLY`. Uses Win32 `VirtualAlloc` with the caller's base, size and protection; release with `VirtualFree`. |
| `XMemVirtualAlloc`, 64 KiB reservations | Two supported `0x20402000` forms: CPU mappable (`0x21`, `PAGE_READWRITE`) and graphics VA only (`3`, `0x801`). Size and any specified base must be multiples of 64 KiB. |
| [`XMemAllocatePhysicalPages`](https://docs.xbox.com/reference/system/xmem/functions/xmemallocatephysicalpages), [`XMemMapPhysicalPages`](https://docs.xbox.com/reference/system/xmem/functions/xmemmapphysicalpages), [`XMemFreePhysicalPages`](https://docs.xbox.com/reference/system/xmem/functions/xmemfreephysicalpages) | CPU mappings of 64 KiB page runs, with shared backing. See limits below. |

Unsupported virtual allocation flags, types, or protections return `NULL` and set
`ERROR_NOT_SUPPORTED` (this DLL's diagnostic). This includes graphics commits,
tools memory, tags, and other special page sizes or protection modifiers.
Errors from supported calls pass through from Win32. There is no console
budget accounting.

### Reservation and mapping milestone

The two startup reservations consume process **address space only**. The CPU
arena uses a Win32 placeholder. The graphics form reserves inaccessible VA:
the returned pointer is **not a GPU address** and there is no GPU mapping or
memory allocation. The DLL rejects mapping CPU sections into the graphics
reservation. GPU commit, bandwidth guarantees, and D3D resource sharing need
a graphics backend; treating these as anonymous CPU RAM would be incorrect.

With allocation flags zero, each page run owns a pagefile-backed Win32 section.
Returned numbers are process-local page identifiers, not OS PFNs, DMA
addresses, or pinned physical memory. Consecutive identifiers from one run
can be mapped into a vacant, aligned subrange of a CPU arena; overlapping
views of the same backing observe each other's writes. This requires Win32
`VirtualAlloc2`, `MapViewOfFile3`, and `UnmapViewOfFile2` placeholder support.
Failures do not fall back to copying or private committed RAM.

Pass `NULL` for the page array and the exact view address/count to unmap.
Unmapping all views restores the entire original reservation for
`VirtualFree(base, 0, MEM_RELEASE)`. Free each run as a whole after unmapping
all aliases (`ERROR_BUSY` otherwise). Use the XMem API, not `UnmapViewOfFile`
or raw placeholder edits, for active views. The shim reclaims released arena
bookkeeping on subsequent CPU reservations. Replacing live mappings, partial
unmaps/frees, arbitrary page lists, 2 MiB clusters and scatter are unsupported.

### Graphics commit: measured blocker

In `scarlett-run.x6UanyCT/logs/steam-999001.log:239–246`, the trap is at
RVA `0x1e1d3a7`, with `RAX=0`, `R15=0x49400000` and pool index `R13=0`.
At `0x1e1d181` the call through thunk `0x5f3fcc0` (IAT `0x6793a58`,
`XMemVirtualAlloc`) passes `(NULL, 0x49400000, 0x20003000, 0x183, 0x22004)`.
At `0x1e1d189` a null result branches directly to this trap. That is a
1,172 MiB graphics reserve+commit with requested 2 MiB pages; the two
preceding large allocations were VA-only. `R9=0x182` in the exception log
is **after** the call; the call site loads `0x183`. No physical-page API
failed on this branch. Other pool cases at `0x1e1d288` and `0x1e1d369` use
the same flags with per-pool sizes; `0x1e1d4d6` uses `0x283, 0x2404`.
Those are static call sites, not observed failures.

`gpu_buffer.hpp` is a private graphics-backend prototype. It imports a
2 MiB-aligned, CPU-writable allocation via desktop
[`OpenExistingHeapFromAddress`](https://learn.microsoft.com/windows/win32/api/d3d12/nf-d3d12-id3d12device3-openexistingheapfromaddress),
creates a placed cross-adapter buffer, and resolves interior CPU pointers to
GPU buffer addresses with a bounds check. Alignment is verified; the backing
is system memory, with no guarantee of contiguous 2 MiB physical pages,
console cache attributes or GPU-local bandwidth.

On an NVIDIA RTX 3070 (driver 595.112.7), vkd3d-proton 3.0.0 build
`212991fc2c266bc`, `test_gpu.cpp` passed at the exact `0x49400000` size.
The CPU wrote a marker at offset zero. A fenced GPU copy moved it into a
separate default-heap resource; after the CPU erased the source, a second
fenced GPU copy put it at the last word of the original CPU allocation.
CPU base was `0x03a00000`, GPU base `0x0c600000`. This proves shared bytes
for a buffer, not identical address spaces or Xbox graphics protection.

The public DLL still rejects this request. Enabling it needs the graphics
consumer to resolve each title address against the owning device's live
resource. The title imports ordinary `VirtualFree` (IAT `0x6793a70`):
release must retire GPU work and release all views **before** the underlying
CPU allocation is freed or reused. Keeping only a map of raw pointers in
`xmem.dll` would not observe that free. No console ABI is introduced by the
prototype and the public DLL gains no graphics dependency.

To reproduce on the NixOS GPU test host, from this directory:

```sh
mkdir -p build/gpu-game build/gpu-run
x86_64-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -Werror -static \
  test_gpu.cpp -ld3d12 -o build/gpu-game/game_scarlett_master.exe
TMPDIR="$PWD/build/gpu-run" ../../test-launch-nixos.sh "$PWD/build/gpu-game"
grep -h 'GPU copied CPU sentinel' build/gpu-run/scarlett-run.*/logs/steam-999001.log
```

The harness requires this executable name; the directory above contains only
the synthetic test.

### Heap allocation attributes and ownership

For the defaults, [`XALLOC_ATTRIBUTES`](https://docs.xbox.com/reference/system/xmem/structs/xalloc_attributes)
supports `ObjectType` (bits 0–13), `AllocatorId` (16–23), and `Alignment`
(24–28). Alignment encodings supported: 0 (any), 2 (4 bytes), 4–17 (16 bytes
through 128 KiB). The allocator rounds up to at least 16 bytes. The page size
and memory type must both be zero (4 KiB and `XALLOC_MEMTYPE_HEAP_CACHEABLE`);
32-bit mapping and reserved bits must be clear. Unsupported attributes fail
with `ERROR_NOT_SUPPORTED`; unrepresentable or unavailable sizes fail with
`ERROR_NOT_ENOUGH_MEMORY`.

The shim uses `HeapAlloc` with private alignment and attribute bookkeeping.
Free a live default block with its original attributes. If the attributes
differ, the shim sets `ERROR_INVALID_PARAMETER` and leaves the block live.
Foreign pointers and double frees are outside the API contract.
There is no XMem heap statistics, tag-query, or console heap placement support.

Hook dispatch forwards both arguments verbatim, including attributes the
defaults do not support. A custom allocation must return to its matching
free hook; callbacks choose whether to delegate to the defaults. Nested public
calls still dispatch to the hooks. Runtime replacement of the pair through
the deprecated `XMemSetAllocationHooks` API is unsupported.

Other imports are absent, including `XMemMapPhysicalPagesScatter`,
`XMemGetWorkingSetStatistics`, and `XMemTransferMemory`. A caller requiring
those exports cannot use this DLL as a full replacement.

## Build and exercise the DLL

From this directory on Linux with x86-64 MinGW-w64 and Wine:

```sh
mkdir -p build
x86_64-w64-mingw32-gcc -O2 -Wall -Wextra -Werror -shared xmem.c \
  -Wl,--out-implib,build/libxmem.a -o build/xmem.dll
x86_64-w64-mingw32-gcc -O2 -Wall -Wextra -Werror test.c \
  -Lbuild -lxmem -o build/test.exe
x86_64-w64-mingw32-gcc -O2 -Wall -Wextra -Werror test_pages.c \
  -Lbuild -lxmem -o build/test_pages.exe
for mode in 0 1 2; do
  x86_64-w64-mingw32-gcc -O2 -Wall -Wextra -Werror -DHOOK_MODE=$mode test_alloc.c \
    -Lbuild -lxmem -o "build/test-alloc-$mode.exe" || exit 1
done
export WINEPREFIX="$PWD/build/prefix" WINEDLLOVERRIDES=xmem=n
for exe in build/test.exe build/test_pages.exe build/test-alloc-{0,1,2}.exe; do wine "$exe" || exit 1; done # bash
```

The synthetic executable imports the adjacent DLL by name, checks page state
and protection through `VirtualQuery`, writes and releases memory, and tests
refusal of unsupported requests. `test_pages.c` checks both 16 and 24 GiB
startup reservation pairs, subrange aliases above 4 GiB, GPU map refusal,
page persistence, busy/stale page IDs and release after coalescing.
`test_alloc.c` builds three EXEs: no hook
export, a complete pair, and a partial pair. They check alignment, original
attribute matching, full-width sizes/attributes through a callback, nested
calls, direct defaults, and binding before `main`. For a Proton test use the same adjacent
DLL and executable with a disposable `STEAM_COMPAT_DATA_PATH` and
`WINEDLLOVERRIDES=xmem=n`.
