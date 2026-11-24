# XCRDAPI.dll — Windows x64 file and console staging subsets

The sole named export is `HRESULT WINAPI DStorageGetFactory(REFIID, void **)`.
It returns a process-local singleton with distinct interfaces for the Windows
`IDStorageFactory` and the title's console factory. On the Windows path,
unknown factory/file interfaces return `E_NOINTERFACE` and
clear the output pointer. The implementation is original C using Win32.

| Windows operation | Behavior |
| --- | --- |
| `OpenFile` | Opens an existing Unicode path read-only, shared for reads. Returns a refcounted Windows `IDStorageFile` or `IUnknown`; Win32 open errors become HRESULTs. |
| `GetFileInformation` | Retrieves `BY_HANDLE_FILE_INFORMATION` from the open handle. After `Close`, returns `HRESULT_FROM_WIN32(ERROR_INVALID_HANDLE)`. |
| `Close`, `Release` | `Close` is idempotent and does not release COM references. Final `Release` closes the handle even without explicit `Close`. |
| `CreateQueue`, `CreateStatusArray`, `SetStagingBufferSize` | `E_NOTIMPL` for valid requests. No data transfers, GPU work, or decompression. |
| `SetDebugFlags` | Void method; no debug layer. |

The [console ABI evidence](ABI.md) identifies the title's factory IID and
its first operation, `SetStagingBufferSize(32 MiB)`. The console interface
commits a private 32 MiB buffer with `VirtualAlloc`, reuses it on repeated
requests, and releases it on size zero or DLL unload; other sizes return
`E_NOTIMPL`. `GetStats` reports current/peak staging bytes; queue, storage,
and decompression counters are zero. Console `CreateStatusArray` supports
1–65,536 independent entries. Each starts as `E_PENDING`/incomplete (a local
choice; the GDK leaves pre-enqueue state undefined). Out-of-range indices
return `E_INVALIDARG`/false. Console file and queue creation return `E_NOTIMPL`
and clear the output. No requests are submitted.

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
The separate console caller tests the observed GUIDs and raw vtable offsets,
checking the 32 MiB commit and release independently with `VirtualQuery` and
creating, polling and releasing a status object. `test_status.c` drives the
private writer with synthetic request results to verify pending, successful
and failed completion, index isolation and reuse.
Only synthetic callers run, in a newly created private Wine prefix
removed on exit. No SDK runtime DLL is installed or loaded. The resulting
`build/XCRDAPI.dll` remains available after the test.
