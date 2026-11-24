# D3D12XboxCreateDevice: device identity

The identity-only test build implements one interface: `IID_IUnknown`. With a null first argument
and non-null parameter block it creates a wrapper owning one reference to the
default adapter's vkd3d-proton device (desktop feature level 12.0). Each wrapper
has its own COM identity. `QueryInterface(IID_IUnknown)` returns the same pointer
with an additional reference; final `Release` drops the host reference.
Xbox queue sizing, feature flags, and SDK version are not interpreted.

For any other IID, including the retail `{5405c344-d457-444e-b4dd-2366e45aee39}`
and desktop `IID_ID3D12Device`, both the factory and `QueryInterface` return
`E_NOINTERFACE` and clear the output. Desktop callers can use the existing
`D3D12CreateDevice` export. A missing output returns `E_POINTER`; a non-null
adapter, missing parameter block, or missing IID at creation returns
`E_INVALIDARG`. Host creation failures propagate to the caller.

## ABI evidence

- Microsoft's [SimpleDeviceAndSwapChain, `Sample::CreateDevice`](https://github.com/microsoft/Xbox-GDK-Samples/blob/main/Samples/IntroGraphics/SimpleDeviceAndSwapChain/SimpleDeviceAndSwapChain.cpp)
  passes null, `&params`, and an IID/output pair. The source gives method names,
  not binary slot numbers or structure-return conventions.
- Static inspection of the supplied x64 executable (SHA-256
  `e95020b2fa4a721db3df59fdecf75621730ac559d8835252f6c3b658d5a08993`):
  at RVA `0x2478564` the call setup places the parameter block in RDX, IID at
  RVA `0x61e0638` in R8, output address in R9, and zero in RCX. The call at
  RVA `0x24785b7` goes through the `D3D12XboxCreateDevice` import thunk.
- [Rules for implementing QueryInterface](https://learn.microsoft.com/en-us/windows/win32/com/rules-for-implementing-queryinterface)
  specify COM identity; [desktop D3D12CreateDevice](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-d3d12createdevice)
  documents the host device singleton per adapter.

## What the initializer consumes

The function at RVA `0x2478440` tests the factory HRESULT at `0x24785c5`.
On success it copies the result from `0x75df048` to aliases including
`0x8759d00`. The latter is the device receiver in the calls below. RVAs
refer to the executable above; `+off` is a **byte** offset in the receiver's
vtable. The method names are interpretations checked against Microsoft's
[public D3D12 IDL](https://github.com/microsoft/DirectX-Headers/blob/main/include/directx/d3d12.idl).
This is a lower bound on initialization, not a complete console interface.

| Call RVA(s) | Receiver and observed behavior |
|---|---|
| `0x2478698` | **Device `+0x188`**, conditional on a configuration byte and version comparison. EDX=`0x11f`, R8D=`1`; no return value consumed. Purpose unknown. |
| `0x247870d`, `0x2478731`, `0x2478758` | **Device `+0xd0`**. RCX points to the same stack destination, RDX is the device, R8D=`1`, R9D=`1,2,3`: fits `GetCustomHeapProperties(nodeMask, DEFAULT/UPLOAD/READBACK)`. The buffer is overwritten before use; this site does **not** prove the console result layout or a required RAX value. |
| `0x247878a` | **Device `+0x40`**. Descriptor words `(0,0,0,1)` match `D3D12_COMMAND_QUEUE_DESC` (Direct, normal, no flags, node 1). R8 points to the exact `IID_ID3D12CommandQueue`; R9 is the output. Negative HRESULT calls device `+0x10` (Release shape) and returns failure. |
| `0x24787e2` → `0x5a86180` → `0x1d0b2fa` | The queue setup helper requests a companion **fence from the device** at `+0x120`: initial value `0`, flags `1` (desktop `SHARED`), exact `IID_ID3D12Fence`, stack output. It stores the fence at helper object `+0x10` and the queue at `+0`. |
| `0x2478813`, `0x2478853`, `0x2478893`, `0x24788d3` | **Device `+0x180`**, consistent with `CreateCommandQueueX`. Four queue outputs, same queue IID, distinct 16-byte inputs `(2,0,1,0)`, `(2,0,2,0)`, `(2,0,3,0)`, `(2,0,0,1)`; the second is built by duplicating a qword. The title passes each queue and a separate bookkeeping ID (1–4) to fence setup at `0x2478909`, `0x247893c`, `0x247896f`, `0x24789a2`. Its allocator loop uses Compute (`2`) for these IDs. |
| `0x2478b2d`, `0x2478c3b` | **Device `+0x48`**, exact `IID_ID3D12CommandAllocator` and types Direct (`0`) / Compute (`2`). The resulting allocator goes into `+0x248`, below. The helper reached from `0x2479345` and `0x2479366` also uses this allocator slot (`0x161d0a0`). |
| `0x2478b71`, `0x2478c70` | **Device `+0x248`**. RDX points to four words `(0,2,1,0)` or `(2,2,1,0)`, R8=allocator, R9=null, stack=IID and output. The requested list IID `c3827890-4024-474c-98cf-645b51dc4874` differs from desktop `IID_ID3D12GraphicsCommandList6`. The **returned list**, not the device, immediately receives `+0x420`, `+0x418`, then `+0x48` (Close shape, HRESULT checked). |
| `0x2478de4`, `0x2478e42`–`0x2478e9a` | **Device `+0x278`** takes `(2,0,0,0)` and its HRESULT gates progress. **Device `+0x78`** fits `GetDescriptorHandleIncrementSize`: arguments `0..4`; nested call at `0x5c5ff2f` multiplies descriptor counts by the result. No mapping assumed for heap type 4. |
| `0x247924f`, `0x247927a` | Reads a byte at **device object `+0x18`**, bypassing the vtable. If zero the caller writes two literal qwords into descriptor storage; otherwise it calls device `+0x88`. A correctly ordered vtable alone cannot satisfy this ABI. |
| `0x247954e`–`0x2479646` | Five calls to **device `+0x148`** match `CreateCommandSignature`: stride/count/argument-array descriptor, null root signature, exact `IID_ID3D12CommandSignature`, stack output. Negative HRESULTs go to failure. The initializer sets its success byte at `0x247967a`. |

The identity-only build has two private host operations: the three node-1 heap
queries and the ordinary Direct queue descriptor. They use the real device
owned by the wrapper. The heap helper writes a **desktop** `D3D12_HEAP_PROPERTIES`
and the queue helper returns an owned **desktop** queue internally. Neither
is installed in a console vtable or invoked for an unsupported factory IID.

## Installed bootstrap (`SCARLETT_DEVICE_BOOTSTRAP`)

This build accepts the retail device IID only with the observed 80-byte
parameter shape (version `0x20004`). It validates that shape; it does not
claim to implement console scratch/ring allocation or interpret arbitrary
GDK flags. The device owns a real vkd3d device. Its bootstrap prefix serves
node-1 `DEFAULT`, `UPLOAD`, `READBACK` heap properties using destination in
RCX and device in RDX; the return pointer is the destination. It creates
the exact Direct queue and zero-valued shared fence using separate native
adapters. The previous build returned `E_NOTIMPL` for the first extended
queue and the retail caller trapped at `0x2478826` on RADV.

`+0x180` supports the sample's default Compute request `(2,0,0,0)`.
Microsoft's [FramePacing](https://github.com/microsoft/Xbox-GDK-Samples/blob/b1e77b6ac10f95b07296025fda171ca2ddc42dbe/Samples/Graphics/FramePacing/FramePacing.cpp#L1730-L1748)
creates an ordinary Direct queue, then uses `CreateCommandQueueX` with a
zero-initialized `D3D12XBOX_COMMAND_QUEUE_DESC`, `Type=COMPUTE`, `Flags=NONE`,
`EngineOrPipeIndex=0`, `QueueIndex=0`. Its [DynamicCubeMap](https://github.com/microsoft/Xbox-GDK-Samples/blob/b1e77b6ac10f95b07296025fda171ca2ddc42dbe/Samples/Graphics/DynamicCubeMap/DeviceResources.cpp#L118-L131)
uses ordinary default Compute alongside Direct. We create a fresh host Compute
queue at normal priority for the all-zero options. Those assignments do not
establish a binary offset for each optional field or a desktop equivalent for
nonzero pipe/queue selection. Treating word 2 as desktop `Flags` or word 3
as `NodeMask` would be unjustified.

For the four observed indexed descriptors in the table, each successful call
creates a distinct, normal-priority host Compute queue on node 1. This
preserves stream identity, FIFO submissions and explicit fence dependencies.
The host schedules each queue. Other indices, combinations and flags return
`E_NOTIMPL` with null output.

**Why this meets the observed scheduling requirement.** The initializer
stores four queue pointers at `0x75e7038 + 8*i`, then passes each pointer and
its ID (1–4) to `0x5a86180`. Each gets a separate fence and submitted counter;
the submission worker reads the queue, fence and 64-bit value to issue
`queue[+0x80]` (see the [queue evidence](queue/README.md)). None of those
operations observes a hardware pipe ID. AMD's [RDNA Performance Guide](https://gpuopen.com/learn/rdna-performance-guide/#synchronization)
describes ordered command buffers and submission-boundary cross-queue fences.
Its [MES architecture specification](https://gpuopen.com/download/micro_engine_scheduler.pdf),
“HW architecture overview” and “Scheduling algorithm”, distinguishes application
queues from the physical queues to which the scheduler maps them. Separate pipes
can improve overlap and latency; neither source establishes that Xbox placement
is only a hint. There is no claim about physical affinity, guaranteed concurrent
shader execution, priorities, or forward progress of shaders spinning on another
dispatch. Those require separate evidence.

Pinned vkd3d-proton [assigns each virtual queue to a host queue](https://github.com/HansKristian-Work/vkd3d-proton/blob/212991fc2c266bc0d59f4c4ce8f80f7126508d71/libs/vkd3d/command.c#L3034-L3045).
For [shared-fence waits](https://github.com/HansKristian-Work/vkd3d-proton/blob/212991fc2c266bc0d59f4c4ce8f80f7126508d71/libs/vkd3d/command.c#L22749-L22760)
the submitting worker waits before consuming the physical queue, so another
logical queue can submit the GPU signal even on a shared physical queue.
This does not infer an Xbox vtable slot for Wait, submission or cache control.

### Occlusion query heaps

At RVA `0x2481b2b` (return `0x2481b31`), device `+0x138` takes the three-dword
`D3D12_QUERY_HEAP_DESC`, IID `0d9658ae-ed45-469e-a61d-970ec583cab4`
(`ID3D12QueryHeap`, RVA `0x61e06f8`), and output at object `+0x58`. The four
observed descriptors are `(OCCLUSION,2460,1)`. Each wrapper owns one distinct
native query heap; only IUnknown identity methods are exposed. Other descriptors
are not enabled. In particular, title kind 2 takes a separate allocation path,
and kind 4 can reach this slot with `PIPELINE_STATISTICS` rather than occlusion.

After the HRESULT handler at `0x2481b35`, success calls device `+0xd8` at
`0x2481bce` (return `0x2481bd5`), matching `CreateCommittedResource`: READBACK
heap, masks 1/1, no flags, row-major buffer, COPY_DEST (`0x400`), no clear value,
`IID_ID3D12Resource`, output at object `+0x60`. Width is count times result
stride (2460 × 8 = 19680 bytes for the traced requests). On failure the title
releases the query heap through `+0x10`.

To run the synthetic occlusion test from the repository root on RADV:

```sh
w=$(mktemp -d); mkdir "$w/game"
x86_64-w64-mingw32-gcc -std=c11 -Os -Wall -Wextra -Werror \
  -DSCARLETT_DEVICE_BOOTSTRAP -DSCARLETT_STAGED_PLACED \
  scarlett/native/{test-query-heap,d3d12_device}.c -ld3d12 -ld3dcompiler \
  -o "$w/game/game_scarlett_master.exe"
TMPDIR="$w" SCARLETT_TEST_GPU=radv scarlett/test-launch-nixos.sh "$w/game"
grep -E '^visible samples|^PASS ' "$w"/scarlett-run.*/logs/steam-999001.log
```

The fixture releases the console device, draws four scissored triangles using
index 2459 in four heaps, then resolves all four through a native command list.
After the fence, it checks 64, 48, 32 and 16 samples, unchanged guards, and
final object release. This checks native backing; it does not infer any
console query recording or resolve slot.

### Allocator and list boundary

Device `+0x48` creates a real Direct or Compute host allocator behind a
separate console object. It accepts `{6102dee4-af59-4b09-b999-b44d73f09b24}`;
QI for that IID or `IUnknown` retains the same identity. At `0x248bc9d`
the title loads the first allocator, calls `+0x10` and clears its pointer.
The pooled path at `0x161d0a0` stores its allocator in word zero of a
48-byte entry; `0x4e9b575` takes this pointer, calls `+0x40` with only
`this`, and passes EAX to the HRESULT handler. This matches
[allocator Reset](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12commandallocator-reset).
Fence retirement before reset remains the caller's responsibility. Separate
calls at `0x1884fe` and `0x2474b5e` use allocator `+0x48` with a function
address and a context; they are not Reset. The adapter guards this slot.

The `+0x248` constructor takes `(device, desc, allocator, initialState,
riid, out)`; the last two arguments are on the stack. The first descriptions
are `(0,2,1,0)` and `(2,2,1,0)` with null initial state. The method checks
the retail list IID, descriptor, allocator type/device and host enhanced
barrier support before creating a recording list with its own COM identity.
A separate helper
at `0x17f6d34` uses `(type,6,1,0)`, requests a different list IID
`{5b160d0f-ac1b-4185-8ba8-b3ae42a5a455}`, and closes the result at
`0x17f6d48`. Neither description establishes the semantics of words 1–3.

**Pooled generation.** Its IID matches desktop `IID_ID3D12GraphicsCommandList`
byte-for-byte, but the pooled Reset slot and inline packet ABI are not the
desktop interface. This IID accepts only `(Direct|Compute,6,1,0)`,
null initial state and a matching allocator. The title constructs a pool of
512 Direct entries (`2478d11`) and 64 Compute entries (`2478d6f`).
`17f6ba0` creates one allocator per pool; its loop passes that allocator to
the constructor, immediately closes each recording list, stores the pointer
in a 16-byte title-owned entry, and releases the seed allocator at `17f6c65`.
Thus each list owns a separate real recorder and a reference to its current
allocator. Checkout (`2474c37`–`2474cce`) resets the entry's list with a new
allocator and two zeros. Teardown (`247aae0`) releases each entry's pointer.
The pooled vtable exposes IUnknown, Close `+0x48`, and Reset `+0x458` only;
it does not advertise the startup IID or its synchronization slots.

The pool entry is **not** the list's object layout. On Compute checkout the
list pointer is installed at `context+0x12348`; `248c705` and `248c712`
write vectors at **list** `+0x544` and `+0x554`, and `248c71a` compares a
cursor at `+0x18` against a limit at `+0x30`. Those recording operations
need a packet translator. The adapter ends its accessible storage at the
vtable pointer and reserves an inaccessible page immediately after it:
these observed inline accesses fault rather than modifying internal state
or emitting untranslated packets. The linked test submits a checked-out,
closed list and waits for GPU retirement before allocator reset; both PE
tests exercise full-sized pool construction, COM identity and teardown.

#### What the two pre-Close calls consume

All four initialization call sites reload RCX from the list output at
`[rsp+0x60]`. Values below are hex, at the **caller's** stack pointer.
Writing EDX/R8D zero-extends them; this doesn't by itself establish the
declared parameter widths. The final argument is a qword.

| List and call RVA | Slot | RDX | R8 | R9 | `[rsp+0x20]` |
|---|---:|---:|---:|---:|---:|
| Direct `2478b9e` | `420` | `700000` | `0` | `0` | `0000ffffffffff00` |
| Direct `2478bbf` | `418` | `c3c0` | `80020000` | `0` | `0000ffffffffff00` |
| Compute `2478c93` | `420` | `100000` | `0` | `0` | `0000ffffffffff00` |
| Compute `2478cb4` | `418` | `c380` | `20000` | `0` | `0000ffffffffff00` |

No output buffer is supplied and neither return register is consumed. In
contrast, the immediately following `+0x48` call has its HRESULT tested.
The lists are saved at RVA `75e7300 + index*8`; `2482660` later takes the
address of an entry and issues `queue[+0x118](queue,1,&entry,...,0)`.
The submission worker reaches that helper at `24821b4` and `2482493`.
Effects recorded here may therefore matter when the list is submitted,
even though no rendering commands intervene between creation and Close.

One independent renderer path (`1560960`) takes the current list from
`context+0x127c8`, acquiring it via `188430` if necessary. At `1560994` it
calls `+0x418` with `(c280,20000,0,0000ffffffffff00)`. Its caller at `1560942`
then returns the object saved in RDI and sets a context byte; it does not
return or store a value obtained from `+0x418`. This is the same argument
pattern as initialization on another list path, not proof of identical IIDs.
Filtering by receiver matters: `24745e4` uses `+0x420` on the **device**
(`75df048`), while `437cfed`/`437d01a` pass stack addresses in RDX and read
back dwords. Neither is evidence for list getters.

**Interpretation:** the strongest public match is a pair of range-based GPU
synchronization commands. Microsoft's [Series API directory](https://docs.xbox.com/nda/nda-topic-directory)
names `SyncBottomOfPipeX`, `SyncTopOfPipeX`, `D3D12XBOX_SYNC_FLAGS` and
`D3D12XBOX_SYNC_CONTROL_FLAGS`. Its public
[DirectStorageTextureShuffling sample](https://github.com/microsoft/Xbox-GDK-Samples/blob/b1e77b6ac10f95b07296025fda171ca2ddc42dbe/Samples/Graphics/DirectStorageTextureShuffling/DirectStorageTextureShuffling.cpp#L198-L203)
records a cache flush using top/bottom masks, null GPU VA and
`D3D12XBOX_FLUSH_RANGE_ALL` before Close. That sample uses **three**
explicit arguments to `FlushPipelineX`, not these four; it supplies a
semantic analogy, not a slot map. The two integers, zero and large aligned
constant look like masks plus an address/range. No call passes a GUID, a
data buffer or a size pointer as the public private-data APIs would require.

The adapter recognizes only the four rows above, for their respective list
types. Each records an enhanced **global** `ALL/COMMON -> ALL/COMMON` barrier.
The [enhanced barriers spec](https://microsoft.github.io/DirectX-Specs/d3d/D3D12EnhancedBarriers.html)
defines COMMON-before as including all writes and COMMON-after as making data
available for all layout-compatible accesses. Pinned [vkd3d-proton](https://github.com/HansKristian-Work/vkd3d-proton/blob/212991fc2c266bc0d59f4c4ce8f80f7126508d71/libs/vkd3d/command.c#L20426-L20477)
maps ALL to Vulkan `ALL_COMMANDS` and COMMON to `MEMORY_READ|MEMORY_WRITE`
on both sides of the dependency. This intentionally over-synchronizes
same-queue work. It does not transition texture layouts, establish cross-queue
ordering, or establish the meaning of individual Xbox control bits. A new
range, mask or control poisons recording: Close reports `E_NOTIMPL`; the
queue refuses submission. There is no weaker fallback if OPTIONS12 is false.

Submission supports `+0x118` only for one closed list on a matching queue,
zero token and zero flags. RVA `248266e` actually loads the token from
`75e0e88`; it is not necessarily zero. Nonzero tokens, the batch path's flags
`3`, and unknown slots stop explicitly. `+0x458` follows the null-state,
zero-extra-argument reset shape at `2474cce`; a reset retains the replacement
allocator only after host Reset succeeds. Recycling a submitted allocator
still requires retirement on its queue.

The prior trace stops at `0x19f4f58` with ECX=`0x80004002`, before any
success-path calls. `+0x188` **is not needed with this runtime/prefix**:
`0x24785cd` first checks the title configuration byte. Even when set, the
helper at `0x55dea0c` queries the runtime analytics IID and the caller
compares the four hosting-OS words at offset 8 to `10.0.26100.9628`.
The failed prefix records Windows build 19045. A separate synthetic PE
initialized the installed runtime, queried that same IID, and read
`10.0.19045.0` from `hosting_os`. This rules out the version branch regardless
of the configuration byte; it is not an observation of a successful retail
run. Unknown slots within the traced prefixes, including `+0x188`, fail fast
instead of returning a guessed value. Neither their semantics nor the later
inline console object layout is implemented.

Scratch build and public export test (from the repository root):

Retail passed the device factory call, then requested 64 KiB through
`XMemVirtualAlloc(NULL, 65536, 0x20401000, 0x283, 0x42404)` at `+0x24786b9`.
The null result triggered `+0x24786d1`, before the heap and queue calls.

```sh
src="$PWD/scarlett/native"; work=$(mktemp -d); mkdir "$work/public"; cd "$work"
cc=x86_64-w64-mingw32-gcc
"$cc" -Os -s -Wall -Wextra -Werror -DSCARLETT_DEVICE_BOOTSTRAP -shared \
  -Wl,--no-insert-timestamp,--out-implib,candidate.a "$src/d3d12_device.c" \
  "$src/queue/queue.c" "$src/d3d12_vm.c" "$src/d3d12_xs.def" -ld3d12 -o public/d3d12_xs.dll
"$cc" -O2 -Wall -Wextra -Werror "$src/test-d3d12-bootstrap.c" -ld3d12 candidate.a \
  -o public/game_scarlett_master.exe
TMPDIR="$work" "$src/../test-launch-nixos.sh" "$work/public"
```

`test-d3d12-bootstrap.c` independently dispatches by slot, compares each
heap result to the desktop device with surrounding canaries, submits a real
queue signal, and checks `E_NOTIMPL` and null output for unknown selectors at `+0x180`. The linked
variant (`-DXS_TEST_LINKED -DSCARLETT_DEVICE_BOOTSTRAP` plus `d3d12_device.c`
and `queue/queue.c`) enqueues Compute's wait before Direct's signal. A CPU
gate holds Direct, so the final Compute event must time out until the gate
opens and then complete. This tests independent command queues and explicit
dependencies. It also rotates each indexed Compute queue as the sole runnable
queue: its GPU signal retires while the other three signals remain pending,
then its second GPU signal opens their gate. The four rounds use fence
values above `2^40` with identical low words. The public export test checks
distinct COM identities and rejects unsupported selector combinations. Run after
the public test above:

```sh
mkdir linked
"$cc" -O2 -Wall -Wextra -Werror -DXS_TEST_LINKED -DSCARLETT_DEVICE_BOOTSTRAP \
  "$src/test-d3d12-bootstrap.c" "$src/d3d12_device.c" "$src/queue/queue.c" \
  -ld3d12 -o linked/game_scarlett_master.exe
TMPDIR="$work" "$src/../test-launch-nixos.sh" "$work/linked"
grep -F 'PASS bootstrap:' "$work"/scarlett-run.*/logs/steam-999001.log
```

Both tests passed on an RTX 3070 (595.112.7) and AMD `1002:164e` (RADV
26.3.0). No private test symbol is exported by the DLL.
The linked test copies 4 KiB of distinct words to a default buffer on Direct,
default Compute and each indexed Compute queue. It records the exact startup
barriers and a dependent copy from that buffer to readback on the wrapped
list. After submission behind a gate it observes timeout, releases the gate,
waits for completion, checks all 1024 words, and resets the allocator and list.
An unknown barrier then fails at Close. Run with `SCARLETT_TEST_GPU=radv` or
`nvidia`. This validates ordered result and queue progress, not every bit's
Xbox microarchitectural behavior.

## Scratch GPU test

From the repository root, with the NixOS test host and installed compatibility
tool used by `scarlett/test-launch-nixos.sh`:

```sh
src="$PWD/scarlett/native"
work=$(mktemp -d)
mkdir "$work/game"
cd "$work"
cc=x86_64-w64-mingw32-gcc
"$cc" -O2 -Wall -Wextra -Werror -shared "$src/d3d12_device.c" \
  "$src/d3d12_xs.def" -ld3d12 -Wl,--out-implib,device.a -o game/d3d12_xs.dll
"$cc" -O2 -Wall -Wextra -Werror "$src/test-d3d12-device.c" \
  -ld3d12 device.a -o game/game_scarlett_master.exe
TMPDIR="$work" "$src/../test-launch-nixos.sh" "$work/game"
grep -F 'PASS Xbox device:' "$work"/scarlett-run.*/logs/steam-999001.log

# Compile the private helpers into the synthetic PE; no test hooks in the DLL.
mkdir private
"$cc" -O2 -Wall -Wextra -Werror -DSCARLETT_DEVICE_PRIVATE_TEST \
  "$src/test-d3d12-device.c" -ld3d12 -o private/game_scarlett_master.exe
TMPDIR="$work" "$src/../test-launch-nixos.sh" "$work/private"
grep -F 'PASS private device init:' "$work"/scarlett-run.*/logs/steam-999001.log
```

The executable above is the synthetic test. It checks rejection of the retail
IID at creation and at query time, COM identity and reference ownership. It also
stores private data on the desktop device, drops the desktop reference, then
reopens the device and reads the data back while only the wrapper has kept it
alive. The private test also checks the heap query results and submits a
Direct queue signal to a shared fence, waiting for its event and reading back
the completed value. Both tests passed on a GeForce RTX 3070, vkd3d-proton 3.0.0.
