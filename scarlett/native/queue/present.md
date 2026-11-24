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

## Display creation: primary ring vs. general texture allocator

**The primary ring call sites lead to device `+3f8`, not `+428`, on the
ordinary SDR path.** This follows from the arguments, rather than from
recognizing a function name:

- `247add3` builds the compact texture description; `247add8` ORs `8` into
  byte `+c`. `501f4a0` initializes byte `+d` to zero. `247ae4c/aeb1` pass
  null reuse/placement arguments. `cc27b4` sets texture flags to `28` (or
  `a8` in the alternate loop). `34bfd6/bff9` pass the null fifth argument
  of `34beb0` to `193de10`; the reuse branch `193de5d` cannot be selected.
  `164e52`–`164e65` copy all 14 compact bytes to texture `+38`; consequently
  the display bit lives at texture `+44`, not in the resource pointer.
- `193de6b` clears texture `+18`; `193dedc` sends the null allocation
  argument to the regular descriptor builder (or control already went there
  at `193dec9`). `193e11d` tests bit `8` at texture `+44` and takes
  `193e150` → `193e1b4`. RCX is device alias `75df040`, RDX points to
  heap properties `{DEFAULT,0,0,1,1}`, R8D=`8` (`ALLOW_DISPLAY`), R9
  points to the resource descriptor. Caller stack `+20/+28/+30/+38/+40/
  +48/+50`: initial state, optimized clear pointer, null, u32 zero, null,
  IID, `&texture[+20]` (byte offset). The IID at `63fa690` is
  `696442be-a72e-4059-bc79-5b5c98040fad` (`ID3D12Resource`).
  Do not truncate this extended committed-call ABI to the desktop signature.
- By contrast `193e2d4` on device `75df048` matches the public
  [`CreatePlacedResourceX(address,desc,state,clear,iid,out)` call](https://github.com/microsoft/Xbox-GDK-Samples/blob/b1e77b6ac10f95b07296025fda171ca2ddc42dbe/Samples/Graphics/DirectStorageTextureShuffling/DirectStorageTextureShuffling.cpp#L606-L625).
  RDX is a **placement address**. `abfad0` provides the size/alignment pair;
  `193e243` calls `2474690` to allocate if no backing was supplied.
  `2474690` selects `221ad20` or `24746d0` (which calls `4fc99c0` and
  tail-calls `4fac3c0`), optionally rounding size/alignment to 128.
  There is also an allocation attempt at `193e220` for a different flag
  combination. If `base <= addr < base+size`, `193e28a`–`193e2ad`
  maps `addr` to `addr-base+alias` using `9e1dac0/c8/d0`. R8=`&desc`,
  R9D=state, caller stack `+20/+28/+30`=clear/IID/out. `193dff8` is a
  second `+428` call with texture `+18` as address, state 6, null clear.
  Size, alignment and address equality are not proof of shared GPU storage.
- The descriptor built at `c4b030` has dimension u32 `+00`, alignment u64
  `+08`, width u64 `+10`, height u32 `+18`, depth/array and mips u16
  `+1c/+1e`, format u32 `+20`, sample count/quality `+24/+28`, layout
  `+2c`, flags `+30`; builder zeroes through `+3f`. It can write console
  layout/flag values as well as standard values. In the ordinary `28`
  texture-flags branch, `48d8d0` → `4dbffb0` maps initial state selector
  1 to raw state **6**, not desktop `PRESENT`. `193e089` supplies an
  optimized clear value from format and the title's color table. Don't
  discard it or assume the image starts in scanout state.

**Output and ring.** `2478130` stores configured width/height. Before the
window or device, `2478481` calls `5eb24f0`, copying those values and the
format byte from `a9438cc/d0/dc` to `75e6fe0/e4/e8`. `247ada6`–`247add3`
use these for the primary texture; `501f4ba/c2/d3` write width/height u16
and format u8. `58aacb0` selects mode 2 by default, with a runtime
override; `58aad40` returns `1c` (RGBA8 UNORM) for 2. Other modes read
table `61e03e0 + mode*28 +20` (`18` and `0a` for modes 0 and 1).
The secondary loop runs **before** the primary loop if enabled (`247ad69`,
`5eb25b1`); it uses different dimension/format globals and also sets the
display bit. Heap flag 8 alone cannot identify the primary output.

`22435f2`–`22435fc` set ring count to `2 | configByte`; `2479373/937e`
copy it to the wrap limit. `247afa2` resets index to zero, `24747c6`–`24747dc`
increment then wrap at that limit. This is ascending title order, not an
observed equality with the host index at the first present.

In the RADV retail run that reached `+0x3f8`, the wrap limit at `a94390c`
was 2. The failure unwound to `247aeb6`, the return from the primary
texture constructor. The call requested a 3840×2160 RGBA8 texture with
`ALLOW_DISPLAY`, default heap properties `{1,0,0,1,1}`, a transparent
black clear, and `IID_ID3D12Resource`. Both ring pointer arrays already
existed; their allocation alone does not identify a rendered image.

`24784d5` registers ANSI `sgaWindowClass` (class name `677b672`; window
procedure `247a660` tail-calls `DefWindowProcA`). `247853a` calls
`CreateWindowExA` with that class, title `a9437b8`, style `00cf0000`,
configured dimensions, no parent. The nonnull returned HWND goes straight
to `ShowWindow` at `2478553`; device creation follows. The input dimensions
are outer-window dimensions, not proof of client or surface size. A window
bridge can pass this successful create result for the current process and
lifetime to the graphics bootstrap. Nothing in the resource call identifies
that HWND. Do not use a foreground-window search or create a competing window.

**Retail observation (RADV, private 800×600 Sway output):** Wine's `+win`
trace showed the title thread creating `sgaWindowClass`, caption `Americas`,
with a requested outer size of 3840×2160. `NtUserCreateWindowEx` returned
`0x10082` in that run and `show_window` used the same handle. A separately
created `Default IME` window had a different handle. All this precedes the
174 MiB placed-buffer request; neither the HWND value nor the requested
window size establishes a swapchain index, image size or rendered frame.

## Decision at future initialization

Correlate the returned HWND with device/queue creation; record primary vs.
secondary loop, index/count, full descriptor, heap properties, clear value
and initial state at `193e1b4`; follow the returned interface into the
actual resource/view and transition calls. Compare the first plane key
and index with this ring before turning on a retail bridge. Direct wrapping
requires an equivalent backbuffer for **all** required rendering uses and
states. Otherwise preserve the original render target and use a synchronized
copy/resolve into a compatible output. For a genuinely placed request also
prove host allocation/offset/alias ownership. Both observed slots currently
report the descriptor prefix and fail with a null output.

## Private binding

1. Obtain a trusted host HWND/factory, actual dimensions and a compatible
   SDR format and ring length. Open the chain once on the **presenting
   console queue's own host Direct queue**. This candidate supports two
   RGBA8 buffers; retail format, ring length and index alignment must be
   established before enabling this route.
2. In display-resource creation, have the title resource wrapper returned at
   texture `+20` actually render into `chain->GetBuffer(i)`. Call
   `xs_display_wrap(queue,i,hostRenderResource,&privateSurface)` there.
   The wrapper owns the checked `GetBuffer(i)` and `xs_surface_host` returns
   that exact image for rendering. This private IUnknown is not a retail
   ID3D12Resource implementation and must not be returned for that IID.
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
the wire arrays independently. The opaque keys are the two real surface
wrappers. A D3D12 upload copy via each wrapper on the underlying Direct queue
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
