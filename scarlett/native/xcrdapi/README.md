# XCRDAPI.dll — Windows x64 read-only file subset

The sole named export is `HRESULT WINAPI DStorageGetFactory(REFIID, void **)`.
It returns a process-local singleton for the Windows `IDStorageFactory` or
`IUnknown` IID. Unknown factory/file interfaces return `E_NOINTERFACE` and
clear the output pointer. The implementation is original C using Win32.

| Operation | Behavior |
| --- | --- |
| `OpenFile` | Opens an existing Unicode path read-only, shared for reads. Returns a refcounted Windows `IDStorageFile` or `IUnknown`; Win32 open errors become HRESULTs. |
| `GetFileInformation` | Retrieves `BY_HANDLE_FILE_INFORMATION` from the open handle. After `Close`, returns `HRESULT_FROM_WIN32(ERROR_INVALID_HANDLE)`. |
| `Close`, `Release` | `Close` is idempotent and does not release COM references. Final `Release` closes the handle even without explicit `Close`. |
| `CreateQueue`, `CreateStatusArray`, `SetStagingBufferSize` | `E_NOTIMPL` for valid requests. No data transfers, GPU work, or decompression. |
| `SetDebugFlags` | Void method; no debug layer. |

The [GDK export](https://learn.microsoft.com/en-us/gaming/gdk/docs/reference/system/dstorage/functions/dstoragegetfactory)
has the same signature, but that alone does not establish binary compatibility
with `IDStorageFactoryX`, `X1`, `X2`, or `IDStorageFileX`. Those IIDs are not
implemented. In particular, the Windows file interface exposes `GetFileInformation`;
the console file interface exposes `GetHandle`.

## Build and verify

From this directory, build the DLL offline with x86-64 MinGW-w64:

```sh
mkdir -p build
x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -Werror -shared xcrdapi.c -luuid -o build/XCRDAPI.dll
```

Run `./test.sh` for an independent ABI check. It builds a C++ caller against
`native/include/dstorage.h` from Microsoft's [Windows DirectStorage 1.2.3
NuGet package](https://www.nuget.org/packages/Microsoft.Direct3D.DirectStorage/1.2.3);
the script verifies the package SHA-256 and extracts only headers. The caller
imports the new DLL by name and dispatches through the published vtables.
Only the synthetic fixture runs, in a newly created private Wine prefix
removed on exit. No SDK runtime DLL is installed or loaded. The resulting
`build/XCRDAPI.dll` remains available after the test.
