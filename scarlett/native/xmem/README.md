# xmem.dll — Windows x64 subset for Proton

This original DLL has six named exports. It needs only Win32; there is no
console SDK, external compatibility runtime, or ordinal compatibility contract.

| Export | Supported behavior |
| --- | --- |
| [`XMemAlloc` and `XMemFree`](https://docs.xbox.com/reference/system/xmem/macros/xmem_set_allocation_hooks) | Dispatch to the main EXE's `__XMemAllocationHooks__` data export, or to the defaults when absent. Both callbacks are copied at DLL process attach; neither is called during binding. A present but incomplete pair fails dispatch with `ERROR_INVALID_DATA`. |
| [`XMemAllocDefault`](https://docs.xbox.com/reference/system/xmem/functions/xmemalloc_routine) and [`XMemFreeDefault`](https://docs.xbox.com/reference/system/xmem/functions/xmemfree_routine) | Cached CPU heap, ordinary pages, minimum 16-byte alignment. A tracking callback can call these directly without re-entering itself. |
| [`XMemIsToolsMemAvailable`](https://docs.xbox.com/reference/system/xmem/functions/xmemistoolsmemavailable) | Returns `FALSE`: there is no profiling-mode tools partition. |
| [`XMemVirtualAlloc`](https://docs.xbox.com/reference/system/xmem/functions/xmemvirtualalloc) | Untagged `XMEM_CPU` (`0x00000001`), ordinary 4 KiB pages, `MEM_RESERVE`, `MEM_COMMIT`, or both, with `PAGE_READWRITE` or `PAGE_READONLY`. Uses Win32 `VirtualAlloc` with the caller's base, size and protection; release with `VirtualFree`. |

Unsupported virtual allocation flags, types, or protections return `NULL` and set
`ERROR_NOT_SUPPORTED` (this DLL's diagnostic). This includes graphics/tools
memory, mappable memory, tags, special page sizes, and protection modifiers.
Errors from supported calls pass through from Win32. There is no console
budget accounting.

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

Other imports are absent, including `XMemAllocatePhysicalPages`,
`XMemFreePhysicalPages`, `XMemMapPhysicalPages`, `XMemMapPhysicalPagesScatter`,
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
for mode in 0 1 2; do
  x86_64-w64-mingw32-gcc -O2 -Wall -Wextra -Werror -DHOOK_MODE=$mode test_alloc.c \
    -Lbuild -lxmem -o "build/test-alloc-$mode.exe" || exit 1
done
export WINEPREFIX="$PWD/build/prefix" WINEDLLOVERRIDES=xmem=n
for exe in build/test.exe build/test-alloc-{0,1,2}.exe; do wine "$exe" || exit 1; done # bash
```

The synthetic executable imports the adjacent DLL by name, checks page state
and protection through `VirtualQuery`, writes and releases memory, and tests
refusal of unsupported requests. `test_alloc.c` builds three EXEs: no hook
export, a complete pair, and a partial pair. They check alignment, original
attribute matching, full-width sizes/attributes through a callback, nested
calls, direct defaults, and binding before `main`. For a Proton test use the same adjacent
DLL and executable with a disposable `STEAM_COMPAT_DATA_PATH` and
`WINEDLLOVERRIDES=xmem=n`.
