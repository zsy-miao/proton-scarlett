# Direct queue `+0x100`: which surface gets presented?

The primary plane names `primaryRing[index]->texture->resource`. The DXGI
implementation needs to know **at creation** which host image actually backs
that resource. The title does not pass an HWND, swapchain index, host COM
interface, or address of pixels to `PresentX`.

## Static evidence (hex RVAs, image base `140000000`)

| Where | What the caller does |
|---|---|
| `2479384`–`24793d2` | Allocates and clears outer-pointer arrays (`a9439a8`, `a9439b8`). These allocations hold pointers, not images. |
| `247ae4c/247ae58`, alternate `247aeb1/247aec3` | Initialization loop calls `cc2780` with its first two registers zero and stores its return value at primary ring `[i]`; loop bound is `a94390c`. Optional secondary ring filled via `5eb26bc/5eb26c1`. |
| `cc27e4` → `5204e60` → `34beb0`; `34bffc` → `193de10` | Constructs the texture before the present request. Resource helper can take an existing interface (`193de5d` writes texture `+20`) or supply the address of the output field to resource creation (`193e286`, call through device `+428` at `193e2d4`). This does not identify the live allocation branch or a desktop swapchain. |
| `248470f`–`2484748`, `248475b`–`2484776` | Reads ring `[a9437b0]`, loads texture from outer object `+08`; secondary ring is conditional. `248482e`–`24848b8` selects primary color `0` or `12`, builds parallel texture/color arrays, count 1 or 2; the two-plane branch can swap order. |
| `24848b8` → `cd72b0` → `cd7320` | `cd74b8`: request plane count at `+38`. `cd74d3`–`cd7508`: entries start at request `+08`, stride `18`. Per entry: `+00` resource qword from texture `+20`; `+08` color u32; `+0c/+10` width/height u32 widened from texture u16 `+38/+3a`. Last four bytes not used here. |
| `2482249`–`2482255`, `2482448`, `2482487` | Current token is supplied or obtained by origin wait, stored in `75e0e88`; history at `75de4b0 + (frame & 15)*90 + 8`. The older token passed to the type-2 wait at `2482405` is separate. |
| `24822a7`–`24822cd`, `2482313`–`248235f` | Builds one integer `(0,0,width,height)` RECT per plane. Each plane gets **the same current token**, resource count 1 and the **address of its entry's resource qword**. Destination is a shared float `(0,0,1,1)` (constant at `6159cd0`). |
| `248236a`–`248237c`, `24824a6`–`24824c1` | Zeroes present parameters and sets flags at `+18` to 0 or 1. `RCX=queue`, `EDX=plane count`, `R8=&planes[0]`, `R9=&params`; HRESULT checked after call. |

**Plane (`48` bytes):** `+00` token u64, `+08` resource count u32,
`+10` resource-pointer array, `+18` source RECT array, `+20` placement rect
pointer, `+28` color u32, `+2c` filter u32, `+30` extension count u32,
`+38` extension pointer, `+40` flags u32. **Present parameters (`20`
bytes):** `+00` threshold float, `+04` view count, `+08` extension count,
`+10` extension pointer, `+18` flags. Padding is ignored.
Field order agrees with the [public Go declarations](https://github.com/hajimehoshi/ebiten/blob/4c219926eef626a924f821547f2b7231a4572783/internal/graphicsdriver/directx/d3d12_windows.go).
Microsoft's [FramePacing example](https://github.com/microsoft/Xbox-GDK-Samples/blob/b1e77b6ac10f95b07296025fda171ca2ddc42dbe/Samples/Graphics/FramePacing/FramePacing.cpp#L2074-L2113) passes an origin token and a display render target to `PresentX`.

## Minimum retail integration

1. Obtain a trusted host HWND/factory, actual dimensions and a compatible
   SDR format and ring length. Open the chain once on the **presenting
   console queue's own host Direct queue**. This candidate supports two
   RGBA8 buffers; retail format, ring length and index alignment must be
   established before enabling this route.
2. In display-resource creation, have the title resource wrapper returned at
   texture `+20` actually render into `chain->GetBuffer(i)`. Call
   `xs_display_bind(queue,i,titleInterface,hostRenderResource)` there.
   The binder checks `GetBuffer(i)` identity and retains both objects.
   Resource reuse must preserve the association. A separately allocated or
   differently formatted title image needs an explicit synchronized copy or
   conversion path; registering it against an unrelated buffer is incorrect.
3. Finish rendering in PRESENT state on that queue (or order other queues
   with fences). The producer accepts only the current buffer's registered
   key, then passes the exact token to the device clock's serialized
   `xs_frame_present`. No pointer from a plane is treated as a host object.

No retail creation path calls these private hooks yet. Unknown keys, missing
chain, non-full rectangles, multiple planes/resources, HDR, nonzero options,
extensions and the zero-plane teardown do not submit. A resize or recreated
ring requires a new binding lifecycle. Scheduler completion semantics and
the separate `+118` token-bearing submit are unchanged.

## Synthetic check

`test-present-x.c` creates the console queue via device `+40` and builds
the wire arrays independently. Two distinct synthetic COM keys stand in for
title resource interfaces. A D3D12 upload copy on the underlying Direct queue
paints mapped buffers red, then green. Only the console queue `+100` presents;
each token completes via type 2, and the other stays pending after the first
present. Invalid descriptors and mappings preserve both the count and token.

```sh
s="$PWD/scarlett/native"; w=$(mktemp -d); mkdir "$w/game"
x86_64-w64-mingw32-gcc -O2 -Wall -Wextra -Werror -DSCARLETT_DEVICE_BOOTSTRAP \
  "$s/test-present-x.c" "$s/d3d12_device.c" "$s/queue/queue.c" -ld3d12 -ldxgi \
  -o "$w/game/game_scarlett_master.exe"
SCARLETT_TEST_GPU=radv SCARLETT_TEST_DISPLAY=sway SCARLETT_RECORD=1 TMPDIR="$w" \
  scarlett/test-launch-nixos.sh "$w/game"
```

Check `PASS PresentX` in the Proton log and decode the center pixel of
`display.mp4` as in `scarlett/tests/radv-present.sh`; it should change
from red to green in that order.
