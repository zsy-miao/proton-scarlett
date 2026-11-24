# After list initialization (static RVAs, device IID `5405c344-…`, base `4553fc72`)

## Frame events

| Device offset | x64 call shape after `This`; return `HRESULT` |
|---|---|
| `+0x270` SetFrameIntervalX | `IDXGIOutput *syncTarget, UINT lengthUs, UINT periodInIntervals, flags32` |
| `+0x278` ScheduleFrameEventX | `eventType32, UINT offsetUs, scheduleObjectList *, flags32` |
| `+0x280` WaitFrameEventX | `eventType32, UINT timeoutMs, waitObjectList *, flags32, UINT64 *token` |

Microsoft's [FramePacing sample](https://github.com/microsoft/Xbox-GDK-Samples/blob/b1e77b6ac10f95b07296025fda171ca2ddc42dbe/Samples/Graphics/FramePacing/FramePacing.cpp#L2025-L2046)
names the units and uses a null output target. [Ebitengine's public binding](https://github.com/hajimehoshi/ebiten/blob/4c219926eef626a924f821547f2b7231a4572783/internal/graphicsdriver/directx/d3d12_windows.go#L1566-L1601)
supplies the argument types and names event 0 `ORIGIN`. The [Microsoft FSR3 sample](https://learn.microsoft.com/en-us/samples/microsoft/xbox-gdk-samples/amdfidelityfxfsr3frameinterpolation/)
connects the wait to presentation. None of these establishes the name or signal point of event 2.

- `2478dc2` calls `247ab10`. `247abd8` sets a null sync target, table-selected interval, computed period, flags 0. The table at `61ece60` starts with 16667, 16667, 33333, 50000, 66667 µs. A successful interval call leads to `247abfc`: `(ORIGIN, computedOffsetUs, NULL, 0)`. Failure of either skips the config copy and returns.
- The separate `2478de4` call schedules `(2,0,NULL,0)`; negative HRESULT executes `int3` at `2478dee`. `24900e3` repeats the same registration after calling the interval helper again.
- `2482405`: wait `(2,INFINITE,NULL,0,&savedToken)`. The input qword came from a 16-entry frame history and is checked nonzero. `2482448`: `(ORIGIN,0,NULL,2,&currentToken)`; `2484637`: `(ORIGIN,INFINITE,NULL,0,&localToken)`, then returns that token. At `2482710` an older token is used with device `+0x288`, mask `0x11100`, count initialized to six, and an output array (consistent with GetFrameStatisticsX).
- `2482313`–`2482338` put the current token in the first qword of each plane, followed by resource count 1 and a resource-pointer array. `24824c1` passes the plane count, plane array and parameter block to queue `+0x100` (consistent with PresentX). `24821dd` also passes the token to the existing queue submission slot `+0x118`.

The diagnostic patch returns `E_NOTIMPL` for both setters. A timer independent of presentation would not establish completion for the older frame named by the type-2 wait. No success contract for that event is claimed.

## Descriptor arena and heap ABI

1. Before the visible `+0x78` sweep, `2478e22 -> 5c5ff00` calls `UINT GetDescriptorHandleIncrementSize(This, heapType)` for type 0 at `5c5ff2f`. It multiplies two counts by the stride and rounds each extent up to 64 KiB. At `1e8649a` the allocator first calls `XMemVirtualAlloc(NULL,sumOfFiveExtents,0x42000,7,1)`; a null result fails initialization. This tuple is unsupported at `4553fc72`.
2. The success path splits the reservation using `VirtualFree(addr,size,0x8002)`, then requests fixed-base regions (`0x20406000`, flags `0x27`, protections `0x1004/0x1404/0x8404/0x2404/0x2004`), allocates physical pages (`0x280`) and maps subranges. These are actual XMem/Win32 imports; `1e86440` is not the descriptor-heap COM factory.
3. `2478e42`–`2478e9a` query types 0 through 4. Only the type-0 result is stored there. The [public increment contract](https://docs.xbox.com/reference/graphics/d3d12/interfaces/id3d12device/methods/id3d12device_getdescriptorhandleincrementsize_public) doesn't prescribe a numeric stride. The [desktop enum](https://learn.microsoft.com/en-us/windows/win32/direct3d12/creating-descriptor-heaps) uses 4 as a count, not a heap. A discarded probe is insufficient evidence for a console type-4 stride.
4. **First heap is placed:** `2478eb5 -> 5cabb00` divides the extent by the type-0 stride, then passes type 0, the arena base, descriptor count and flag 1 to `1cf3280`. The next four calls use `5a54700`, which passes a null backing pointer: `(type,count,flags)` = `(1,128,1)`, `(1,128,0)`, `(2,4096,0)`, `(3,2048,0)`.
5. `1cf32fa`: `HRESULT CreateDescriptorHeap(This, &{UINT type,count,flags,nodeMask=1}, REFIID, void **out)`, exact `IID_ID3D12DescriptorHeap = 8efb471d-616c-4f49-90f7-127bb763fa51` at `61e0628`. The backing is **not** a fifth argument or a field of that 16-byte descriptor. For a supplied address the caller installs/removes a callback around the factory; `5eb2730` returns that address on `(selector & 0x3fff)==4` and exact `count * increment` bytes, otherwise chains to the next callback.
6. Heap `+0x48` returns CPU start in RAX without a hidden return pointer. `1cf3367` compares it to the supplied address and traps on mismatch. The wrapper saves start, end and stride at `+0x20/+0x28/+0x30`; flag bit 0 enables heap `+0x50` (GPU start, also RAX). It saves GPU-minus-CPU at `+0x38`, or `0xbadadd0000000000` for non-visible heaps.
7. `55ac280` reads a free-list link **from the descriptor slot itself** using the reported stride. `164efb` calls device `+0x90` to create a view, and `247924f` reads device-object byte `+0x18`, either writing 16 bytes directly or calling `+0x88`. At the base revision the wrapper ends at `+0x18`; the read is beyond the object. A desktop CPU handle is opaque and may not even be a writable pointer. A working implementation needs the title's arena addresses and a descriptor representation translated at use; a host increment or heap pointer alone satisfies neither contract.
