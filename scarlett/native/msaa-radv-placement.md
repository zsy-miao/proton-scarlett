# Why RADV stops at `+cc2936`

Read-only retail and a negative control on the RTX 3070 isolate the **color
placement**. Both devices ran providers built from `442e321e`. Both allocation
queries succeeded and the depth placement returned `DXGI_ERROR_UNSUPPORTED`
and null in every run.

| Run | Device `+428` for 4× RGBA8 | Next observed stop |
|---|---|---|
| RADV | Unsupported, null | Title breakpoint `+cc2936`, caller `+5bfec15` |
| RTX | Success, original WC-backed resource | Device `+a0`, title return `+cc29ea` |
| RTX, scratch provider rejecting only the normally successful RGBA8 placement | Unsupported, null | Title breakpoint `+cc2936`, caller `+5bfec15` |

The placement call stacks diverge at `+5bfeb90` (color) and `+5bfebce`
(depth). With no color object the title reuses offset `0xe800000` for depth;
with a color object it requests depth at `0xec00000`. In the successful color
case the *same object pointer* is passed first to device `+90` (RGBA8
`TEXTURE2DMS` SRV, return `+164f01`), then, after depth placement failed,
to `+a0` (RGBA8 `TEXTURE2DMS` RTV, return `+cc29ea`). At this revision the
RTV writer returns `E_NOTIMPL` to its fail-stop adapter. The actual uses
therefore require a sampleable multisample **attachment**, even though
retail has not submitted any draw using it. The negative control changed
no title code or exception handling.

## Same-payload check

`test-radv-msaa-import.c` runs natively on RADV 26.3.0 (`1002:164e`).
Build with `cc -std=c11 -O2 -Wall -Wextra -Werror test-radv-msaa-import.c
-lvulkan -o probe` and select the RADV ICD with `VK_DRIVER_FILES`.

The positive control exports a matching `0x89700000`-byte WC buffer (type 2)
as a DMA-BUF, reimports it and drops the original buffer and Vulkan memory
object. The fd permits types `0x104`; buffer requirements permit `0x7ad`.
A GPU fill at `0xe800000` is visible as `ace0d00d` in an independent CPU
mapping after queue completion; both adjacent words retain their sentinels.

The results below were identical for sampled + color attachment (`0x14`)
and that usage plus both transfer directions (`0x17`):

| Candidate | Measured restriction for the 1×1 4× RGBA8 image |
|---|---|
| DMA-BUF | Linear and optimal return `VK_ERROR_FORMAT_NOT_SUPPORTED`. Each of five DRM modifiers reports sample count 1 only. |
| Opaque fd | Optimal advertises 4×, but requires an image-dedicated allocation; `0x21010` bytes, `0x10000` alignment, type bits `0x7ad`. An existing large buffer allocation at a nonzero offset cannot satisfy this. |
| Mapped foreign host pointer | Both linear and optimal return `VK_ERROR_FORMAT_NOT_SUPPORTED`. |
| Host allocation pointer | Optimal 4× also requires a dedicated allocation. The title range is an mmap of a Vulkan DMA-BUF, not independently host-allocated memory. |

The buffer fill proves the exported allocation and its offset work for a
buffer. It does not establish multisample renderability or sample storage.
No supported image binding to that allocation was found.
