# xmem.dll — Windows x64 subset for Proton

This original DLL has two named exports. It needs only Win32; there is no
console SDK, external compatibility runtime, or ordinal compatibility contract.

| Export | Supported behavior |
| --- | --- |
| [`XMemIsToolsMemAvailable`](https://docs.xbox.com/reference/system/xmem/functions/xmemistoolsmemavailable) | Returns `FALSE`: there is no profiling-mode tools partition. |
| [`XMemVirtualAlloc`](https://docs.xbox.com/reference/system/xmem/functions/xmemvirtualalloc) | Untagged `XMEM_CPU` (`0x00000001`), ordinary 4 KiB pages, `MEM_RESERVE`, `MEM_COMMIT`, or both, with `PAGE_READWRITE` or `PAGE_READONLY`. Uses Win32 `VirtualAlloc` with the caller's base, size and protection; release with `VirtualFree`. |

Unsupported allocation flags, types, or protections return `NULL` and set
`ERROR_NOT_SUPPORTED` (this DLL's diagnostic). This includes graphics/tools
memory, mappable memory, tags, special page sizes, and protection modifiers.
Errors from supported calls pass through from Win32. There is no console
budget accounting.

Other imports are absent, including `XMemAlloc`, `XMemAllocDefault`,
`XMemFree`, `XMemFreeDefault`, `XMemAllocatePhysicalPages`,
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
WINEPREFIX="$PWD/build/prefix" WINEDLLOVERRIDES="xmem=n" wine build/test.exe
```

The synthetic executable imports the adjacent DLL by name, checks page state
and protection through `VirtualQuery`, writes and releases memory, and tests
refusal of unsupported requests. For a Proton test use the same adjacent
DLL and executable with a disposable `STEAM_COMPAT_DATA_PATH` and
`WINEDLLOVERRIDES=xmem=n`.
