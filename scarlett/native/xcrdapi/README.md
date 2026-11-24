# XCRDAPI.dll — read-only files, console staging and status arrays

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
`E_NOTIMPL`. Staging changes while a console file object exists return
`HRESULT_FROM_WIN32(ERROR_BUSY)`. `GetStats` reports current/peak staging bytes
and current/peak/cumulative file opens; the observed realtime queue class reports queue, request, byte and error counters. Console `CreateStatusArray` supports
1–65,536 independent entries. Each starts as `E_PENDING`/incomplete (a local
choice; the GDK leaves pre-enqueue state undefined). Out-of-range indices
return `E_INVALIDARG`/false. Console `OpenFile` opens an existing Unicode path
read-only and shared for reads. `GetHandle` returns a caller-owned read-only
handle that survives object `Close` or final `Release`; callers use
`CloseHandle`. After `Close`, `GetHandle` returns `INVALID_HANDLE_VALUE` with
`ERROR_INVALID_HANDLE`. Console `CreateQueue1` supports the [captured file queue](ABI.md#createqueue1-captured-descriptor-and-supported-slots).
An explicit submit runs offset reads on a FIFO worker. Status markers report
completion or the first error. Option 1 reads one zlib-wrapped file range to
linear memory. Source and destination are each bounded to 32 MiB. Invalid
streams, trailing bytes and size mismatches fail before destination publication.
The caller must retain the writable destination until completion; the queue
owns an independent file handle and copies the request at enqueue.

Before this queue implementation, on RADV and RTX 3070, retail build `1.0.2.1` called factory slot `+0x50`
(`CreateQueue1`) with a descriptor and queue IID
`3c02cb0c-c050-4195-99a1-a1ce94ba33c5`. The provider returned
`0x80004001` and a null queue to title RVA `+0x28c95ca`. The same thread
then hit the breakpoint at `+0x28c95ce`, before it could submit storage work.

## Build and verify

From this directory, build the DLL offline with Bash and x86-64 MinGW-w64.
The build uses the inflate sources from the pinned `wine/libs/zlib` submodule
directory (or set `SCARLETT_ZLIB_SRC` to that directory in an existing matching
Wine source checkout). These are zlib by Jean-loup Gailly and Mark Adler;
see `wine/libs/zlib/LICENSE`. No additional runtime DLL or host libz is needed.

```sh
mkdir -p build
source ./zlib-flags.sh
x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -Werror -shared xcrdapi.c "${zflags[@]}" -luuid -o build/XCRDAPI.dll
```

Run `./test.sh` for an independent ABI check. It builds a C++ caller against
`native/include/dstorage.h` from Microsoft's [Windows DirectStorage 1.2.3
NuGet package](https://www.nuget.org/packages/Microsoft.Direct3D.DirectStorage/1.2.3);
the script verifies the package SHA-256 and extracts only headers. The caller
imports the new DLL by name and dispatches through the published vtables.
The separate console caller tests the observed GUIDs and raw vtable offsets,
checking the 32 MiB commit and release independently with `VirtualQuery` and
creating, polling and releasing a status object. It also compiles against the
pinned public ATG file interface declaration and verifies read access, refusal
of writes and independent handle ownership across `Close` and final `Release`.
`test_status.c` drives the
private writer with synthetic request results to verify pending, successful
and failed completion, index isolation and reuse.
`test_queue.cpp` uses the public title slots to test actual reads, pending and terminal status, malformed requests, full-queue backpressure and final release.
Only synthetic callers run, in a newly created private Wine prefix
removed on exit. No SDK runtime DLL is installed or loaded. The resulting
`build/XCRDAPI.dll` remains available after the test.
