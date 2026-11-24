# Device `+0x148`: indirect command signatures

The five calls pass the device in RCX, the descriptor in RDX, a null root
signature in R8, `IID_ID3D12CommandSignature` (`c36a797c-ec80-4f0a-8985-a7b2475082d1`)
in R9, and the output address on the stack. From RVA `2479466` onward the
caller builds `ByteStride` and `NumArgumentDescs` as the low/high dwords of
a qword. Each count is 1, each argument pointer is at offset 8, and each
`NodeMask` at offset 16 is zero. There is no observed flags field. Padding
and inactive argument union fields need not have been initialized.

| Call RVA | Output RVA | Stride | Argument type | Host policy |
|---|---|---:|---:|---|
| `247954e` | `75e75d8` | 16 | 0 | Draw |
| `247958a` | `75e75e0` | 20 | 1 | DrawIndexed |
| `24795c6` | `75e75e8` | 12 | `0x69` | `E_NOTIMPL`, null output |
| `2479605` | `75e75f0` | 12 | 2 | Dispatch |
| `2479646` | `75e75f8` | 12 | 10 | DispatchMesh, subject to OPTIONS7 mesh tier and host creation |

The third argument descriptor starts with a **qword** `0x69`, hence a
type of 105 and the next dword zero, not two arguments. At `248cdef` the
title passes this signature to list `+0x1e8`, with command count 1 and a
buffer offset; ordinary Dispatch uses the same method at `248cbba`. This
does not establish that the two signatures consume the same indirect
command. The initializer returns failure on the third creation HRESULT;
the fourth and fifth calls are tested independently.

The [Microsoft DirectX IDL](https://github.com/microsoft/DirectX-Headers/blob/main/include/directx/d3d12.idl)
defines the four standard enum values and descriptor layout.
[ComputeParticles](https://github.com/microsoft/Xbox-GDK-Samples/blob/b1e77b6ac10f95b07296025fda171ca2ddc42dbe/Samples/Graphics/ComputeParticles/ParticleSystem.cpp#L224-L236)
uses the same single Draw/16-byte/null-root shape.
[Mesh shader API design](https://docs.xbox.com/build/core-features/graphics/newfeatures/ms/ms-api-design)
describes indirect mesh dispatch of three group counts. None of these
defines a translation for `0x69`. The adapter copies only the active type,
creates an owned host signature, and exposes its own controlling `IUnknown`.
The title releases all five outputs at `248b822`–`248b856`. Unverified
inherited method slots are guarded. No console list execution ABI is added.

## Synthetic GPU test

From the repository root on the existing NixOS test host:

```sh
s="$PWD/scarlett/native"; w=$(mktemp -d); cd "$w"; mkdir public linked
cc=x86_64-w64-mingw32-gcc
"$cc" -Os -Wall -Wextra -Werror -DSCARLETT_DEVICE_BOOTSTRAP -shared \
  -Wl,--out-implib,sig.a "$s/d3d12_device.c" "$s/queue/queue.c" \
  "$s/d3d12_vm.c" "$s/d3d12_xs.def" -ld3d12 -o public/d3d12_xs.dll
"$cc" -O2 -Wall -Wextra -Werror "$s/test-d3d12-signature.c" -ld3d12 sig.a \
  -o public/game_scarlett_master.exe
"$cc" -O2 -Wall -Wextra -Werror -DSCARLETT_DEVICE_BOOTSTRAP -DXS_TEST_LINKED \
  "$s/test-d3d12-signature.c" "$s/d3d12_device.c" "$s/queue/queue.c" \
  -ld3d12 -ld3dcompiler -o linked/game_scarlett_master.exe
TMPDIR="$w" "$s/../test-launch-nixos.sh" "$w/public"
TMPDIR="$w" "$s/../test-launch-nixos.sh" "$w/linked"
grep -E 'PASS five|ExecuteIndirect GPU CSInvocations:' "$w"/scarlett-run.*/logs/steam-999001.log
```

The linked variant obtains the signature created by the same console slot,
then borrows its host interface privately. A Direct list executes one
indirect Dispatch from byte offset 12 of an upload buffer (`3,2,1` groups,
4 threads per group), resolves pipeline statistics into a readback buffer
and checks `CSInvocations == 24` after fence completion.
