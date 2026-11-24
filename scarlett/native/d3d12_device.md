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
adapters. For the first extended queue call, `+0x180` clears the output,
logs the operation and returns `E_NOTIMPL`. The retail caller checks the
HRESULT and contains `int3` at RVA `0x2478826` on failure.

`+0x180` supports the sample's default Compute request `(2,0,0,0)`.
Microsoft's [FramePacing](https://github.com/microsoft/Xbox-GDK-Samples/blob/b1e77b6ac10f95b07296025fda171ca2ddc42dbe/Samples/Graphics/FramePacing/FramePacing.cpp#L1730-L1748)
creates an ordinary Direct queue, then uses `CreateCommandQueueX` with a
zero-initialized `D3D12XBOX_COMMAND_QUEUE_DESC`, `Type=COMPUTE`, `Flags=NONE`,
`EngineOrPipeIndex=0`, `QueueIndex=0`. Its [DynamicCubeMap](https://github.com/microsoft/Xbox-GDK-Samples/blob/b1e77b6ac10f95b07296025fda171ca2ddc42dbe/Samples/Graphics/DynamicCubeMap/DeviceResources.cpp#L118-L131)
uses ordinary default Compute alongside Direct. We create a fresh host Compute
queue at normal priority for the all-zero options. Those assignments do not
establish a binary offset for each optional field or a desktop equivalent for
nonzero pipe/queue selection. Treating word 2 as desktop `Flags` or word 3
as `NodeMask` would be unjustified. All four retail inputs still fail.

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
queue signal, and checks `E_NOTIMPL` and null output at `+0x180`. The linked
variant (`-DXS_TEST_LINKED -DSCARLETT_DEVICE_BOOTSTRAP` plus `d3d12_device.c`
and `queue/queue.c`) enqueues Compute's wait before Direct's signal. A CPU
gate holds Direct, so the final Compute event must time out until the gate
opens and then complete. This tests independent command queues and explicit
dependencies, not hardware-engine affinity or physical concurrency. Run after
the public test above:

```sh
mkdir linked
"$cc" -O2 -Wall -Wextra -Werror -DXS_TEST_LINKED -DSCARLETT_DEVICE_BOOTSTRAP \
  "$src/test-d3d12-bootstrap.c" "$src/d3d12_device.c" "$src/queue/queue.c" \
  -ld3d12 -o linked/game_scarlett_master.exe
TMPDIR="$work" "$src/../test-launch-nixos.sh" "$work/linked"
grep -F 'PASS bootstrap:' "$work"/scarlett-run.*/logs/steam-999001.log
```

Both tests passed on an RTX 3070. No private test symbol is exported by the DLL.

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
