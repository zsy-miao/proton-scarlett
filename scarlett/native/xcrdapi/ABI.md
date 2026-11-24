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

The file returned by `+20` is stored in a wrapper at `f43623`. That routine
opens a *separate* Win32 handle by path at `f4363f` and reads from it. This
does not establish the order of the console file object's `Close` and
`GetHandle` methods. The DLL therefore declines console `OpenFile` rather
than returning the Windows file vtable. Status and queue creation likewise
remain unavailable; no retail data-path success is implied.
