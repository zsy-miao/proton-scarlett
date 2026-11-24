# xmem.dll — Windows x64 subset for Proton

This original DLL has nine named XMem exports and a private host copy-source export. There is no
console SDK, external compatibility runtime, or ordinal compatibility contract.

| Export | Supported behavior |
| --- | --- |
| [`XMemAlloc` and `XMemFree`](https://docs.xbox.com/reference/system/xmem/macros/xmem_set_allocation_hooks) | Dispatch to the main EXE's `__XMemAllocationHooks__` data export, or to the defaults when absent. Both callbacks are copied at DLL process attach; neither is called during binding. A present but incomplete pair fails dispatch with `ERROR_INVALID_DATA`. |
| [`XMemAllocDefault`](https://docs.xbox.com/reference/system/xmem/functions/xmemalloc_routine) and [`XMemFreeDefault`](https://docs.xbox.com/reference/system/xmem/functions/xmemfree_routine) | Cached CPU heap, ordinary pages, minimum 16-byte alignment. A tracking callback can call these directly without re-entering itself. |
| [`XMemIsToolsMemAvailable`](https://docs.xbox.com/reference/system/xmem/functions/xmemistoolsmemavailable) | Returns `FALSE`: there is no profiling-mode tools partition. |
| [`XMemVirtualAlloc`](https://docs.xbox.com/reference/system/xmem/functions/xmemvirtualalloc) | Untagged `XMEM_CPU` (`0x00000001`), ordinary 4 KiB pages, `MEM_RESERVE`, `MEM_COMMIT`, or both, with `PAGE_READWRITE` or `PAGE_READONLY`. Uses Win32 `VirtualAlloc` with the caller's base, size and protection; release with `VirtualFree`. |
| `XMemVirtualAlloc`, 64 KiB reservations | Type `0x20402000`: CPU mappable (`0x21`, `PAGE_READWRITE`), WC CPU/GPU mappable (`0x23`, `0x2404`), or graphics VA only (`3`, `0x801`). Size and any specified base must be multiples of 64 KiB. |
| `XMemVirtualAlloc`, 64 KiB CPU reserve+commit | Type `0x20403000`, flags exactly `0x181`, protection `PAGE_READWRITE` (`4`). Returns zero-initialized private committed CPU memory. Size must be a positive multiple of 64 KiB; an explicit base must be free and 64 KiB-aligned. Release with ordinary `VirtualFree(base, 0, MEM_RELEASE)`. |
| `XMemVirtualAlloc`, graphics reserve+commit | Type `0x20003000`, flags `0x183`, protection `0x22004`: a live GPU-importable shared section on patched Wine. Size must be a positive multiple of 2 MiB; a requested base must also be aligned to 2 MiB and free. Release with `VirtualFree(base, 0, MEM_RELEASE)`. |
| [`XMemAllocatePhysicalPages`](https://docs.xbox.com/reference/system/xmem/functions/xmemallocatephysicalpages), [`XMemMapPhysicalPages`](https://docs.xbox.com/reference/system/xmem/functions/xmemmapphysicalpages), [`XMemFreePhysicalPages`](https://docs.xbox.com/reference/system/xmem/functions/xmemfreephysicalpages) | 64 KiB page runs with CPU section backing (`0`), graphics backing (`0x280`), or the whole write-back pool (`0x380`). See limits below. |

The graphics default allocator also accepts exactly `(64 MiB, 0x30808000)`.
The full word is a compatibility candidate informed by xcl `f4ce27ceb6`;
the title's interceptor establishes only `(attrs & 0x00ffc000)==0x00808000`.
This request creates one 4 MiB **VA**-aligned, cached read/write allocation
using ordinary committed memory. The title's own allocation hook rounds and
returns slices; its free hook absorbs interior frees and forwards the base.
No physical page size is promised.

For a host image import, `ScarlettRetainDisplayBackingV1(slice,bytes,&info)`
checks that the range is inside one live default allocation, returns its
original base, size and offset, and takes one reference. This is an arena
bounds check, not proof the title issued a particular suballocation. Check
image bounds/alignment and GPU memory compatibility independently. Keep
XMem loaded and pair each success with `ScarlettReleaseDisplayBackingV1(info.base)`
after destroying the import on GPU retirement. Owner free immediately denies
new retains; existing imports keep the CPU bytes until their last release.
`test-display-alloc.c` exercises those rules with a synthetic retail-style
hook pair and two requests of `0x1fa4000` bytes.

Unsupported virtual allocation flags, types, or protections return `NULL` and set
`ERROR_NOT_SUPPORTED` (this DLL's diagnostic). This includes other graphics commits,
tools memory, tags, and other special page sizes or protection modifiers.
Errors from supported calls pass through from Win32. There is no console
budget accounting.

### Retail write-back page pool

Build `1.0.2.1` calls `XMemAllocatePhysicalPages(0x380)` at `+0x1e6c22d`
for 99,776 pages of 64 KiB. XMem allocates five 1,024 MiB write-back heaps
and one 1,116 MiB heap. Page IDs advance by 16 so they stay distinct after
the title drops the low four bits. The matching
`XMemVirtualAlloc(NULL, 0x185c00000, 0x20402000, 0x23, 0x2004)` reserves a
contiguous range. `XMemMapPhysicalPages` at `+0x1e6c295` maps those same six
sections into it. Only whole-run mapping is supported. At `+0x1e6c2bb`,
retail registers that range through device `+0x250`. XMem verifies all six
section identities under the page lock; the device imports those sections
and returns an opaque registration handle. The [registration test](../test-page-pool.c)
checked 12 fenced reads from the original heaps after release and address reuse.
The guest unregister slot and queue-side remapping remain unverified.

[`test-tile-pages.cpp`](test-tile-pages.cpp) maps the run twice, writes and
reads both ends of each heap through opposite mappings, and checks fenced
GPU copies in both directions across the first 1 GiB boundary. It also
checks busy frees, unmap/remap, stale addresses, and retained imports after
the title frees the run. Build it like `test_wc_pages.cpp` and run on RADV.

### Reservation and mapping milestone

The descriptor arena (`0x1e8649a`) requests `XMemVirtualAlloc(NULL, total,
0x42000, 7, PAGE_NOACCESS)`. For a positive 64 KiB multiple the shim returns a
native `VirtualAlloc2` placeholder. Four ordinary `VirtualFree` calls with
`MEM_RELEASE | MEM_PRESERVE_PLACEHOLDER` split five extents. The fixed-base
requests at `0x1e8659e`–`0x1e8666c` (`0x20406000`, flags `0x27`, protection
`0x1004/0x1404/0x8404/0x2404/0x2004`) replace exact vacant placeholders with
inaccessible reservations. Wine checks base and size atomically; there is no
free-and-reallocate window. A replacement can be released directly or restored
as a placeholder and coalesced. Registered page arenas are excluded.

`XMemAllocatePhysicalPages(0x280)` at `1e866e4` publishes one run of stable
IDs. For the observed 2,960-page layout, when all five contiguous reservations
are present it creates five dedicated heaps before returning any ID. Each
ID names the same page for the lifetime of the run. Maps may not cross a heap
boundary or change its cache policy. This special case does not change the
710 MiB worker run. Unmap all active views with XMem before freeing the run or
editing the underlying virtual reservations.

| Map RVA | Relative IDs | CPU | Guest GPU use provided |
|---|---|---|---|
| `1e86738` (`2404`) | `[0,752)` | WC/RW | buffer read/write |
| `1e86760` (`2004`) | `[752,2352)` | WB/RW | buffer read/write |
| `1e86784` (`1004`) | `[2352,2512)` | WB/RW | read source |
| `1e867ac` (`1404`) | `[2512,2528)` | WC/RW | read source |
| `1e867d1` (`8404`) | `[2528,2960)` | WC/RW | translated compute shader store and bounded GPU copy source; no buffer handle or raw GPU VA |

`test-descriptor-pages.cpp` builds like `test_wc_pages.cpp`. On RADV it tests
the actual layout, all five maps, last-word GPU reads and writes permitted by
the translated operations, rejected writable imports of source-only VAs,
conflicting aliases, lifetime and teardown. The two smaller dedicated WB heaps
use `ScarlettWbPagesHeapV1` independently of the existing WB pool size contract.
Wine's `0x5343000d` maps a WB slice; `0x5343000e/0f` map WB/WC source-only
views. The ordinary acquire operations refuse the latter. `ImportedReadSpan`
uses `0x53430010/11` privately and only records bounded source copies. This is
a restriction of the translation path, not a GPU page-table permission or a
sandbox against arbitrary native code. CPU access is never made read-only.

At `4f45cf`–`4f45fd` the upload path also uses the `8404` extent as a
buffer copy source. For this extent `ImportedReadSpan::acquire` calls the
loaded XMem provider's private `ScarlettAcquireCodeReadSourceV1`. XMem
checks the requested bytes against a live page view under its page lock,
pins that run's backing and imports it on the supplied host device. The
returned object can only record a bounded `CopyBufferRegion` and release;
it doesn't grant writable access, even after title VA reuse. The trusted
recorder supplies a same-device list and destination buffer in appropriate
states and retains the span through its retirement fence. Follow the XMem
map/unmap protocol for active page views. This is a buffer-copy operation,
not an implementation of pitched copies or full-width SRV/UAV bindings.

The last extent is a code store, not an executable CPU mapping or a host GPU
instruction buffer. `0x53430012` maps the exact 432-page WC backing; the
buffer-acquire operations refuse it. `0x53430013` copies at most 64 KiB from
one live code view while holding Wine's VM lock; it returns no backing handle.
In `code_store.hpp`, `xs_code_pipeline` compares the private snapshot with
known-good host DXIL before creating a real desktop compute PSO from the
snapshot. The approved bytes must come from trusted host code. Title writes
after capture cannot change the PSO. The synthetic consumer supplies two
DXC-compiled fixtures, checks rejection after changing one byte, and dispatches
both PSOs after unmapping and reusing the title VA. Build the fixtures next to
the synthetic executable, then launch it from that directory:

```sh
for entry in first second; do
    dxc -T cs_6_0 -E "$entry" -Qstrip_debug -Qstrip_reflect \
        -Fo "$game/$entry.dxil" "$src/test-code-store.hlsl"
done
```

This tests the translated store and host shader consumer, not an equivalence
between retail console shader ISA and DXIL. No retail consumer or trusted
translation for an unknown title shader is supplied.

After mapping, retail requests a [174 MiB placed buffer](../placed-arena.md) spanning the code store and the two read/write regions. The code store also serves as a source for ordinary GPU data copies.

The separate 20 MiB CPU allocation at RVA `+0x5ba6c30` saves its result at
`+0x78cb800` and branches to `+0x5ba6c65` on null. On success, `+0x28b7b09`
passes the pointer and `0x1400000` size to heap initialization; `+0x1f67809`
writes block metadata into that memory. Teardown at `+0x28b7ace` releases the
saved base with `VirtualFree`. For this flags tuple, Win32 `VirtualAlloc`
provides 64 KiB **VA alignment** and ordinary host pages. No physical page size,
pool placement, or GPU visibility is guaranteed.

The two startup reservations consume process **address space only**. The CPU
arena uses a Win32 placeholder. The graphics form reserves inaccessible VA:
the returned pointer is **not a GPU address** and there is no GPU mapping or
memory allocation. The DLL rejects mapping CPU sections into the graphics
reservation. A separate committed graphics allocation is described below.

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

### Write-combined physical pages

At RVA `0x5eb27a7` the title allocates `0x2c60` pages (710 MiB) with flags
`0x280`. Its consumer reserves an arena with flags `0x23`, protection `0x2404`
at `0x5eb1fbd` and maps selected IDs one page at a time at `0x2474671`.
For this path XMem exports a real WC heap handle and checks re-import via
`ImportedBuffer::wc` before publishing any IDs. The run owns the handle until
whole-run free. It cannot be mapped into a cached CPU arena, nor can a cached
run be mapped into a WC arena. Mapping/unmapping requires Wine WC subview
support; an older Wine build rejects the map without a CPU fallback.

Each WC map replaces one vacant 64 KiB placeholder with a view at the selected
backing offset. Unmap restores the placeholder; use ordinary `VirtualFree`
only after unmapping all aliases. `ImportedBuffer::acquire` on a WC slot
requires Wine to return the full heap size and backing-relative byte offset.
The acquired GPU resource retains the backing even after the page IDs are freed.

The installed RTX 3070 tool passed `test_wc_pages.cpp` for the full 710 MiB
run. Wine patch `0004` maps from offset zero and moves the selected slice
without copying. Mapping the last page temporarily maps the full run; bulk
mapping throughput has not been measured.

Build `test_wc_pages.cpp` like `test_gpu.cpp` below, without `-DTEST_MARKED_SECTION`.
It checks first/last page selection, two aliases of the last page, a GPU resource
acquired from within that page, and teardown. With `-DTEST_MISSING_SLOT_ABI` it
instead checks rollback on a tool that supports WC heaps but lacks WC subviews.

### Graphics commit and consumption

The separate 64 KiB request at RVA `0x24786b9` (`NULL, 0x10000, 0x20401000,
0x283, 0x42404`) first tries the GPU-uncached control allocator.
Its protection includes `PAGE_GRAPHICS_NOCACHE`
(`0x40000`) in addition to CPU read/write, CPU write-combine and GPU read/write.
The title clears the full page, uses it as an aligned bump allocator and releases
it at `0x248be0f` with `VirtualFree(base, 0x10000, MEM_RELEASE)`.

A 64 KiB probe of `ScarlettWcHeapV1` on the qualified RTX 3070 / 595.112.07
passed CPU zeroing and fenced GPU read/write, but NVIDIA RM returned CPU
`COHERENCY_WRITE_COMBINE` **and** `NVOS32_ATTR2_GPU_CACHEABLE_YES`
(`attr=0x4a800000, attr2=0x5`); the DMA mapping used the allocation policy
(`NVOS46_FLAGS_GPU_CACHEABLE_DEFAULT`). This heap is unsuitable for the extra
modifier. The title's nonzero-size release also returned error 87 and left
that ordinary WC mapping committed; zero-size release succeeded. On the
identified RTX driver and title build the startup fallback uses a distinct,
CPU-only view that accepts exact-size release. It provides no GPU import.
See the [control-page tests](control-page.md).

In `scarlett-run.x6UanyCT/logs/steam-999001.log:239–246`, the trap is at
RVA `0x1e1d3a7`, with `RAX=0`, `R15=0x49400000` and pool index `R13=0`.
At `0x1e1d181` the call through thunk `0x5f3fcc0` (IAT `0x6793a58`,
`XMemVirtualAlloc`) passes `(NULL, 0x49400000, 0x20003000, 0x183, 0x22004)`.
At `0x1e1d189` a null result branches directly to this trap. That is a
1,172 MiB graphics reserve+commit with requested 2 MiB pages; the two
preceding large allocations were VA-only. `R9=0x182` in the exception log
is **after** the call; the call site loads `0x183`. No physical-page API
failed on this branch. Other pool cases at `0x1e1d288` and `0x1e1d369` use
the same flags with per-pool sizes. The installed fork passed this first commit
and reached `0x1e1d4d6`: a 2,199 MiB request with flags `0x283` and protection
`0x2404` returned null, followed by a breakpoint at `0x1e1d4e0`.

[`marked_section.h`](marked_section.h) uses the [native x64 Wine lifetime ABI](../../wine-patches/README.md)
to create the title view. Before returning it, XMem validates the entire
allocation on the default host D3D12 device (feature level 12.0): it acquires
the backing, maps a **privately owned view** and imports it via desktop
[`OpenExistingHeapFromAddress`](https://learn.microsoft.com/windows/win32/api/d3d12/nf-d3d12-id3d12device3-openexistingheapfromaddress),
then creates a placed cross-adapter buffer and checks its GPU virtual address.
The temporary resource, heap and alias are released immediately. Stock Wine
or a failed device/heap/buffer import returns `NULL` without leaving the title
view mapped. CPU-only requests do not load D3D12.

When consuming a live title range, use `ImportedBuffer::acquire(device,
address, bytes, &offset)` from `gpu_buffer.hpp` on the owning device. Use the
returned byte offset within `resource()` (or add it to the resource's GPU
VA). Keep the `ImportedBuffer` alive until every queue using it retires.
Ordinary title release and VA reuse leave this private binding on its original
backing. There is no cache keyed by title VA. This establishes shared storage
and 2 MiB **VA** alignment, not physical large pages, console cache policy or
GPU-local bandwidth.

On the RTX 3070 test host a synthetic caller obtained the full `0x49400000`
allocation through the production export, imported its final four bytes, and
retained the old data across ordinary release and exact VA reuse. The same
export failed with the requested VA still free on stock Wine and on patched
Wine with no usable Vulkan device.

After building the DLL and import library below, run the GPU lifetime test
on the NixOS GPU test host with a patched compatibility tool:

```sh
mkdir -p build/gpu-game build/gpu-run
cp build/xmem.dll build/gpu-game/
x86_64-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -Werror -static \
  -DTEST_MARKED_SECTION test_gpu.cpp -Lbuild -lxmem -ld3d12 -o build/gpu-game/game_scarlett_master.exe
TMPDIR="$PWD/build/gpu-run" ../../test-launch-nixos.sh "$PWD/build/gpu-game" "$patched_tool"
grep -h 'PASS:' build/gpu-run/scarlett-run.*/logs/steam-999001.log
```

The harness requires this executable name; the directory above contains only
the synthetic test and the adjacent provider.

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
x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -Werror -c xmem.c -o build/xmem.o
x86_64-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -Werror -shared -static build/xmem.o graphics.cpp -lntdll \
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
# On stock Wine, also check that the graphics path fails and leaves the VA free:
wine build/test.exe --no-graphics
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
