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
| `24795c6` | `75e75e8` | 12 | `0x69` | Dispatch for the observed form (next dword zero) |
| `2479605` | `75e75f0` | 12 | 2 | Dispatch |
| `2479646` | `75e75f8` | 12 | 10 | DispatchMesh, subject to OPTIONS7 mesh tier and host creation |

The third argument descriptor starts with a **qword** `0x69`, hence a
type of 105 and the next dword zero, not two arguments. At `248cdef` the
title passes this signature to list `+0x1e8`, with command count 1 and a
buffer offset; ordinary Dispatch uses the same method at `248cbba`. The
slot alone does not identify the operation; the paired callers below do
identify the payload for one pass. A failing creation HRESULT aborts the initializer.

The [Microsoft DirectX IDL](https://github.com/microsoft/DirectX-Headers/blob/main/include/directx/d3d12.idl)
defines the four standard enum values and descriptor layout.
[ComputeParticles](https://github.com/microsoft/Xbox-GDK-Samples/blob/b1e77b6ac10f95b07296025fda171ca2ddc42dbe/Samples/Graphics/ComputeParticles/ParticleSystem.cpp#L224-L236)
uses the same single Draw/16-byte/null-root shape.
[Mesh shader API design](https://docs.xbox.com/build/core-features/graphics/newfeatures/ms/ms-api-design)
describes indirect mesh dispatch of three group counts. None of these
defines a translation for `0x69`. For that type alone, the adapter also
checks the explicitly written second dword; its meaning is unknown.
It creates an owned host signature and exposes its own controlling `IUnknown`.
The title releases all five outputs at `248b822`–`248b856`. Unverified
inherited method slots are guarded. No console list execution ABI is added.

## What `0x69` consumes in the meshlet-culling pass

`38fcb0` selects one of two paths for the **same work record**, according
to `522dd70`. Both select the `meshlet_culling` object (name at `6781144`,
loaded at `5b03ef8`–`5b03f0d`), then the same pass entry at table offset
`+0x138` (`3908e6`, `32c0706`). The difference follows the list:

| Path | Address passed by caller | Signature and final list call |
|---|---|---|
| Compute (`context+0x12348`) | `38fe0c` → `[rsp+0x58]` at `38fe5d` → `390a3b` | `248cd20`: `75e75e8` (type `0x69`), `248cdef` |
| Direct (`context+0x127c8`) | `35b790e` → `[rsp+0x58]` at `35b7963` → `32c085b`, extra byte offset `r8d=0` | `248cbe0`: `75e75f0` (type 2, Dispatch), `248cd09` |

In both rows let `A` be the qword from `work+0x58`. The list receives
`(signature, 1, [75df108], A - [75df140], NULL, 0)`; the Direct wrapper
adds the zero extra offset before subtracting the base. Neither wrapper
reads or repacks the argument. In particular, the three stack dwords
constructed at `390a25`–`390a30` go to the context-state setter `4e96580`,
**not** to `A`. The matching Direct branch sets those same state dwords.

**Writer evidence:** `317fd`/`31804` and `47e307`/`47e30e` use the work
object at owner `+0x150`. On the allocation path `4e31d9d`–`4e31db6`, it
requests `0x20` bytes and saves the returned address at `work+0x58`.
`3deaef` passes that address to `38ff60`; when its binding is active,
`38ff79`–`38ff90` supplies the address, length `0x20` and stride `0x20`
to `511f150`. The helper selects pass entry `+0xf0` and issues `(1,1,1)`
at `39010d`. It is also called after the indirect work (`38feb3`). This
identifies a shader binding and a candidate GPU writer; the actual shader
stores and runtime values of X/Y/Z were not recovered. Only the first
12 bytes are required by the matching standard Dispatch path.

**Translation evidence:** the paired calls support interpreting those first
three dwords as `D3D12_DISPATCH_ARGUMENTS` for this pass, including on its
Compute list. No public numeric definition or guarantee about additional
console scheduling semantics was found. Microsoft's [public topic
directory](https://docs.xbox.com/nda/nda-topic-directory) indexes the Series
enum but does not publish its values. The [pinned vkd3d-proton signature
validator](https://github.com/HansKristian-Work/vkd3d-proton/blob/212991fc2c266bc0d59f4c4ce8f80f7126508d71/libs/vkd3d/command.c#L25179-L25261)
recognizes standard Dispatch as a compute action; a single raw `105`
is unsupported and fails its exactly-one-action check.

The creation adapter accepts the observed form: one argument, stride 12,
null root, node mask 0, second argument-descriptor dword 0. It requests
standard Dispatch from the host but retains the original type in the wrapper.
Borrowing the host signature requires the original type, so a recorder
can distinguish the two cases. No equivalence is claimed for other uses of
`105`.

## Synthetic GPU test

From the repository root on the existing NixOS test host, with a Linux DXC
on `PATH` or `DXC` set to its absolute path:

```sh
s="$PWD/scarlett/native"; w=$(mktemp -d); cd "$w"; mkdir public linked
cc=x86_64-w64-mingw32-gcc
"$cc" -Os -Wall -Wextra -Werror -DSCARLETT_DEVICE_BOOTSTRAP -shared \
  -Wl,--out-implib,sig.a "$s/d3d12_device.c" "$s/queue/queue.c" \
  "$s/d3d12_vm.c" "$s/d3d12_xs.def" -ld3d12 -o public/d3d12_xs.dll
"$cc" -O2 -Wall -Wextra -Werror "$s/test-d3d12-signature.c" -ld3d12 sig.a \
  -o public/game_scarlett_master.exe
for e in produce consume; do
  "${DXC:-dxc}" -E "$e" -T cs_6_0 "$s/test-d3d12-signature.hlsl" -Vn "$e" -Fh "$w/$e.h"
done
"$cc" -O2 -Wall -Wextra -Werror -I"$w" -DSCARLETT_DEVICE_BOOTSTRAP -DXS_TEST_LINKED \
  "$s/test-d3d12-signature.c" "$s/d3d12_device.c" "$s/queue/queue.c" \
  -ld3d12 -o linked/game_scarlett_master.exe
TMPDIR="$w" "$s/../test-launch-nixos.sh" "$w/public"
TMPDIR="$w" "$s/../test-launch-nixos.sh" "$w/linked"
grep -E 'PASS five|GPU-generated Dispatch indirect counter' "$w"/scarlett-run.*/logs/steam-999001.log
```

The linked test obtains both signatures from the same console slot. On each
list a producer shader writes `(3,2,1)` at byte offset 12 of a 32-byte UAV
and clears a separate counter. After the UAV and indirect-argument barriers,
a four-thread consumer increments the counter. Fence and readback verify
**24** on both the `0x69` Compute path and the standard Dispatch Direct control.
