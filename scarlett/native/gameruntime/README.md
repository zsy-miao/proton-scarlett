# xgameruntime.dll

Original Win64 provider for the runtime bootstrap in retail build `1.0.2.1`.
Build dependencies are MinGW-w64 and its Win32 import libraries. No GDK
code or console runtime is linked or redistributed.

## Loader and ABI evidence

The recorded stop is at RVA `5a91c21`, `EAX=89240101`. `5a91bd8` calls
`55dcf74` (`XGameRuntimeInitialize` in the executable), which passes `(0,NULL)`
to `55e2a70`. `55e2a52` calls `LoadLibraryExW` with the UTF-16 name at
`678e9a0` via the pointer at `67e2008`: **xgameruntime.dll**. Normal search
flags are `0x800` (system32); `0xa00` also searches the application directory
when caller policy allows it. For integration put the native DLL in the
prefix's **64-bit system32** rather than relying on the current directory.

The dispatch table in the *caller* is assembled using `GetProcAddress`:

| RVA of cached pointer | Export | Win64 signature |
| --- | --- | --- |
| `6e04eb0` | `QueryApiImpl` | `HRESULT(const GUID *provider, const GUID *iid, void **out)` |
| `6e04eb8` | `InitializeApiImpl` | `HRESULT(uint64_t, uint64_t)` |
| `6e04ec0` | `InitializeApiImplEx` | `HRESULT(uint64_t sdk, uint64_t build, uint64_t flags)` |
| `6e04ec8` | `InitializeApiImplEx2` | same, plus `InitOptions *` in R9 |
| `6e04ed0` | `XErrorReport` | `void(HRESULT, const char *)` |
| `6e04ed8` | `UninitializeApiImpl` | `void(void)` |

Query, error, uninitialize and at least one initialization export are required;
an incomplete export set also produces `89240101`. Ex2 is preferred over Ex
over legacy. The retail Ex2 call at `55e2cc5` passes `633600002712`,
`65f400001edd` (hex), flag `2` (or `a` for app-local), `NULL`. The legacy
fallback at `55e2d00` passes `6336,2712` (hex). No table of service function
pointers is exchanged at initialization. With-options callers pass a private
16-byte record: `u32 version=1`, `u32 flags` (bit 0: inline), `char const
*gameConfig` at +8; default has zero flags and a null pointer. Only default
options are supported. A request to check existing options (flag 4) or use an
existing initialization implicitly (flag 1) does not acquire a client reference.

The signatures, options record and service slots were cross-checked against
the **caller** in [Microsoft.GDK.PC 2410.0.1874](https://www.nuget.org/packages/Microsoft.GDK.PC/2410.0.1874):
`GameKit/Lib/amd64/xgameruntime.lib`, `main.obj` symbol names and relocations;
`XGameRuntimeInitializeWithOptions` constructs the private record on its stack.
This public PC caller has a different build value from retail. Private GUIDs
and slot offsets, not the PC build number, were used for corroboration.

## Implemented services

- Initialization acquires one module reference on first explicit use, records
  the Win32 guest OS version, and tracks explicit clients. Last uninitialize
  clears state and releases the module. Outstanding acquired interfaces at
  last uninitialize report `89240105`, retain the module and prevent restart,
  as described in [runtime lifecycle](https://learn.microsoft.com/en-us/gaming/gdk/docs/reference/system/xgameruntimeinit/functions/xgameruntimeinitializewithoptions).
- Each served GUID is both provider and IID. `abi.h` gives the exact GUIDs
  and tables. Slots 0–2 are QueryInterface/AddRef/Release. `IFeatureImpl1`
  slot 3 returns a byte bool for a 32-bit feature number (retail `55dca58`).
  Only `XError` (19) is advertised; in particular both observed feature probes,
  networking (10) and game events (20), return false.
- `IErrorImpl1` slots 3–5 report errors, set a callback/context, and set
  debugger-present/absent options. The exported reporter uses the same
  registration; a callback may suppress further action. Options select
  debug-string output, a breakpoint or fail-fast. [XError reference](https://learn.microsoft.com/en-us/gaming/gdk/docs/reference/system/xerror/xerror_members).
- `IAnalyticsInfoImpl1` slot 3 takes `this, resultBuffer`, returns that
  buffer, and fills `0x90` bytes (retail `55dea0c`). OS and hosting-OS
  versions come from `RtlGetVersion`, revision unspecified (0); family and
  form identify the Windows PC guest. No console ID or console OS is invented.
  This one method does not imply the entire XSystem feature is available.

Other provider GUIDs return `REGDB_E_CLASSNOTREG` and null output; a different
IID on a served provider returns `E_NOINTERFACE`. `55de08e` translates the
former to `89240107`. For example, GUID `073b7dcb-1fcf-4030-94be-e3c9eb623428`
is shared by async, task queues and time-sensitive thread methods;
`55ddff0` queries it for slot `+e8` (`XThreadAssertNotTimeSensitive`) and
ignores lookup failure, so that lookup alone is **not** evidence of the next
fatal stop. Subsequent services, package/config data, and identity are not
created by initialization. To identify a later failed lookup, inspect the two
GUIDs in RCX/RDX at `55de087` and HRESULT at `55de08c`.

## Synthetic test

From this directory:

```sh
mkdir -p build
x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -Werror -shared \
  gameruntime.c -lntdll -o build/xgameruntime.dll
x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -Werror test.c -o build/test.exe
WINEPREFIX="$(mktemp -d)" WINEDEBUG=-all wine build/test.exe
```

The test dynamically loads the DLL, probes all six names, exercises all
three init ABIs, rejects unknown versions/options/services, calls the service
vtables, and checks both normal unload/reload and terminal active-object
shutdown. It uses neither the game executable nor a GDK redistributable.
