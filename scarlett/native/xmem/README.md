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
