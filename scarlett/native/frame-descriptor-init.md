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

The scheduler accepts a null output, 16667/33333 µs, period 1–3, no interval flags.
Origins use a monotonic host QPC time grid with bounded offsets; late callers skip
missed ticks. They are not claimed to be phase-locked to a physical display.
`period` sets the frame's target time (`origin + period * interval`), not the
distance between successive origins. Registration is per-device. Reconfiguration
invalidates history and wakes old waiters. The most recent 16 tokens are kept.
The observed origin poll `(0,0,NULL,2)` is nonblocking; if no origin is due it
returns `DXGI_ERROR_WAIT_TIMEOUT`, rather than fabricating a second token.

**Type 2 policy:** the public name and exact Xbox signal point remain unknown.
For `(2,0,NULL,0)` registration succeeds; `(2,timeout,NULL,0,&token)` can
complete only for a known, individually presented token. `xs_frame_present`
is a private presentation consumer: it actually calls `Present(1,0)` on the
caller's serialized DXGI swapchain, associates the incremented present count,
and the waiter requires that exact `GetFrameStatistics().PresentCount`.
Failure, occlusion, unknown tokens and disjoint stats do not become success.
This indicates host presentation feedback, not proof of physical scanout.
Pinned [vkd3d-proton](https://github.com/HansKristian-Work/vkd3d-proton/blob/212991fc2c266bc0d59f4c4ce8f80f7126508d71/libs/vkd3d/swapchain.c#L2988-L3016)
updates statistics in its presentation worker. Without a consumer, finite
type-2 waits time out and infinite waits stay pending until reconfiguration.
The retail queue's `PresentX` and nonzero submission tokens still need integration;
this patch does not invent display surfaces from console resources.

Reproduce the consumer test from the repo root:

```sh
s="$PWD/scarlett/native"; w=$(mktemp -d); mkdir "$w/game"
x86_64-w64-mingw32-gcc -O2 -Wall -Wextra -Werror -DSCARLETT_DEVICE_BOOTSTRAP \
  "$s/test-frame-clock.c" "$s/d3d12_device.c" "$s/queue/queue.c" -ld3d12 -ldxgi \
  -o "$w/game/game_scarlett_master.exe"
SCARLETT_TEST_GPU=nvidia TMPDIR="$w" "$s/../test-launch-nixos.sh" "$w/game"
grep 'PASS frame clock' "$w"/scarlett-run.*/logs/steam-999001.log
```

## Descriptor arena and heap ABI

1. Before the visible `+0x78` sweep, `2478e22 -> 5c5ff00` calls `UINT GetDescriptorHandleIncrementSize(This, heapType)` for type 0 at `5c5ff2f`. It multiplies two counts by the stride and rounds each extent up to 64 KiB. At `1e8649a` the allocator first calls `XMemVirtualAlloc(NULL,sumOfFiveExtents,0x42000,7,1)`; a null result fails initialization. This tuple is unsupported at `4553fc72`.
2. The success path splits the reservation using `VirtualFree(addr,size,0x8002)`, then requests fixed-base regions (`0x20406000`, flags `0x27`, protections `0x1004/0x1404/0x8404/0x2404/0x2004`), allocates physical pages (`0x280`) and maps subranges. These are actual XMem/Win32 imports; `1e86440` is not the descriptor-heap COM factory.
3. `2478e42`–`2478e9a` query types 0 through 4. Only the type-0 result is stored there. The [public increment contract](https://docs.xbox.com/reference/graphics/d3d12/interfaces/id3d12device/methods/id3d12device_getdescriptorhandleincrementsize_public) doesn't prescribe a numeric stride. The [desktop enum](https://learn.microsoft.com/en-us/windows/win32/direct3d12/creating-descriptor-heaps) uses 4 as a count, not a heap. A discarded probe is insufficient evidence for a console type-4 stride.
4. **First heap is placed:** `2478eb5 -> 5cabb00` divides the extent by the type-0 stride, then passes type 0, the arena base, descriptor count and flag 1 to `1cf3280`. The next four calls use `5a54700`, which passes a null backing pointer: `(type,count,flags)` = `(1,128,1)`, `(1,128,0)`, `(2,4096,0)`, `(3,2048,0)`.
5. `1cf32fa`: `HRESULT CreateDescriptorHeap(This, &{UINT type,count,flags,nodeMask=1}, REFIID, void **out)`, exact `IID_ID3D12DescriptorHeap = 8efb471d-616c-4f49-90f7-127bb763fa51` at `61e0628`. The backing is **not** a fifth argument or a field of that 16-byte descriptor. For a supplied address the caller installs/removes a callback around the factory; `5eb2730` returns that address on `(selector & 0x3fff)==4` and exact `count * increment` bytes, otherwise chains to the next callback.
6. Heap `+0x48` returns CPU start in RAX without a hidden return pointer. `1cf3367` compares it to the supplied address and traps on mismatch. The wrapper saves start, end and stride at `+0x20/+0x28/+0x30`; flag bit 0 enables heap `+0x50` (GPU start, also RAX). It saves GPU-minus-CPU at `+0x38`, or `0xbadadd0000000000` for non-visible heaps.
7. `55ac280` reads a free-list link **from the descriptor slot itself** using the reported stride. `164efb` calls device `+0x90` to create a view, and `247924f` reads device-object byte `+0x18`, either writing 16 bytes directly or calling `+0x88`. At the base revision the wrapper ends at `+0x18`; the read is beyond the object. A desktop CPU handle is opaque and may not even be a writable pointer. A working implementation needs the title's arena addresses and a descriptor representation translated at use; a host increment or heap pointer alone satisfies neither contract.

### Heap creation milestone

The EXE's `__XMemAllocationHooks__` is an `(alloc,free)` pair at `6844380`.
Allocation at `4deb430` selects `table[attributes[23:16]]` (`6843370`);
the placement installer writes the list head at `6843770`, hence allocator ID
`0x80`. It snapshots applicable callbacks for the current thread, then invokes
`callback(size,attributes,context,next)` at `4deb4eb`. The corresponding
free dispatcher uses table `6843b80`, head `6843f80`. At `248b525` the
caller installs a free interceptor around final release. `248b550` swallows
the exact placement pointer, otherwise tail-calls the next callback. Thus
type 0 uses `XMemAlloc(bytes,0x800004)` synchronously and `XMemFree` with
the same attributes on final release. No need to inspect the EXE's TLS or
call a hard-coded title address. The temporary callback context is never
retained. A title that hands this heap to a deferred GPU consumer must still
keep the arena alive and release the final COM reference under its free hook.

The other four shapes own private committed title memory. Each wrapper owns
a real host heap with the requested count and visibility. The shadow ABI uses
32/16/32/32-byte slots for types 0–3: these are adapter-selected increments,
not recovered Xbox hardware sizes. The title computes offsets from the
returned increment at `156c251`; it stores a DWORD link in slot byte zero
(`555e3b1`, sampler `1f76f88`, RTV `1f77023`), and writes qwords at byte
zero and eight (`2479269`, `247926c`). The cache-flush loop at `4ecc83b`
uses the queried stride, rounding down to the cache line. These establish
some accesses but not a complete descriptor encoding. Type 4 is queried
then discarded (`2478e9a`); neither this site nor the public [console enum](https://docs.xbox.com/reference/graphics/d3d12/enums/d3d12_descriptor_heap_type_public)
identifies a fifth usable heap. It gets increment zero and no factory support.

Both returned start handles are in the title address domain; invisible GPU
start is zero. Given a **live heap reference**, private `xs_descriptor_slot`
checks alignment, bounds and visibility and scales the index by the actual
host stride. Only the resulting private handles may be passed to D3D12;
they are never dereferenced by the title. The bootstrap device's byte at
`+0x18` is explicitly allocated and zero. See the writer subset below.

`test-descriptor.cpp` models the alloc/free interceptors using a real adjacent
`xmem.dll`. It creates the five shapes, checks placement and release, writes
both qwords in the last slot and checks bounds. It renders a red pixel via
the translated final RTV handle and checks the copied RGBA8 bytes after the
retirement fence. Both RADV `1002:164e` and RTX 3070 passed. The test uses a
committed placement region, not the retail arena's separately mapped pages.

From `scarlett/native` (with an installed matching tool):

```sh
src=$PWD; w=$(mktemp -d); mkdir "$w/game"; cd "$w"
for f in d3d12_device queue/queue; do
  x86_64-w64-mingw32-gcc -DSCARLETT_DEVICE_BOOTSTRAP -Os -Wall -Wextra -Werror \
    -c "$src/$f.c" -o "${f##*/}.o"
done
x86_64-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -Werror -static \
  "$src/test-descriptor.cpp" d3d12_device.o queue.o -ld3d12 -o game/game_scarlett_master.exe
cp "$tool/scarlett/xmem.dll" game/
dxc -T cs_6_0 -E main -Fo game/test-view.dxil "$src/test-view.hlsl"
SCARLETT_TEST_GPU=radv TMPDIR="$w" "$src/../test-launch-nixos.sh" "$w/game" "$tool"
```

### Descriptor writers (RVA evidence, native x64)

| Slot | Evidence and accepted input |
|---|---|
| Device `+0x90` SRV | `164efb`: `(This, resource, desc, titleCPUHandle)`. Five calls `247905a`–`24790b5` use a null resource and a 40-byte input: DWORDs at `+0,+4,+8` are format `42` (`R32_UINT`), dimension `2,4,9,8,1`, mapping `0x1688`. Remaining bytes are zero. These are 1D, 2D, cube, 3D and buffer null views. |
| Device `+0x98` UAV | `1647610`: `(This, resource, counterResource, desc, titleCPUHandle)` with handle on stack. `24791cf`–`2479224` pass both resources null, format `42`, dimensions `2,4,5,8,1`, remaining 32 bytes zero. Dimension 5 is 2D array. |
| Device `+0x88` CBV / inline | `2479244` zeroes the 16-byte description, `247924f` tests device byte `+0x18`. If set, `247927a` passes `(This, &zeroDesc, titleCPUHandle)`; if clear, `2479269`/`247926c` write qwords `0x0010000000000000`, `0x0002403c00000000` directly. The slot writer and the bound-heap resolver both recognize this exact null CBV. This doesn't establish other inline formats. |
| Non-null raw buffer SRV | `5bdb96c`–`5bdb9c2`: format `39` (`R32_TYPELESS`), dimension 1, mapping `0x1688`, first element `UINT64` at `+16`, count `UINT32` at `+24`, stride zero at `+28`, RAW=1 at `+32`. `5bdba95` loads `resourceOwner[+0x20]` as the **interface pointer** and calls the SRV helper. Padding at offsets 12 and 36 is ignored. Only an explicitly bound same-device host buffer, a nonzero count, and an in-bounds range in four-byte elements are accepted. |

Ownership: each device maintains a lock-protected list of its live type-0
heaps. The writer translates only an aligned, in-bounds destination in
exactly one such heap. Final heap release removes it before dropping backing.
A write at a reused VA addresses the new heap; a stale raw VA cannot select
the old allocation. Callers must keep the heap and their title allocation
alive and synchronize descriptor updates with GPU retirement. Unsupported
void-slot inputs stop; the typed backend returns an HRESULT.

API writes leave a checked slot stamp (serial and kind) in title storage
alongside a separate owned descriptor record. No host pointer is stored
there. Resolving for GPU use checks the stamp and expected kind on the
specific bound heap, or recognizes the exact inline CBV above. This rejects
free-list overwrites, stale stamps after exact-address reuse, and copied
stamps in other slots. Arbitrary raw hardware encodings and bulk descriptor
copies require their own implementation. Command-list bindings must use
the checked resolver rather than the address-only heap arithmetic helper.

`xs_bind_view_buffer` is private: an owning factory registers an interface
identity and a real host buffer, removing the registration before recycling
the identity. The synthetic test supplies this producer; a retail resource
factory has not yet been identified/connected. A recognized write retains
the host buffer in the slot until overwrite or heap release. Foreign COM
pointers are never queried or assumed to be desktop resources. The distinct
`4ecc800` route and dimension 11 at `56ac05c` are not decoded.

`test-view.hlsl` reads from four descriptors after the synthetic resource
owner is unregistered and its buffer reference dropped. Fenced readback
must contain the word from the selected raw-buffer offset, plus zeros from
the null buffer SRV, inline CBV, and null UAV after a write. The fixture also
tests all ten startup SRV/UAV shapes, wrong-kind/misaligned/overrun/foreign
inputs, free-list overwrite, and exact-VA heap recreation. It uses a host
compute command list to isolate descriptor correctness. No retail execution.
