# D3D12XboxCreateDevice: device identity

The factory implements one interface: `IID_IUnknown`. With a null first argument
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
| `0x2478813`, `0x2478853`, `0x2478893`, `0x24788d3` | **Device `+0x180`**. Four more queue outputs, same queue IID, distinct 16-byte inputs `(2,0,1,0)`, `(2,0,2,0)`, `(2,0,3,0)`, `(2,0,0,1)`; the second is built by duplicating a qword. Each queue goes through the fence setup above. The method and descriptor semantics are not established by the shared IID. |
| `0x2478b2d`, `0x2478c3b` | **Device `+0x48`**, exact `IID_ID3D12CommandAllocator` and types Direct (`0`) / Compute (`2`). The resulting allocator goes into `+0x248`, below. The helper reached from `0x2479345` and `0x2479366` also uses this allocator slot (`0x161d0a0`). |
| `0x2478b71`, `0x2478c70` | **Device `+0x248`**. RDX points to four words `(0,2,1,0)` or `(2,2,1,0)`, R8=allocator, R9=null, stack=IID and output. The requested list IID `c3827890-4024-474c-98cf-645b51dc4874` differs from desktop `IID_ID3D12GraphicsCommandList6`. The **returned list**, not the device, immediately receives `+0x420`, `+0x418`, then `+0x48` (Close shape, HRESULT checked). |
| `0x2478de4`, `0x2478e42`–`0x2478e9a` | **Device `+0x278`** takes `(2,0,0,0)` and its HRESULT gates progress. **Device `+0x78`** fits `GetDescriptorHandleIncrementSize`: arguments `0..4`; nested call at `0x5c5ff2f` multiplies descriptor counts by the result. No mapping assumed for heap type 4. |
| `0x247924f`, `0x247927a` | Reads a byte at **device object `+0x18`**, bypassing the vtable. If zero the caller writes two literal qwords into descriptor storage; otherwise it calls device `+0x88`. A correctly ordered vtable alone cannot satisfy this ABI. |
| `0x247954e`–`0x2479646` | Five calls to **device `+0x148`** match `CreateCommandSignature`: stride/count/argument-array descriptor, null root signature, exact `IID_ID3D12CommandSignature`, stack output. Negative HRESULTs go to failure. The initializer sets its success byte at `0x247967a`. |

Only two private host operations are implemented here: the three node-1 heap
queries and the ordinary Direct queue descriptor. They use the real device
owned by the wrapper. The heap helper writes a **desktop** `D3D12_HEAP_PROPERTIES`
and the queue helper returns an owned **desktop** queue internally. Neither
is installed in a console vtable or invoked for an unsupported factory IID.

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
