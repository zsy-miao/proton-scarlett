# Console factory: caller-visible evidence

Static inspection only. Image: `game_scarlett_master.exe`, SHA-256
`e95020b2fa4a721db3df59fdecf75621730ac559d8835252f6c3b658d5a08993`.
Addresses below are RVAs (image base `0x140000000`); offsets are bytes in a
Windows x64 COM vtable. These are cross-references in code, not a runtime trace.

`DStorageGetFactory` is named at IAT RVA `6793fb8`. The thunk at `5f3ff80`
jumps through it. At `1d8e4f0`, RCX points to `62fc7d8` (the IID below) and
RDX to `78f5e78` (the factory output). The caller requires a non-null object
and `S_OK`, then invokes `+40` with EDX = `02000000` and checks the HRESULT.

| Factory call | RVA of call | Evidence |
| --- | --- | --- |
| `+40 SetStagingBufferSize` | `1d8e515` | 32 MiB argument, HRESULT checked |
| `+20 OpenFile` | `28c8c1e`, `28c9d54` | UTF-16 path (also passed to `GetFileAttributesW`), file IID, output pointer; HRESULT checked |
| `+28 CreateStatusArray` | `28c9522` | capacity 1, name pointer, status IID, fifth argument is output pointer; caller does not check HRESULT here |
| `+50 CreateQueue1` | `28c95c7` | descriptor, queue IID, output pointer; caller checks HRESULT |
| `+10 Release` | `28cb203` | tail-call on stored factory at teardown |

| IID argument | RVA | GUID decoded in Windows byte order |
| --- | --- | --- |
| Factory | `62fc7d8` | `62959d11-d45f-4884-a0a3-f7862ea243ae` |
| File | `62fdac8` | `43704f9f-27c6-4a9f-8815-d16527894df4` |
| Status | `62fc7e8` | `cd66d752-2786-44f7-9070-e2a6fdad3f1b` |
| Queue | `62fc7f8` | `3c02cb0c-c050-4195-99a1-a1ce94ba33c5` |

For example, reproduce the initialization evidence with GNU binutils:

```sh
objdump -p game_scarlett_master.exe | grep -A3 'DLL Name: XCRDAPI'
objdump -d -Mintel --start-address=0x141d8e4d0 --stop-address=0x141d8e522 game_scarlett_master.exe
objdump -s --start-address=0x1462fc7d8 --stop-address=0x1462fc808 game_scarlett_master.exe
```

**What is inferred:** the method names and factory generation agree with the
public [GDK DirectStorage overview](https://learn.microsoft.com/en-us/gaming/gdk/docs/features/console/storage/directstorage/directstorage-overview):
`IDStorageFactoryX1` adds `GetStats`, and `IDStorageFactoryX2` adds `CreateQueue1`.
That places `GetStats` at `+48`, immediately before the observed extended queue
call. Its output uses the documented [DSTORAGE_STATS](https://learn.microsoft.com/en-us/gaming/gdk/docs/reference/system/dstorage/structs/dstorage_stats)
layout. This is not evidence that the title invokes `GetStats`. The two base
scalar methods between `+28` and `+40` are not distinguished by these sites.
Both use the same fail-closed machine-level stub; neither mutates state.

## Console file and queue source

Microsoft's public [ATG sample interface declaration, lines 223–233](https://github.com/microsoft/Xbox-GDK-Samples/blob/b1e77b6ac10f95b07296025fda171ca2ddc42dbe/Samples/System/SimpleDirectStorageCombo/DirectStorageWin32/dstorage_win32.h#L223-L233)
binds **exactly** the file IID above to an `IUnknown`-derived interface with
`void Close()` first (`+18`) and `HANDLE GetHandle()` second (`+20`). The
sample calls the type `IDStorageFileWin32`; its IID, not its name, identifies
the interface. This is different from the Windows DirectStorage `IDStorageFile`.
The [GDK GetHandle contract](https://learn.microsoft.com/en-us/gaming/gdk/docs/reference/system/dstorage/interfaces/idstoragefilex/methods/idstoragefilex_gethandle)
specifies read-only access, shared reads, and caller `CloseHandle`. The DLL
reopens its owned file handle under a lock so each caller owns its handle and
the file object can close independently. Only the declarations are used
for testing; the DLL uses no ATG implementation.

The retail caller does **not** prove the two file-specific slots by itself:
it stores the returned COM pointer in the first word of a wrapper at `f43623`,
then independently calls Win32 `CreateFileW` at `f4363f` and `ReadFile` at
`f4368a` on the path. The Win32 handle winds up at wrapper `+30` (`f436be`),
not in the wrapper's COM pointer field.

At `28c9713` the caller enters the request builder. Its file branch at
`28c99ab` dereferences the wrapper once; it stores the resulting **COM file
pointer** at request `+20` and the 64-bit offset at `+28`. The memory branch
at `28c99a0` instead adds the offset to a base address and sets option bit
`0x400000`. The builder puts the source byte count at request `+30` and
passes the stack request to **queue** slot `+18` at `28c9a0b`. This is not
a call to file slot `+18`; the queue receives neither a Win32 handle nor a
path. See the public [request source union](https://github.com/microsoft/Xbox-GDK-Samples/blob/b1e77b6ac10f95b07296025fda171ca2ddc42dbe/Samples/System/SimpleDirectStorageCombo/DirectStorageWin32/dstorage_win32.h#L271-L306).

That sample declares a **different
queue IID** (`a3f6649a-8c2c-4b3a-8b6e-10186a04b9bc`), so it does not
verify the full vtable for the title's `3c02cb0c-...` queue or its extensions.
The observed base calls and source union alone are insufficient: a queue needs
an independently established slot/signature map for that IID and supported
request options, plus actual submission, file lifetime, and status/error
handling. In particular, [EnqueueRequest](https://learn.microsoft.com/en-us/gaming/gdk/docs/reference/system/dstorage/interfaces/idstoragequeuex/methods/idstoragequeuex_enqueuerequest)
lets the caller reuse the request structure immediately; retaining a pointer
to this stack request would be incorrect.

## Console status array

At RVA `28c9507`, the fifth argument for `CreateStatusArray` points to the
wrapper's `+30` field; `28c9615` reloads that field and `28c961f` releases
the object through `+10`. At `e8940` the queue (`wrapper+18`) receives the
status object (`wrapper+30`) and index 0 in RDX/R8D, then the caller submits
through queue slot `+30` at `e894d`. The poll at `e8981` dispatches **status**
slot `+18` with EDX=0 and branches on AL. `4f65847` does the same in a
nonblocking check. Both match `bool IsComplete(UINT32)`. The documented
[IDStorageStatusArrayX](https://learn.microsoft.com/en-us/gaming/gdk/docs/reference/system/dstorage/interfaces/idstoragestatusarrayx/idstoragestatusarrayx)
has only one other method after `IUnknown`: `GetHResult(UINT32)` therefore
occupies `+20`; this is not a claim that a direct caller of that slot was seen.

[EnqueueStatus](https://learn.microsoft.com/en-us/gaming/gdk/docs/reference/system/dstorage/interfaces/idstoragequeuex/methods/idstoragequeuex_enqueuestatus)
explicitly leaves pre-enqueue state undefined. This implementation initializes
each slot to `E_PENDING`. Once enqueued, [GetHResult](https://learn.microsoft.com/en-us/gaming/gdk/docs/reference/system/dstorage/interfaces/idstoragestatusarrayx/methods/idstoragestatusarrayx_gethresult)
stays pending while any covered request is outstanding, then reports `S_OK`
or the first failure in request order. For valid indices, [IsComplete](https://learn.microsoft.com/en-us/gaming/gdk/docs/reference/system/dstorage/interfaces/idstoragestatusarrayx/methods/idstoragestatusarrayx_iscomplete)
is true on either terminal result. The file queue uses these transitions at enqueued status boundaries.

## CreateQueue1: captured descriptor and supported slots

With the 9f4dea89 R8 provider, a scratch `+50` probe on both RADV and RTX
3070 read the same four little-endian QWORDs in the caller's 32-byte
record: `0`, `0x2103f`, `0x1467835d6`, `1`. The third points to the optional
debug name `fiDeviceDirectStorage`; it is not an identity or a capability.
The [GDK queue descriptor](https://learn.microsoft.com/en-us/gaming/gdk/docs/reference/system/dstorage/structs/dstorage_queue_desc1?view=gdk-2510)
uses a UINT64 source, UINT16 capacity, INT8 priority, name pointer and
[64-bit options](https://learn.microsoft.com/en-us/gaming/gdk/docs/reference/system/dstorage/structs/dstorage_queue_options?view=gdk-2510).
Thus: file source (0), capacity 4,159, realtime priority (2), and bit 0
disabling automatic submission. Mixed-priority scheduling is not implemented.

The title uses queue slots `+18` (request, `28c9a0b`), `+20` (status,
`e8940`), `+30` (submit, `e894d`), and `+40` (no-argument teardown at
`28c9637`, before Release). These match the base method shapes and
[GDK queue contracts](https://learn.microsoft.com/en-us/gaming/gdk/docs/reference/system/dstorage/interfaces/idstoragequeuex/idstoragequeuex?view=gdk-2510).
The implementation binds only these slots and IUnknown for the captured IID;
other slots through `+68` stop instead of guessing a return signature.
The [request layout](https://learn.microsoft.com/en-us/gaming/gdk/docs/reference/system/dstorage/structs/dstorage_request?view=gdk-2510)
and its [options](https://learn.microsoft.com/en-us/gaming/gdk/docs/reference/system/dstorage/structs/dstorage_request_options?view=gdk-2510)
qualify plain file reads only: options and intermediate size zero, equal
source/destination byte counts, a linear destination of at most 32 MiB.
Destinations must remain writable through completion. The worker serializes
reads per queue; a status marker reports the first failure since the preceding
marker, including across submit boundaries. The caller must not reuse an
in-flight status index. Full queues wait for space until another thread
submits. Closing or releasing the final reference drains the queue and joins
the worker, including when the title has released the source file or array.

Both unchanged retail runs with this queue passed `+28c95ce` and stopped on
Wine's unimplemented import `xmem.dll.XMemMapPhysicalPagesScatter`. The
queue transfer and completion evidence is from the synthetic fixture.
