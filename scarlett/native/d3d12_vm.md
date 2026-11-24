# D3D12ConfigureVirtualMemoryX

The native export accepts a readable 32-byte block of eight little-endian DWORDs:
`{0, 3 or 4, 0, 0, 0, 0, 0, 0}`. Success means the DLL has reserved and
committed a zeroed software table with one 64-bit slot per 64 KiB graphics page.
The two cases allocate 2 MiB and 3 MiB of host table storage, respectively.
Slots are available to track future mappings relative to a reservation; zero
means unmapped. No title address range or GPU memory is allocated by this call.

## Why those cases?

Microsoft's [public topic directory](https://docs.xbox.com/nda/nda-topic-directory.md)
describes this export as reserving and initializing graphics page tables. The
[memory manager overview](https://docs.xbox.com/build/console-features/memory/system-memory-working.md)
documents 64 KiB physical page mappings, including groups of 32 for 2 MiB
reservations. Neither source establishes the layout of the console page tables.

In the supplied title, RVAs `0x1e1cfa0`–`0x1e1cfe0` clear 32 bytes and pass
their address in RCX. Only DWORD `+4` changes. A boolean selects a span of 16
or 24 GiB; the code shifts the span right by 12, rounds up in groups of 2^22,
adds two, and stores the result. The intermediate helper at `0x4da2350`
returns its argument. The next XMem VA reservation receives the original span.
The retail trace had `RSI=0x600000000` (24 GiB, DWORD value 4). The 16 GiB
case (value 3) is statically evidenced by the other branch. Our inference is
limited to those two pairs: we size private bookkeeping to the **arena**, not
to the DWORD. Neither the hardware meaning of the two added units nor a general
conversion from the field to bytes is established.

## Errors and limits

An unreadable block returns `E_POINTER`, null returns `E_INVALIDARG`, and any
other field pattern (including all zeros) returns `HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED)`.
A failed table allocation returns `E_OUTOFMEMORY` without installing state.
Concurrent calls serialize. An identical configuration keeps the table;
switching to a different supported size returns `HRESULT_FROM_WIN32(ERROR_ALREADY_INITIALIZED)`.
These errors and repeat-call rules are local policy, not verified console HRESULTs.
Explicit DLL unload releases the table.

XMem mapping, physical pages, GPU-visible page tables, residency and device
creation are separate operations. No mapping API currently consumes this table.

## Synthetic test

From `scarlett/native`, build and run in a scratch directory. The test includes
the implementation to inspect the real allocation without adding a DLL export.

```sh
out=$(mktemp -d)
x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -Werror test-d3d12-vm.c -o "$out/vm.exe"
export WINEPREFIX="$out/prefix"
wine "$out/vm.exe" 3 && wine "$out/vm.exe" 4
```
