# Scarlett section lifetime ABI v1 (native x64)

For the 1024² and 2048² linear A8 atlas maps, apply `0017-wine-atlas-map-pin.patch`
after `0016` and stage the rebuilt `ntdll.so` with the matching `xg_xs.dll`
and `d3d12_xs.dll`. Native Map pins the pool generation until Unmap; on RTX
the wrapper maps an alias of the retained section at the atlas offset.

For the opt-in private PresentX bridge, apply `0016-wine-title-window-lease.patch`
after `0015` in the numbered Wine series. It extends the title-window generation
from `0008`: win32u requests a lease, and wineserver serializes it against
retirement. From the matching x64 Wine build, run
`make -j8 dlls/win32u/win32u.so server/wineserver`. Stage **both** outputs
at `files/lib/wine/x86_64-unix/win32u.so` and `files/bin/wineserver` in the
private tool. Keep the matched ntdll, 0033 graphics core and remaining providers.
See the [creator-blocked fixture and bridge build flags](../native/queue/present.md#opt-in-private-presentx-bridge).

Patch `0009` adds a separate CPU-only, write-combined control-page view for
the RTX startup path. `0x53430014` maps the tagged 64 KiB backing;
`0x53430015` returns its allocation identity without a handle; `0x53430016`
checks that a host-import range does not overlap it. The last check is used
by win32u's Vulkan host-pointer import. Build and stage `ntdll.so` and
`win32u.so` together. See the [hardware tests](../native/xmem/control-page.md).

`0001-ntdll-scarlett-section-v1.patch` targets Valve Wine
`e60892cc2398b0a83c332a2078c7b85e47bdb42b`. It lets XMem return a
section view that accepts ordinary `VirtualFree`, while a graphics consumer
can independently retain the backing of a live title range. There are no
new exports, wineserver requests, hooks, or changes to the Wine gitlink.

Apply to a clean scratch checkout at the pin:

```sh
patch=$(realpath scarlett/wine-patches/0001-ntdll-scarlett-section-v1.patch)
test "$(git -C "$wine" rev-parse HEAD)" = e60892cc2398b0a83c332a2078c7b85e47bdb42b
git -C "$wine" apply --check --whitespace=error-all "$patch"
git -C "$wine" apply "$patch"
```

For the standalone Steam installer, configure this patched source as a native
64-bit Wine build in a separate build directory. Run `make dlls/ntdll/ntdll.so`
there, then copy `dlls/ntdll/ntdll.so` to the staged `scarlett/runtime/` next
to the built PE DLLs before running `scarlett/install.sh`. This replaces only
the fork's x86-64 Unix ntdll. For a full Proton build, apply the patch in
the pinned `wine/` submodule before building the distribution.

For example, with GCC, autoconf, flex, bison, Perl and Python on the build host:

```sh
repo=$PWD
(cd "$wine" && perl tools/make_requests && perl tools/make_specfiles &&
  python3 dlls/winevulkan/make_vulkan -x vk.xml -X video.xml && autoconf && autoheader)
wine=$(realpath "$wine")
build=$(mktemp -d)
(cd "$build" && bash "$wine/configure" --enable-win64 --without-x --without-freetype --disable-tests &&
  make -j8 dlls/ntdll/ntdll.so)
install -m755 "$build/dlls/ntdll/ntdll.so" "$repo/scarlett/runtime/ntdll.so"
```

These configure flags are for **this one Unix library**; use the matching Valve
depot for the rest of Wine. Run the GPU test after installing to verify the
actual tool and host rather than assuming a successful build was sufficient.

## Calling convention

Use the existing `ntdll!NtQueryVirtualMemory` syscall with the **current
process pseudo-handle**. The private information class identifies the ABI
version and operation. On stock pinned Wine both classes return
`STATUS_INVALID_INFO_CLASS`. Require `STATUS_SUCCESS` before using outputs;
do not fall back to an ordinary anonymous allocation. Neither WOW64 nor
remote acquisition is supported.

```c
/* NtQueryVirtualMemory(GetCurrentProcess(), address, class, &info, sizeof info, NULL) */
#define SCARLETT_CREATE_V1  0x53430001
#define SCARLETT_ACQUIRE_V1 0x53430002
struct scarlett_section_info {
    SIZE_T size;       /* input byte count */
    void  *base;       /* output: public allocation base */
    SIZE_T view_size;  /* output: whole allocation size in bytes */
    SIZE_T offset;     /* output: byte offset from start of section */
    HANDLE section;    /* output: caller-owned only on successful acquire */
};
/* x64: 40 bytes, offsets 0, 8, 16, 24, 32 */
```

Zero-initialize `info`, then set `size`. The struct must remain writable
for the duration of the call. `ReturnLength` may be NULL; otherwise supply a
separate writable `SIZE_T` (40 on success). On error, ignore all outputs.

| Operation | `address` / `size` | Success |
| --- | --- | --- |
| Create | `NULL` for automatic placement, or an exact vacant 2 MiB-aligned VA. `size` is a positive multiple of 2 MiB. | Fresh committed, shared, read/write, pagefile-backed section, mapped at `base`. `view_size == size`, `offset == 0`, `section == NULL`. Release with `VirtualFree(base, 0, MEM_RELEASE)`. |
| Acquire | Any byte in a live **marked** view. `size` is the nonzero number of bytes the consumer needs, wholly within the view. | Independent non-inheritable section handle (`SECTION_MAP_READ`, `SECTION_MAP_WRITE`, `SECTION_QUERY`), with `base`, `view_size` and `offset` from that same view. Close the handle with `CloseHandle`. |

Creation makes alignment a **VA** property; it does not promise physical
2 MiB pages, GPU visibility, cache policy, or local video memory. Treat the
result as CPU backing until the owning graphics device successfully imports
its private alias. The returned handle refers to the whole section; the range
check validates the request but does not restrict handle access to a subrange.

## Release, acquire, and reuse

Creation publishes the mark and its owned section handle under Wine's VM
mutex. Acquire checks bounds by subtraction and duplicates the handle under
the same mutex. `VirtualFree` on the marked view's **exact base** and **zero
size** goes through existing `NtUnmapViewOfSection`: it removes the server
mapping and deletes the local view before another lookup or reuse can win.
An explicit section unmap also drops the mark. Only marked views get the
extra release behavior. `MEM_DECOMMIT`, partial releases, and write-copy
protection are unsupported for them.

A consumer must **acquire when it consumes a still-live title pointer**, map
the returned section independently, and import that private alias. Map from
section offset zero, and add `info.offset` to the private resource address.
Retain the private view, heap and resource until all consuming queues retire.
Never use the title VA as a resource-cache key. After reuse by another marked
allocation, a *new* acquire identifies the new allocation; a raw pointer
alone cannot request an earlier generation. Existing acquired handles and
private views stay attached to their original section.

## Runtime acceptance tests for the patched build

1. Create a marked view; acquire its last byte and confirm the returned
   offset, section size and shared writes through a private alias. Reject
   `size == 0`, a range extending one byte past the end, and `SIZE_MAX` from
   an interior address.
2. Reject interior `VirtualFree` (including `base + 1`), `MEM_DECOMMIT`, and
   nonzero release size without losing the view. Exact ordinary release
   must succeed. Acquire must then fail for the vacant VA and for an
   ordinary allocation or ordinary section view placed at the same VA.
   `VirtualFree` must still reject the ordinary section view.
3. Recreate a marked view at the same VA. Before and after release/reuse,
   acquired handles must map the old and new sections respectively. Race
   acquire against release with the address vacant until both calls return:
   each result must be either the correct original backing or failure.
4. At `0x49400000` bytes, hold a submitted GPU copy behind a fence; release
   the title view with ordinary `VirtualFree`, reuse its VA and write a fresh
   marker, then open the gate. Check that the copy updated only the old
   private alias, that the replacement marker survived, and that cleanup
   follows the retirement fence.

The patched x64 Unix ntdll built at the pin and passed test 4 on the RTX 3070.
The installed tool passed tests 1–3 with `test-section.c` (256 acquire/free races).
Build `scarlett/native/xmem/test_gpu.cpp` as in the [XMem instructions](../native/xmem/README.md)
with `-DTEST_MARKED_SECTION`, and pass the patched tool directory as the second
argument to `test-launch-nixos.sh`. The passing fixture prints
`ordinary VirtualFree released marked title view; GPU retired on old backing after exact public VA reuse`.
