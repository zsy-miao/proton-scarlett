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
  In the success path, the indirect call at RVA `0x247870d` uses slot `0xd0/8`,
  with a stack destination in RCX and the object in RDX. The nearby indirect
  call at RVA `0x2478698` uses offset `0x188`. An IID match or similar C++
  method spelling would not establish desktop binary compatibility.
- [Rules for implementing QueryInterface](https://learn.microsoft.com/en-us/windows/win32/com/rules-for-implementing-queryinterface)
  specify COM identity; [desktop D3D12CreateDevice](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-d3d12createdevice)
  documents the host device singleton per adapter.

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
```

The executable above is the synthetic test. It checks rejection of the retail
IID at creation and at query time, COM identity and reference ownership. It also
stores private data on the desktop device, drops the desktop reference, then
reopens the device and reads the data back while only the wrapper has kept it
alive. This test passed on an NVIDIA GeForce RTX 3070 with vkd3d-proton 3.0.0.
