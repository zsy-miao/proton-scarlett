# Direct queue and shared fence adapters

`xs_direct_queue` creates a normal-priority Direct queue on node 1.
`xs_shared_fence` creates a zero-initialized `D3D12_FENCE_FLAG_SHARED` fence on
the supplied desktop device. Each returns one owned `IUnknown` reference; the
objects have independent identities and release their own vkd3d objects.
The bootstrap device exposes them under their observed IIDs; QI returns
the wrapper, never a desktop vtable. The standalone constructors expose IUnknown.

The queue prefix implements two non-IUnknown operations: `Signal(fence, UINT64)`
at byte offset `0x80` and `GetTimestampFrequency(UINT64 *)` at `0x90`. The former
unwraps only a fence from this adapter. Use queue and fence objects created on
the same host device. `Wait`, CPU `Signal`, and event registration are typed
host operations without assigned console slots.

## Why those two slots

Evidence is from static inspection of [the same executable as the device
notes](../d3d12_device.md), plus the [Microsoft D3D12 interface definitions](https://github.com/microsoft/DirectX-Headers/blob/main/include/directx/d3d12.idl)
and [Microsoft's GDK sample `WaitForGpu`](https://github.com/microsoft/Xbox-GDK-Samples/blob/main/Samples/IntroGraphics/SimpleDeviceAndSwapChain/SimpleDeviceAndSwapChain.cpp).
The sample uses queue `Signal` and fence `SetEventOnCompletion`; it does not
specify vtable offsets. Addresses below are RVAs.

| Evidence | Interpretation |
|---|---|
| `0x247878a` → `0x24787e2` → `0x5a86192` | The first Direct queue is passed to the title's bookkeeping object at `0x75e0e98`. A separate device call at `0x1d0b2fa` requests the fence IID, value 0, flags 1. On success the title stores the queue at `book+0` and the fence at `book+0x10`. These are **title bookkeeping** offsets, not offsets in either COM object. |
| `0x248481c` → `0x50bb280` → `0x1f6330` | The submission branch at `0x50bb314` takes queue, fence and counter from `book+0`, `+0x10`, `+0x30`. The producer copies them into a 72-byte work record at offsets `0`, `0x30`, `0x40`. |
| `0x2481fd6`–`0x2482013`, `0x24825e0`–`0x2482610` | The worker reloads those fields. With a non-null fence, it calls `queue[+0x80](queue, fence, value)` in RCX, RDX, R8 and passes EAX to the HRESULT handler. This submits `Signal`; the title advances the submitted counter separately. |
| `0x2484187`–`0x24841f7` | Receiver is the original queue global `0x8759ff8`. `+0x90` takes a pointer to one qword. On success with a nonzero result the caller computes `1000.0 / result`, consistent with converting GPU ticks to milliseconds. |
| `0x248b4b5` → `0x2472f60` → `0x108660`; `0x248bcc4` | The bookkeeping fence and the original queue are independently released via slot `+0x10`. |

## Fence `+0x60`: CPU-readable completed value

`4fb0a75` loads the owned fence from `book+0x10`; `4fb0aec` calls its
`+0x60`, saves RAX in RDI and loads `[rdi]` as a qword without a null check.
The address survives calls and sleeps in the wait loop: `4fb0d21` arms
`MONITORX`, `4fb0d24` reloads, `4fb0d52` does a timed `MWAITX`, and
`4fb0d55` reloads again. All ones takes a separate handler (`2474870`).
The target is masked to 56 bits at `4fb0aba`; the default comparison tests
bit 55 of the difference (`4fb0b2d`). These are caller conventions, **not**
a reason to truncate the word. The caller neither frees the address nor
passes it to a GPU. No post-final-Release reader was identified.

Windows [D3DKMTOpenSyncObjectFromNtHandle2](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmthk/ns-d3dkmthk-_d3dkmt_opensyncobjectfromnthandle2)
can return a monitored CPU mapping; [pinned Wine](https://github.com/ValveSoftware/wine/blob/e60892cc2398b0a83c332a2078c7b85e47bdb42b/dlls/win32u/d3dkmt.c#L1794-L1815)
sets that address to zero. Desktop `ID3D12Fence` exposes completion as
a value or event. The adapter therefore returns its own naturally aligned
64-bit word, borrowed for the lifetime of the fence. The first address call
samples completion and arms a thread-pool timer (requested period 1 ms).
The timer reads `GetCompletedValue`; CPU `Signal` also samples synchronously
when active. Samples are serialized. Only a changed value is published,
with a full-width interlocked store. Queue submission never publishes a
value. Final Release cancels and drains the timer before freeing the cell
or releasing the host fence. Outstanding GPU signals use the host fence;
[vkd3d retains it through retirement](https://github.com/HansKristian-Work/vkd3d-proton/blob/212991fc2c266bc0d59f4c4ce8f80f7126508d71/libs/vkd3d/command.c#L22804-L22836).

A failed `GetDeviceRemovedReason` publishes `UINT64_MAX`, as does a host
completed value of all ones. Checking the device separately matters:
[pinned shared-fence code](https://github.com/HansKristian-Work/vkd3d-proton/blob/212991fc2c266bc0d59f4c4ce8f80f7126508d71/libs/vkd3d/command.c#L1710-L1727)
returns zero for a failed Vulkan counter query. Loss not reported by either
host API cannot be distinguished from a valid result. Sampling can lag
completion and skip transient values between ticks; this is a current-value
mirror for the observed advancing counters, not a GPU-addressable mapping,
a per-signal notification, or a way to add rewind support to the host.

## Scratch GPU test

From the repository root on the NixOS test host:

```sh
src="$PWD/scarlett/native/queue"
work=$(mktemp -d)
mkdir "$work/game"
x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -Werror \
  "$src/queue.c" "$src/test.c" -ld3d12 -o "$work/game/game_scarlett_master.exe"
SCARLETT_TEST_GPU=radv TMPDIR="$work" "$src/../../test-launch-nixos.sh" "$work/game"
grep -F 'PASS queue adapter:' "$work"/scarlett-run.*/logs/steam-999001.log
```

On an AMD CPU with user-mode MWAITX, add `-mmwaitx -DXS_TEST_MWAITX` to
exercise the title's monitor/recheck/timed-wait sequence. On RADV the same
pointer remained zero while the queue was gated, then published `2^63+3`
and later `2^63+2^32+3` behind a second gate. Another monitored fence was
released with its signal still queued. The device-removal check injects
only the failed device reason; the host fence is real. It does not induce a
GPU reset (`ID3D12Device5::RemoveDevice` is a stub in pinned vkd3d).

## Direct command allocator callback registration (`+48`)

At `1884fe` the x64 title passes This in RCX, the function at `555df10`
in RDX and the enclosing title context in R8. It does not set a fourth
argument for this call or inspect a result (`188501`). The function's
body is consistent with a memory allocator: R9 supplies a size, argument
5 supplies an alignment (small values are log2), and argument 7 supplies
the context. It requests storage, updates the context's tracked allocation
and returns a pointer. This is static evidence, not a complete callback
invocation ABI. There is no demonstrated size/alignment request at registration.

The pair is opaque, borrowed and local to the direct guest allocator. The
adapter records it together under a lock. The existing list reference keeps
the wrapper alive across recording; GPU retirement follows the host queue
and fence. Reset does not dispatch or consume the registration. Neither
context nor returned title storage is owned by a retirement worker. A new
registration with different values, a null pair or a compute registration
is outside the observed contract. No packet-memory invocation is implemented.

`test-allocator-registration.c` includes the queue adapter for private
inspection. Build with the staged-device defines and link d3d12_device.o,
d3d12_vm.o and `-ld3d12` (not queue.o). It registers a counter function
and inaccessible context, submits a real barrier list behind a queue gate,
drops the caller's allocator reference, waits for completion, checks reset
preserves the pair, then releases the list and the caller's context. There
are no callback calls during these operations.

Unchanged RTX retail registered the pair, reset a pooled direct list on
that allocator, then stopped at the list's `+100` (`4e4cac9`) with two
visible heaps of the same device: 0x130000 resource entries and 128 samplers.

The AMD retail trace establishes the ownership order: the direct allocator
had one reference when the title registered the pair at `188501`. On the
same thread, a previously closed pooled list reset at `2474cd4`, took a
second reference to that allocator and released its old allocator. No
allocator reset, callback invocation or queue submission preceded `4e4cac9`.
This sequence does not establish thread affinity for a future callback.

## Initial pooled-list heap binding

Pooled Direct list `+0x100` binds bank 0 of the 1,245,184-entry resource heap
and the 128-entry sampler heap from the same guest device. Bank 0 holds
1,000,000 slots. The remaining 245,184 slots belong to a distinct bank;
the initial bind does not switch to it.

On RADV and RTX, `test-allocation-info.cpp` sampled resource slot 999,999
with samplers 126 and 127. The red/green seam read `ff008080` with linear
filtering and `ff00ff00` with point filtering. Resetting and rebinding the
list while the submitted work was gated kept the original heaps alive until
its fence completed. Both retail runs passed the bind and stopped at
`+0x238`, title return `+0x4ed46aa`.

## Pooled-list prefix marker

On both adapters retail records a zero at `+0x238`, targeting offset 8 in
the 64 KiB control page. On AMD this records a `WriteBufferImmediate` to the
original GPU-visible section. On RTX, Wine `0015` retains a CPU-only alias
of the original page. On submission, a queue fence orders the CPU write
after earlier work and before this list executes. Resetting a discarded
recording makes no write. Neither path assumes a general mid-list stage.

Build `queue/test-prefix-marker.c` like `test-allocator-registration.c`
for RTX. Build `test-allocation-info.cpp` with `-DTEST_RADV_MARKER` for AMD.
Both tests reuse the original CPU address while the queue is gated. Only
the intended original words change. On AMD a later GPU copy on the same
list reads the new marker and both neighboring words.

Retail advances to list slot `+0x240`, return `+0x2482b37`, before closing
or submitting this list. Execution and retirement are fixture results.

## Timestamp following the first marker

On RTX, list `+0x240` receives an aligned eight-byte destination in the
marked system-memory pool and control arguments `0,0,1`. The adapter retains
a mapping of the original 64 KiB window, records a timestamp query, and
resolves it into that window. Each submission pins the destination and query
heap through its fence. In `test-prefix-marker.c`, pausing the marker worker
after its store kept both the timestamp and the downstream fence pending.
After release, the two timestamps increased within the calibrated GPU-clock
interval; the discarded slot and the reused CPU mapping were unchanged.

RTX retail recorded the timestamp before requesting a second `+0x238` on
the same list. That marker cannot be moved before the timestamp. Another
thread reached queue `+0x118` with token 1 and flags 0. AMD reaches the same
two requests after recording the timestamp into its original write-back pool.
Neither retail run submitted the timestamp-bearing list before stopping.

On RADV the two observed timestamp offsets are `0x4196218` and `0x4196698`.
The fixture holds the queue, reuses the CPU address, and checks the original
eight bytes against the queue's 100 MHz clock calibration. Every other byte
in the 64 KiB window and the replacement mapping must stay unchanged. Build
`test-allocation-info.cpp` with `TEST_RADV_MARKER` and `TEST_WB_OFFSET` set
to either offset; link `testq.c` instead of `queue.o` to inspect the queue
clock in this fixture.

## Marker after the timestamp

The second `+0x238` publishes `0x29a5ee15` at control-page offset 12.
On RADV it stays in the original native list: GPU readbacks immediately
before and after the write saw the same timestamp and different marker
values. On RTX the CPU-only marker separates two native command lists.
The tail restores the recorded initial heap bindings. The fixture replayed
the same closed list twice; each time the GPU copies and timestamp preceded
the second marker, and the final copy and fence followed it. Discarding a
recording produced no writes.

Both retail runs next requested operation 7 at control-page offset zero
(`+0x250`, return `+0x2484b12`). A different thread submitted a closed
Direct sync list at `+0x118`, return `+0x248268f`, with flags zero and the
frame-origin token issued by its own guest device. Submission still rejects
this token: its submit-side effect is unverified. Token-zero submission and
GPU retirement alone do not advance the presentation count or complete the
issued frame.

The title's statistics helper reads an **older** token from a four-entry
history ring before the current sync-list submission. The read therefore
doesn't tell us what tagging this submission changes. In `test-present-x.c`,
both GPUs kept the present count unchanged and the issued token pending
while an untagged sync list was gated **and after its fence retired**. Two
subsequent presents each advanced the count once and completed their own
token. A foreign or reconfigured frame clock rejected the old token.

### Control counter at list `+0x250`

The RTX trace recorded `(cell, 7, snapshot, 1)`. The CPU increments a
64-bit source counter (`+0x302810`) and publishes it to the control page
(`+0x44b6c3`). The operand passed to the list is a snapshot of the source.
At recording time the source, snapshot and original control cell were all
`1`; the preceding marker at offset 12 still held `0`, although the list
had recorded `0x29a5ee15` for it. The list had not executed.

The evidence strongly identifies this slot as `Wait64BitValueX`.
Microsoft's [PipelinedPostprocess sample](https://github.com/microsoft/Xbox-GDK-Samples/blob/b1e77b6ac10f95b07296025fda171ca2ddc42dbe/Samples/Graphics/PipelinedPostprocess/PipelinedPostprocess.cpp#L288-L293)
passes a `D3D12_COMPARISON_FUNC` in the second argument and a wait flag in
the fourth. In that [comparison enum](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ne-d3d12-d3d12_comparison_func),
7 is `GREATER_EQUAL`: the cell must reach or exceed the reference when the
command executes. The sample uses `D3D12XBOX_WAIT_FLAG_NONE`. Microsoft's
[Xbox topic directory](https://docs.xbox.com/nda/nda-topic-directory) describes
`D3D12XBOX_WAIT_FLAGS` for Series consoles as “Flags for selecting the engine
used to perform a wait.” The public entry does not map `1` to an engine.
A second title call site
(`+0x248ff7c`) uses flag `0`; it does not show the two flags are interchangeable.

On RADV the initial publication stores a 64-bit `1` in the same control
section as the two markers, at offset zero. To qualify flag `1`, compare
native execution with flags `0` and `1`, holding the queue, address and
reference fixed. Observe work following the wait with the cell below, equal
to and above the target, including values differing only in the high 32 bits.
The guard remains: the observed `1 >= 1` while recording does not establish
that later GPU work may proceed when this list is submitted or replayed.

### Experimental whole-queue wait

The opt-in staged build (`-DSCARLETT_EXPERIMENTAL_DIRECT_WAIT` on `queue.c`)
implements only the `+0x250` shape below; default builds keep the slot guarded.
The native control page is mapped from the original retained buffer on RADV;
on RTX the post-marker callback uses the original CPU-only alias. Each
submission samples its own unsigned 64-bit threshold after the head retires.
A low sample rearms the same one-shot callback, without blocking a pool thread
or holding a queue lock. A timeout alone never releases the tail.

The submission owns an extra gate fence reference through tail retirement:
releasing the last reference on observer cancellation can let vkd3d run a
pending suffix. The RTX fixture injects a failed device reason on a timer
wake and checks that the observer alias is released without running the tail.
A healthy unsatisfied queue cannot cancel its already-submitted work and
reclaim the retirement resources. Test `test-prefix-marker.c` with the same
define (add `TEST_FAILED_WAIT_REASON` for the fault fixture); for RADV use
`testq.c` and `test-allocation-info.cpp` with `TEST_RADV_MARKER`,
`TEST_POST_SNAPSHOT` and the experimental define.

Two scratch builds support the observed pooled Direct list, comparison `7`,
flag `1` and control-page offset zero. Both record the wait without reading
the counter to decide whether to skip it. The test implementation holds
all following Direct-queue work; it does not identify the Xbox engine.

* **RADV:** A GPU test submitted the list behind a gate, reset and released
  the guest list, then reused the control-page address. The replacement held
  the target; the original counter was lower by `2^32`. The first marker,
  timestamp and second marker completed. The following copy, shader output
  and fence stayed pending. Raising the original counter above target
  released all three. In separate two-list tests, equality also released the
  suffix, and an independent COPY queue published an overshoot to the original
  allocation while the Direct queue was blocked.
* **RTX 3070:** The CPU-only page required a retained alias. On two submissions
  of the same recording, a below-target original with matching low 32 bits
  kept the suffix and fence pending. Equality and unsigned overshoot released
  them. An independent Direct queue retired while the first was stalled.

On both experimental builds retail next called list `+0x490` (return
`+0x32113`). The AMD trace parked the concurrent tagged submit **at its
guard** to observe the recording thread; it did not execute the tagged list.
These traces show recording, not retail execution. A producer scheduled
behind the stronger wait could deadlock even if it could run on the console.

Combined runs on RADV and RTX associated token `1` with a fence signaled
immediately after the submitted sync list. The tagged thread returned from
submission on both, and the recording thread reached `+0x490` without parking
the submitter for tracing. In gated tests on both adapters the associated fence
retired before a later queue fence, while the frame wait stayed pending until
`PresentX`. Retail has not been observed querying that association.

On RTX `+0x490` received one texture barrier for the first primary's guest
resource. The barrier covers all subresources and switches sync, access and
layout from shader writes (`ALL_SHADING`, `UNORDERED_ACCESS`) to rendering
(`RENDER_TARGET`). The traced call requested a transition; it did not draw.

### Primary transition at pooled list `+0x490`

The AMD and RTX calls returning to `+0x32113` record one enhanced texture barrier.
The guest resource matched the first committed primary returned by the same
`+0x3f8` device factory. The list copies the group at record time, resolves
that identity in the device's live color registry, and records the barrier
on its current host segment. The source is all shading / unordered access;
the destination is render target for both sync and access. The layout also
changes from unordered access to render target, for all subresources. A
global UAV barrier would miss this layout change. See Microsoft's
[texture barrier](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ns-d3d12-d3d12_texture_barrier).

Build `test-allocation-info.cpp` with `-DTEST_PRIMARY_BARRIER` and put
`dxc -T cs_6_0 -E main test-primary-barrier.hlsl -Fo test-primary-barrier.dxil`
output next to the executable. On both GPUs the fixture submits a shader fill and
partial RTV clear through the pooled list, resets it while the queue is
gated, and drops the guest resources and descriptor records. It verifies
both halves of the first image, the untouched second slice and padding,
and destruction of the host texture after the queue retires. This fixture
needs neither the experimental control wait nor a tagged submission.
In staged retail runs with both experiments enabled, both GPUs next requested
a full render-target clear at `+0x1a0` (return `+0x50a62de`) before submission.

### Primary clear at pooled list `+0x1a0`

On AMD and RTX the call returning to `+0x50a62de` selects a tagged RTV for the image
just transitioned at `+0x490`, four positive zero floats, zero rectangles
and a null rectangle pointer. The adapter records exactly that first clear
on the open direct list. It holds the owning device's descriptor lock from
slot validation through `ClearRenderTargetView`; the slot may then be
rewritten or released. [RTVs are consumed during recording](https://learn.microsoft.com/en-us/windows/win32/direct3d12/non-shader-visible-descriptor-heaps).
The retained primary, rather than the mutable descriptor, lives through the
submission fence.

Add `-DTEST_PRIMARY_CLEAR` to the primary-barrier fixture build above. It
rewrites the guest RTV to the sibling immediately after recording the guest
clear, resets/releases the list and drops the guest heap behind a queue
gate. GPU readback checks both complete 3840×2160 textures; the backing
check also covers slice padding and unused space. Retail next records a
second `+0x490` for the same image with sync `0x40→0`, access
`0x8→0x80000000` and layout `2→0x1f`. That layout is unqualified.

With `SCARLETT_EXPERIMENTAL_PRIMARY_RELEASE`, the exact post-clear release
on the same primary records a transition to host `COMMON`. This is a
recording-local choice, not a general mapping of guest layout `0x1f`.
Build the primary-clear fixture with `TEST_PRIMARY_RELEASE` and use that
experimental define on `testq.c`. The producer leaves a red stripe in the
black texture; an independent Direct queue, submitted first, waits for the
producer fence, acquires from `COMMON`, and reads both images. RADV and RTX
preserved the stripe, remaining black pixels, sibling image and padding.
Both experimental retail runs next requested a second timestamp (`+0x240`,
return `+0x2482b37`); neither demonstrated a subsequent acquire or flip.

The opt-in release path records the second timestamp at pool offset `0x4196210`,
eight bytes before the first timestamp. On RADV and RTX a GPU test replayed the same closed
list and observed `head₁ < tail₁ < head₂ < tail₂`. In each pass the head
became visible while the counter wait still held the tail. Resetting and
releasing the submitted list and reusing the pool address did not redirect
either write. The recorder compares section identities before reusing the
first query heap and destination buffer; query index 1 belongs to the tail.
Run [`tail-timestamp.sh`](../../tests/tail-timestamp.sh) with `radv` or
`nvidia` and the staged tool directory to reproduce the replay and VA-reuse test.
On each adapter a staged retail run reached queue `+0x118`, flags `3`
(return `+0x24821e3`). Another thread requested device `+0x1e8`
(return `+0x12b981e`). The batch did not execute in those runs.

A subsequent RADV experiment admitted one closed pooled Direct list on the
same queue and live token as the previous flags-zero sync list. It submitted
the head and tail around the counter wait and signaled a separate fence after
the tail. In retail, that fence reached `1`; the title returned to `+0x24821e3`
and issued two signals on its own fence. This establishes GPU completion for
the observed singleton, not the meaning of the individual bits in flags `3`
or a presentation. Unsupported calls remained stopped during observation.

`SCARLETT_EXPERIMENTAL_TAGGED_BATCH` opts a staged build into the two observed
singleton shapes. It requires `SCARLETT_EXPERIMENTAL_DIRECT_WAIT` and
`SCARLETT_EXPERIMENTAL_PRIMARY_RELEASE`. A nonzero token remains guarded in
default builds. Eligibility requires a closed, unexposed list created by the
queue's guest device, a matching host device and an unescaped Direct queue.
The flags-zero sync submission pins its allocator and records its own fence
against the current frame issuance. The flags-3 path additionally requires the
same queue, the identical heap rebind after the prefix, and the complete
post/wait/primary-release/tail-stamp recording. It pins both recorders and
allocators through a second fence signaled after the tail. Frame reconfiguration
or eviction drops the association without dropping the independent retirement
pins. Neither operation advances the frame clock or presents.

Run `scarlett/tests/tail-timestamp.sh radv STAGED-TOOL tagged` (or `nvidia`).
The optional last argument selects a paired timestamp in the original 64 KiB
window: `0x6218`, `0x64a8`, or `0x6698`. This is an ordinary queue-order
experiment for one recorded sequence; the console flag-bit semantics remain unknown.

### Markers after the first presentation

On a reused pooled Direct list, retail writes `1` at control-page offset
`0x50`, then `0x29a5ee15` at `0x54` after a timestamp. AMD records native
GPU writes; RTX uses the fence-ordered alias of the original page.
`test-marker-bank.c` checks this pair and the earlier `+8/+c` pair on
both paths: an unsubmitted recording changes nothing, and a submitted
recording updates only the two original words after address reuse.

Four retail launches (two diagnostic, two ordinary). Both ordinary runs
presented token 1. AMD then stopped at timestamp `+240` (`+2482b37`);
RTX issued token 2 and stopped at wait `+250` (`+2484b12`). Neither
presented token 2.

For the second marker pair, a RADV diagnostic observed timestamp offset
`0x419aa38` in the write-back pool. The RTX wait used offset zero of the
original control page, operation 7, reference 1 and flags 1. On both
GPUs the fixture verified that the timestamp and post marker completed
while the suffix fence stayed pending. After address reuse, only reaching
or exceeding the unsigned 64-bit threshold on the original cell released it.

Four retail launches (two diagnostic, two ordinary). The final RADV run
again stopped at `+2482b37`; its rejected destination was not captured.
RTX passed the wait and reached `+344188`, sizing a 2048×2048 `A8_UNORM`
texture with layout `0x100` and flags `0x300000`. Both presented token 1;
only RTX issued token 2, and neither presented it.

The subsequent RADV rejection was qword index 1 in the active kind-2
timestamp record. Checking that record and its array resolves the location
when the pool address changes. Relocated-record tests reject a wrong
owner, stride or index. With this change and the linear A8 sizing rule,
both ordinary runs issued tokens 1 and 2, presented only 1, and stopped
at device `+360` (return `+abfe0d`) for the 1024×1024 A8 atlas.
This batch used four diagnostic and two ordinary retail launches.

After atlas setup, pooled Direct list `+418` (return `+156099a`) records
a global barrier after the second-bank wait: mask `0xc280`, control
`0x20000`, address zero, range `0xffffffffff00`. On both adapters the
fixture found the readback unchanged below threshold, then verified the
overlapping copies completed in order after the original counter advanced.

Six retail launches (four diagnostic, two ordinary). The two ordinary
runs issued tokens 1 and 2, presented only token 1, and stopped at the
virtual call returning to `+5121894`. Slot `+40` alone does not identify
the receiver. The earlier RTX breakpoint at `+5707b8c` did not recur.

### Pooled Direct topology fallback

The title checks the inline cursor (`+0x18`) against the limit (`+0x30`).
The Direct wrapper advertises zero capacity in a read-only prefix, with a
guard at `+0x38`. The observed fallback calls `+0xa0` with topology 4;
the host list records `IASetPrimitiveTopology(TRIANGLELIST)`. The draw
fixture first sets a strip so that omitting the fallback changes the result.
Both GPUs returned the expected fenced sample counts: 64, 48, 32, 16.

Two ordinary retail launches, no diagnostic launches. Both workers then
reached list `+0x3f0` (return `+3263e`); on RTX the main thread first
faulted at `+b5a5f8`. Both runs issued tokens 1 and 2 and presented only 1.

The next pooled Direct command, `+0x3f0` (return `+3263e`), sets a 1×1
shading rate with two `PASSTHROUGH` combiners. Both GPUs produced 16
coarse and 48 fine pixels in the ordering fixture. One diagnostic and
two ordinary retail launches followed the worker to `+110` (return
`+327cb`). On RTX the `rage_bink` technique lookup succeeded at index 1
but group slot 1 was null. Both ordinary runs presented only token 1.

Pooled Direct `+110` (return `+327cb`) binds the graphics root signature
from the group's `+0x80` field to the active recorder. The fixture reset
and released the list before its fence completed and still read back all
four expected pixels through `t0, space3` on both GPUs.

Four retail launches: two RTX diagnostics and two ordinary runs on the
same staged build. Both workers reached `+258` (return `+32820`). On RTX
the main thread first hit `+b5a5f8`: the `rage_bink` builder's 308-byte
root signature had returned `E_NOTIMPL`. Accepting that layout in a
diagnostic reached device `+1c8`; completed group creation is unverified.
Both ordinary runs issued tokens 1 and 2 and presented only token 1.

An RTX trace now ties the `rage_bink:bink_opaque` group in slot 1 to its
accepted 308-byte root. The fixture exercised all seven table entries and
static sampler `s3`: both GPUs read back 761, 805, 849, 849. The title
handles `E_NOTIMPL` on the `+1c8` pipeline warm-up request by skipping
that cache record. Publication of the group does not establish a retail draw.

Four retail launches (two diagnostic and two ordinary). The ordinary
workers stopped at `+258`, return `+32820`. The first RTX fault was again
`+b5a5f8`, this time from the second technique lookup (`+5e50ffd`).
Both runs issued tokens 1 and 2; neither presented token 2.

The second lookup selects `rage_bink:bink_alpha`, slot 0. Its 332-byte
root adds `t8,space1` at table offset 3, before `t7`. On both GPUs the
shared fixture read back 1203, 1273, 1343, 1343 for alpha and 761, 805,
849, 849 for opaque. In the RTX diagnostic all four video requests
pointed to their selected groups. The groups shared a cached root;
that snapshot does not demonstrate a retail video draw or HDR rendering.

Five retail launches (three RTX diagnostics and two ordinary runs on the
same stage). AMD stopped at list `+258`, return `+32820`. RTX first
rejected a seven-mip 128×72 `R32_FLOAT` size query at `+344145`.
Both issued tokens 1 and 2; the final present count was 1 on each.

Pooled Direct `+258(0)` now records the requested depth order on the
guest list. It survives a recording split or failed reset, prevents a
native recorder borrow, and clears on successful reset. No guest draw
with pending depth order is enabled. Both adapters passed the list-state,
ordering and lifetime tests. AMD reached list `+178`, return `+328be`.

At `+328be` the observed index-buffer view was null. The pooled list now
records `IASetIndexBuffer(NULL)` and preserves pending depth order. With the
linear RGBA8/D32-S8 clear pipeline accepted, AMD instead stops earlier at
`+3278b` while writing its selected pipeline to `list+0x544`. A diagnostic
matched that pipeline to the subsequent `+c8` bind on the same recorder.
The cache bytes were all zero; only the object argument identified the
pipeline. No retail clear draw has been observed.

Six retail launches (four diagnostics and two ordinary runs) and ten
passing fixtures on the main-based build. AMD stopped at `+3278b`; RTX
stopped at `+4ecc821`. Both issued tokens 1 and 2 and presented only 1.

Pooled Direct `+c8` now binds the selected clear pipeline. The two
16-byte guest cache stores land on a separate page; the inline cursor
and limit remain read-only zeros. The bind checks the owning device's
live pipeline object and exact root, and retains them through submission.
On AMD the next call was `+c0(0)`, return `+329a1`, with depth order
pending. No retail draw was observed.

Four retail launches (two diagnostics and two ordinary). RTX first
stopped at `+4ecc821` creating the A8 lookup texture's shader view. Both
ordinary runs presented only token 1. Of 14 fixture launches, 13 passed;
the first byte-buffer test package was missing a shader and its corrected
retry passed. Three simultaneous scatter mappings and removal of the
middle mapping passed on both GPUs; the third retail return is unverified.

Pooled Direct `+c0(0)` records `OMSetStencilRef(0)` on the current list.
The hardware test seeded a nonzero reference and checked the changed
stencil comparisons. AMD then reached `+470`, return `+329e9`: the
requested 64-bit value was all ones and the cached value was zero. The
following blend-factor call was not reached.

Four retail launches (two diagnostics and two ordinary); all eleven
fixtures passed. The RTX diagnostic accepted the aligned 128-byte A8
lookup row and the separate 307,200-byte `R8_UINT` buffer. A later A8
row, at `0xd611fbd00`, was 256-byte aligned but not 512-byte aligned,
and the direct copy-footprint path rejected it. RTX ordinary first
stopped at the A8 view writer (`+4ecc821` from `+501f703`). Both ordinary
runs issued tokens 1 and 2 and presented only token 1.

The Direct list now retains `+470(~0ULL)` only when the recorder owns
the selected clear pipeline and has its exact graphics root bound.
The setting survives a list split. While it is pending, ordinary and
tagged submissions are refused before host execution. Both adapters
passed the reset, mixed-list rejection and retirement fixtures.

Three retail launches: one RTX diagnostic and two ordinary runs on the
same binaries. Both adapters first stopped at `+b8`, return `+32a0c`;
the diagnostic captured blend factor `(1,1,1,1)`. The 2560×1440 depth
target was created successfully in this diagnostic. That does not
explain the earlier intermittent null. Both ordinary runs issued
tokens 1 and 2 and presented only token 1 (WSI count 0→1).

Pooled Direct `+b8` copies the four blend factors and records the measured
`(1,1,1,1)` on the active list. The fixture seeded the factor to zero
and checked the resulting black control band and the RGBA output after
the setter. A split replays the factor; successful reset clears it.

Four retail launches (two diagnostics and two matched ordinary runs);
all six fixtures passed. Both GPUs first stopped at `+490`, return
`+32113`. The RTX trace matched two whole-resource texture barriers to
the selected 4K color and depth resources. The color resource matched
neither primary ring entry and both secondary entries were empty.
Neither the attachment bind nor the draw was reached. Both ordinary
runs issued tokens 1 and 2 and presented only token 1.

### Offscreen clear: paired barriers and attachment bind

List `+490` records the two whole-resource barriers in order on the same
recorder, retaining the original 3840×2160 resources. In hex, the color
barrier changes sync `1000→40`, access `80→8`, layout `6→2`; depth changes
sync `1000→20`, access `80→20`, layout `6→4`. The host receives the
original fields with the corresponding host resource pointers.

At `+33311`, list `+190` received one RTV and a pointer to a DSV handle
on the caller's stack. Both handles resolved to the barrier pair, with
serials 119 and 121 in the RTX capture. The bind validates both records
under one descriptor lock and copies the descriptors into private heaps.
Both GPUs replayed those copies after a split even when the producer
slots had been rewritten. The color target is offscreen; the bind does
not establish a draw, composite, or token-2 render submission.

| Batch | Retail launches | Hardware fixtures | First ordinary stop |
| --- | --- | --- | --- |
| Barriers | 2 diagnostic, 2 staged ordinary, 1 installed RADV | 4 passed; 1 RADV setup failure corrected by allocating the WC pool earlier | Both staged GPUs: bind at `+33317`. Installed `1a5178f2`: barriers at `+32113`. |
| Attachment bind | 1 RADV diagnostic, 2 staged ordinary | All 6 passed | Both GPUs: **list** `+230`, return `+33396`, setting depth bounds. |

RADV measured `(0.0,1.0)` on the clear recorder. The independent
[host control](../test-depth-bounds.cpp) on both GPUs counted `32/0`
samples on the left/right halves with narrow bounds and `32/32` with
full bounds. At this stage the guest setter was still guarded.
In every staged ordinary run and the installed comparison, the
game issued tokens 1 and 2; only token 1 presented (WSI count 0→1).

### Clear raster state and vertex source

The list records the measured depth bounds `(0,1)`, one scissor
`(0,0,3840,2160)`, and one viewport `(0,0,3840,2160,0,1)` after binding
the offscreen pair. Split recordings replay all three on the tail;
successful reset clears them. Eight fixture launches passed across the
depth-bounds and raster stages. The pending `+470` submission guard is
unchanged.

Five retail launches: three diagnostics and two final ordinary runs.
RADV and RTX both stopped at list `+160`, return `+32327`, before a draw.
On RADV, parameter 0 received the CPU writer's VA `0x1143ff80` on the
#4 recorder. The 100-byte span starts at `+0x3ff80` in the original
placed allocation and contains the three clear vertices. The allocation
was live, but `ScarlettPlacedDataV1` does not expose its first 27 MiB.
The bind-time stamp and reuse counter do not prove the bytes survive
through GPU consumption. Both ordinary runs issued tokens 1 and 2 and
presented only token 1 (WSI count 0→1).

### Clear vertex source and first draw entry

On RADV, the CPU stores at `+1228f1..+12299b` wrote the 100-byte triangle
record later passed to root parameter 0 on the same recorder. List `+160`
now records a copy of that range from the matching original allocation
into a private GPU buffer, transitions the buffer for vertex-shader reads,
and binds it. A split rebinds the same buffer without copying it again.
The gated fixture distinguished a write to the retained backing after
recording from both a CPU snapshot and a replacement at the same address.

Both ordinary runs stopped at **list `+60`, return `+bff06`**. The RADV
diagnostic measured `DrawInstanced(3,1,0,0)`. The chunk's stamp, hold count
and reuse counter stayed unchanged from bind to draw entry. There was no
host draw or consuming retail fence: pending `+470` still blocks submission.
Only the startup image presented (token 1, WSI count 0→1), although both
adapters issued token 2.

Three retail launches: one RADV diagnostic and two matched ordinary runs.
Five fixture attempts: the first RADV control held an extra pipeline
reference; the corrected source and ordering fixtures passed on both GPUs.
The input diagnostic recorded successful `GameInputCreate` and completion
of the controller enumeration callback in the title. It recorded no
gamepad reading or state copy before the draw stop.

### Diagnostic only: after the clear request

In six retail diagnostic launches and six guard fixtures, a probe saved
the arguments to `+60` and made the recording ineligible for submission.
On the final matched RADV and RTX runs it returned to the caller without
issuing a host draw. The game dropped one hold on the vertex chunk
(`2→1`) and cleared its source pointer. No chunk reuse or consuming fence
was observed. Both runs next stopped at list `+a0`, return `+325c0`.

A follow-up RADV diagnostic measured topology **17** there, on the same
offscreen color/depth pair. The title was setting up a color-only
rectangle, before the optional pass that samples the color image.
Recording just the topology request exposed a null shader group in the
pipeline lookup at `+b5a5f8`. The previous clear pipeline was still
cached. Both topology guard fixtures passed; rectangle coverage was not
tested in these runs.

The only observed token-2 queue submission was a flags-0 sync singleton
on a different list. The diagnostic builds were not promoted. The
ordinary first stop remains `+bff06` on both GPUs, with one startup
presentation each.

One further RADV diagnostic traced the missing group to device `+80`.
The game requested a root signature with one descriptor table: a volatile
constant-buffer view at `b27,space1`. The provider returned `E_NOTIMPL`
and a null root. The factory discarded the group and stored null in
slot 42. When `im:CS_BLIT_COLOUR` selected that slot on the same asset,
it replaced the previous group with null; lookup then faulted at `+b5a5f8`.
The run issued tokens 1 and 2; only token 1 presented (WSI count 0→1).

### Colour-pass root signature

Device `+80` now accepts that exact layout. Eight fixture runs passed
across AMD and RTX. On each GPU a test shader read four distinct values
at byte offset 32 through the returned root's CBV table.

The RADV diagnostic followed the successful root through group creation
and slot 42 to the same group's pipeline request. The request still had
no pipeline. The first stop was list `+110`, return `+327cb`: the recorder
still held the previous clear pipeline and rejected the new root. The
game copied the 96-byte constant block into its ring; the four floats
at offset 32 were zero. No retail shader read from that buffer was observed.

On the identical ordinary stage, both AMD and RTX still stopped at the
earlier offscreen draw (`+bff06`). In each of the three retail launches,
the game issued tokens 1 and 2 and presented only token 1 (WSI count 0→1).

### Colour pipeline creation

The slot-42 request is asynchronous. Its worker missed the native cache
and device `+2b0` returned `E_NOTIMPL`. The factory now recognizes the
measured stream, both shaders, the input declaration and the owned b27
root. The host shaders preserve smooth vertex colour and multiply by the
float4 at CBV offset 32 before rounding toward zero to half precision.
Both adapters produced RGBA bytes `1e 4b 7d be` in the rounding control
and `11 35 89 dd` in the packed-colour control.

The post-fix RADV diagnostic observed the worker publish a live pipeline
and set the ready bit. The recording thread had already stopped at the
root bind (`+327cb`). The latest ordinary RADV run issued tokens 1 and 2
and stopped at the offscreen draw (`+bff06`); RTX issued token 1 and hit
the intermittent depth-target breakpoint (`+5707b8c`). Each presented
only token 1 (WSI count 0→1).

This batch comprised two ordinary runs, two RADV diagnostics and six
passing fixtures. Two of the fixtures independently measured 4×4 sample
coverage: 6 for the original triangle, 16 for the completed rectangle,
and 32 when drawn twice. No retail rectangle draw has been observed.

### Clear pattern and colour-pass tests

On AMD and RTX, the standalone clear test painted 48 pixels, left 156
guard pixels intact and preserved both depth and stencil. It checked the
pattern across a command-list split and then restored full coverage. The
first run failed on each GPU: two expected panels were swapped. Both
passed after correcting the expected images, without changing the shader.

The [bounded-copy fixture](../test-b27-placed.hpp) checked that the
colour pass reads its 96 bytes from the original allocation after address
reuse. Both GPUs read the four expected pixels from offset 32, left all
160 padding bytes intact and rejected a new copy using the stale identity.

A separate fixture on each GPU bound the colour root before the worker
finished creating its pipeline. Only an explicit later pipeline bind made
that pipeline current. In the RTX diagnostic the root bind succeeded; the
next stop on the recorder was the vertex-buffer bind (`+180`, return
`+32966`). The first process breakpoint was `+1d52fa1` on another thread.
Tokens 1 and 2 were issued; only token 1 presented (display count 0→1).

### First offscreen clear recorded in ordinary retail

AMD recorded the game's `DrawInstanced(3,1,0,0)` on the host Direct list.
It used the full-coverage mask and the frozen offscreen colour and depth
views (serials 119 and 121). The next unsupported call was list `+a0`,
title return `+325c0`. Before the clear, token 2 had a sync submission
with no rendering batch. No subsequent submission of the clear was
observed. On the identical stage, RTX stopped at the 1440p depth factory
(`+5707b8c`) after issuing only token 1. Each GPU presented token 1 and
advanced its display count from 0 to 1.

The integrated fixture ran first on both GPUs. Three fenced readbacks
checked the diagonal pixel pattern before and after splitting the list,
then full coverage after restoring all ones. Both depth and stencil
were unchanged across all 3840×2160 pixels. Ordinary provider SHA-256:
`a8e70b7bfd7595e1189957bdbc534182b0512ad028b651b532df75cc8b843c12`.

In a separate AMD diagnostic, the colour pass bound a 4,080-byte vertex
view (stride 24, first vertex 0) and selected its one used CBV table
entry. The game wrote and copied that descriptor inline; neither slot
had a provider serial. A private fixture with supplied descriptor
identities passed on both GPUs. Binding that inline entry in ordinary
retail still requires preserving the active heap slot and its original
backing through completion.

A later RTX diagnostic with a failure-only depth probe reached the same
clear and recorder stop as ordinary AMD (`+325c0`). Its first process
breakpoint was `+1d52fa1` on another thread. The depth failure did not
recur, so its cause remains open. The game issued tokens 1 and 2 and
presented token 1 once. Two earlier attempts to load the diagnostic
failed before the probe ran.

Separately, the [mode-5 transfer fixture](../../tests/im-transfer/README.md)
passed all four cases on AMD and RTX. The ordered two-list fixture wrote
the expected red/green primary pixels on both, rejected a bad second list
without submitting the first, and held its command memory until the fence
completed. Neither fixture flipped the display. A token-2 retail rendering
batch that writes the current primary has yet to be observed.

### Command memory for the first rendering batch

The title can return a list to its pool as soon as queue `+118` returns.
Reuse of the list's command bytes is a separate decision: the last hold
puts the chunk on a return chain, the Direct producer stamps it with a
queue sequence, and the recycler checks that dependency before advancing
the chunk generation. Keeping the allocation mapped does not keep its
bytes unchanged.

In 21 hash-checked retail logs, 18 issued token 2 and queued its sync;
none logged a token-2 batch end. The 100-byte range already traced for
the clear is vertex data. To admit a rendering batch, join each member
to its actual command ranges (or observe its whole recording with no
packet allocation), then check what happens to an overlapping reuse
request before and after the consuming fence. The frame token, list
return and fence completion are distinct events.

### After the clear on both GPUs

On the same ordinary stage, RADV and RTX each recorded one offscreen clear
with the all-ones mask and attachment serials 119/121. The game selected
rectangle-list topology (17) and reached the vertex-buffer bind. On each
adapter the first process and recorder stop was list `+180`, title return
`+32966`. Token 2 had only a sync submission with no rendering batch.
Token 1 was the only successful `PresentX`; both display counts went 0→1.

Before those launches, the clear and delayed-pipeline fixtures passed on
both adapters. Splitting the list preserved the rectangle selection and
the clear's original resources. The new root did not inherit the clear's
vertex source; the colour pipeline became current only on an explicit
bind. Both depth and stencil remained unchanged at all 3840×2160 pixels.
The ordinary provider SHA-256 was
`a7aefdcf31b0c57cba4a85b900a1259c72139037cd4c4f43624846fa9868cecc`.

The next six fixture runs passed (three per GPU). The inline table test
checked the `+120` handler itself and both unsupported neighbours. Its
earlier runs exited 1 because the fixture still expected the handler to
be absent. With that expectation corrected, the split replay, reset,
96-byte copy, padding and replacement-allocation checks all passed. The
other two fixtures per GPU rejected the offscreen barrier pair before
the colour-root switch and accepted it on the split list after the switch.

The colour root uses a [volatile descriptor range](https://learn.microsoft.com/en-us/windows/win32/direct3d12/root-signature-version-1-1#descriptors_volatile).
Setting the table selects a slot; it doesn't freeze that slot's contents
until execution. A successful bind-time comparison alone cannot qualify
a later submission using a private copy of the descriptor. This matters
even when the 96 bytes of constant data come from the correct allocation.

### Staged rectangle vertex bind

The next private stage (`4df41ad6…`) passed its integrated fixture on
each GPU before the ordinary runs. Both recorded the offscreen clear,
bound the 4,080-byte vertex view and passed the offscreen barrier pair.
AMD stopped at the descriptor-table bind (`+120`, return `+32206`). RTX
first hit `+1d52fa1` on another thread; its recorder stopped at the same
table bind. Both issued tokens 1 and 2, with only a sync submission for
token 2. Only token 1 presented (display count 0→1).

In a separate AMD diagnostic the destination was in the active visible
heap, but eleven nonvisible heaps met the producer search criteria.
The candidate rejected the bind before its transactional read. The colour
PSO was still null. No draw or skip after this call was observed.

### First composite vertex bind

The `+120` setter now records the selected slot in the pinned active heap.
It reads no descriptor bytes and needs no CPU transaction. The focused
fixture passed on both adapters, including an empty slot, a later rewrite,
split replay and a delayed pipeline bind.

To rebuild that fixture with local test inputs, run
`scarlett/tests/build-colour-recorder.sh PRIVATE-INPUT-DIR MATCHED-XMEM-DLL NEW-OUTPUT-DIR`.
Pass the resulting `game/` directory to the shared test launcher.

Both ordinary runs then stopped at a **second** vertex-buffer bind, list
`+180`, return `+32966`. The caller chain `+1263f0 ← +4fd1e7b` puts this
call in the alternate composite pass. On each GPU it was the first
process and recorder stop. Each recorded one offscreen clear, issued
tokens 1 and 2 and presented only token 1 (display count 0→1).

In the separate AMD diagnostic, the colour-table setter returned `S_OK`.
The game tested the pipeline it had selected for the preceding colour
rectangle: it was still null, so the game **skipped** that draw and entered
the composite. The same list requested a new 4,080-byte vertex range
at `0x1143df10` while its first range at `0x1143ef30` was still pinned.
The recorder rejected that replacement. Neither the composite's output
generation nor a rendering submission for token 2 has been observed.

### [Ordered-batch display control](../../tests/ordered-display.md) (synthetic)

On both GPUs the same fixture executable presented a red/blue primary
for token 1. For token 2 it submitted two lists: the first filled an
intermediate image, the second copied it to a different primary and
released that primary. A completed but unrelated fence was rejected.
With the actual producer fence complete, `PresentX(2)` succeeded on the
original window generation and the count advanced from 1 to 2.

Independent decoding of the private recordings found red/blue followed
by green/magenta on both adapters (capture frames 26→50 on RADV, 42→65
on RTX). These are capture sample numbers, not frame tokens. This checks
the route from a known rendering batch through presentation to visible
pixels; the corresponding retail batch has yet to be observed.

### Composite source and destination

The same ordinary build on AMD and RTX accepted the replacement vertex
buffer. Both first stopped at list `+490`, return `+32113`, with the
composite caller `+1263f0`. Each recorded one offscreen clear and issued
tokens 1 and 2. Token 2 queued synchronization only; token 1 was the
only successful presentation (display count 0→1).

The AMD diagnostic identified the source as the original offscreen
render target (RTV serial 119). The request was one whole-image transition
from render target to shader resource. Technique 96 selected ring entry
1 of 2 as its intended destination. Its candidate RTV (serial 17)
resolved to the registered primary. Recording stopped before the output
bind; no shader sample or write to that primary was observed.

Before ordinary testing, both GPUs passed fenced comparisons of the two
4,080-byte vertex ranges, including the last legal three-vertex window
at index 167. Splitting the recording bound the replacement buffer and
preserved both original ranges. Ordinary provider SHA-256:
`cf348e198ef8ea021a038ed7be0bc720112d53ec8f0b0d5dac42c9fcd28c235b`.

### What the offscreen clear would display

In the saved AMD startup capture, all 72 decoded 800×600 RGB samples
match an all-zero image. The single successful presentation in that
run was token 1. A later retail draw trace measured leading-vertex RGBA
`(0,0,0,1)` and a full-screen triangle for clear #4. With the full 4K
scissor and all mask lanes enabled, that draw would write opaque black
to the offscreen target. This is a shader prediction; execution of the
retail clear has not been fenced and read back.

A copy of that clear alone would leave displayed RGB unchanged. The
technique-96 shader also multiplies sampled RGB by vertex colour, so
sampling an unchanged black clear would still produce black RGB.
Identifying the intended primary or advancing a presentation counter is
therefore insufficient to establish a visibly different retail frame.

The colour rectangle after #4 did have white vertex colours: the packed
colour at byte 12 of each of its three 24-byte records was `ffffffff`.
In the same AMD trace the four colour multipliers at `b27+32` were zero,
so the translated shader would have produced zero RGB. A separate AMD
diagnostic observed the game skip that draw. No captured retail draw
has yet joined a nonblack source to a visibly changed primary image.

### First composite output bind

On both GPUs the ordinary recorder accepted the whole-image transition
from render target to shader resource. It kept the distinct primary
image intact. The first process and recorder stop was list `+190`,
return `+33317`. Both runs issued tokens 1 and 2, presented only token 1
(display count 0→1), and showed a blank client.

The AMD diagnostic saw the actual output bind: one RTV, no depth view.
The slot had one match, serial 17, for the current registered primary
(ring entry 1 of 2). The setter returned `E_NOTIMPL`. The worker for
the same technique-96 request also returned `E_NOTIMPL` and published
no pipeline.

An independent output-binding fixture passed on both GPUs: after a public
slot rewrite and command-list split, the fenced readback was cyan on the
original primary. That establishes the fixture binding, not a retail draw.

### Composite display target bound

The same ordinary build on AMD and RTX bound the selected primary as the
sole render target, with no depth view. The first process and API stop on
both was device `+230`, return `+4d9f369`, under composite parent `+1263f0`.
Each issued tokens 1 and 2. Only token 1 presented (display count 0→1);
both private captures showed a blank client.

The AMD diagnostic identified a 32-element raw shader view of a 128-byte
buffer at `0x1143de20`. Creation returned `E_NOTIMPL`. The same run also
traced the two shader groups through misses in both outer caches to the
same inner root-cache entry. The worker's stream was 688 bytes and had
three input elements. Its complete shader hashes and fixed state were
not recovered in that run.

### Composite raw buffer view

The writer accepts the measured 128-byte raw view on the original placed
allocation. It checks the view's format, range and alignment; DWORDs 3,
9 and 14 are not initialized by this caller. With nonzero test values in
all three fields, both GPUs read all 32 late-written words through the
view, got zero for the next 32 reads and retained the original backing
after address reuse. The reported view size was 128 bytes.

Build the fixture with `scarlett/tests/build-raw32.sh MATCHED-XMEM-DLL NEW-FIXTURE-DIR`;
run the resulting directory on each GPU with the shared launcher.

On the identical ordinary stage both GPUs passed device `+230`. Their first
process and recorder stop was the composite table bind, list `+120`, return
`+32206`, under `+1263f0`. Each issued tokens 1 and 2 and presented only
token 1 (display count 0→1). Neither capture showed a recognizable image.
The prior failure's operands weren't captured, so the earlier stop cannot
be attributed to a particular uninitialized word.

An AMD diagnostic traced the composite table from its producer to the
same list's `+120(0, handle)` call. All three entries were in the active
visible heap, starting at slot 368373:

| Cell | Source at the bind |
| --- | --- |
| 0 | The 128-byte raw view, nonvisible slot 690, serial 128, offset `0x3de20` in the original placed allocation. |
| 1 | The selected texture binding, visible slot 98, SRV serial 118. |
| 2 | An inline constant-buffer descriptor from nonvisible slot 18, with no registered serial. |

All 32 bytes of each entry matched its source at the setter. The three
destination slots still had no registered serial or kind. The pipeline
was pending, and the rejected setter left the prior colour table
selected. No shader read or token-2 presentation followed.

### Successor-list primary release

The three-cell bind passed its fixtures on AMD and RTX, then both ordinary
runs first stopped at list `+490`, return `+32113`, under `+24847a8`.
Each issued tokens 1 and 2, queued only synchronization for token 2,
and presented only token 1 (display count 0→1). The paired runs used
provider `a87fcadb8331d0effa643c21a3192e8016c2453346870dcc5ef759780f021981`.

The AMD diagnostic saw the composite input bind succeed. The pipeline
was still pending and the game skipped the composite draw. It closed the
list, opened a successor and requested a whole-image `2→1f` release.
The ring key and RTV slot matched the predecessor, but the successor
held neither the primary registration nor its RTV serial. The release
was rejected before a rendering batch was observed. Closing one list
does not establish GPU execution order between it and the next.

### First token-2 rendering batch request

The recorder now saves the successor's release request against the closed
producer's original primary registration and RTV serial. Both GPUs passed
the release fixture after fixing its setup to use separate allocators for
two simultaneously open lists. In the ordinary pair, RADV first stopped at
list `+240` (timestamp, return `+2482b37`); RTX first stopped at queue
`+118` (submission, return `+24821e3`). Each issued tokens 1 and 2;
only token 1 presented (display count 0→1).

The RTX diagnostic saw the actual flags-3 array for token 2. Member 0 was
the closed, split composite recording (generation 627); member 1 was the
closed successor (628). Both named the same release receipt, queue, clock
epoch and registered primary, with RTV serial 17. No third member was
present. The retail queue rejected the count of two before host execution.

A separate fixture on each GPU checked the same order against the primary
writer and descriptor generation. The valid pair executed once, advancing
WSI 0→1→2 with a visible red/blue to green/magenta change. Reversed,
duplicate and stale pairs were rejected before execution.

The AMD diagnostic reproduced the successor's `+240` after the recorded
release. Bank 2 contained 2,048 eight-byte entries. The caller selected
the high index (0), not the low index (1). Comparing the section handles
confirmed that the destination at offset `0x419aa30` and the producer's
head timestamp at `0x419aa38` belonged to the same write-back allocation.
The release and successor timestamp had not executed when recording stopped.

In a split-producer fixture on both GPUs, the head timestamp completed
while the counter still blocked the tail and the batch-end fence remained
pending. Opening the counter allowed the tail pixels and successor release
to complete, then one batch-end fence signaled. Both captures changed from
red/blue to green/magenta and the display count advanced 0→1→2. This
fixture's successor contained only the release; it did not test the AMD
retail timestamp that follows it.

A second RTX diagnostic captured all 39 calls on the producer and all four
on the successor. The latter bound its heaps, recorded the primary release,
recorded a timestamp, then closed. Its timestamp used its own query and
destination, eight bytes before the producer's head timestamp. The producer's
token-2 wait used threshold 1. The CPU sampled the counter at 1 when the
batch was rejected; the GPU never ran the wait.

Neither recording requested more command storage from the allocator
callback. Publication associated the producer's acquire and the successor's
release with the same chunk, generation 1. Its hold count was zero at both
observations. Submitting the pair still requires keeping the recorded work
and everything it references alive through the batch fence.

### Guarded submission retail test

Both GPUs passed the gated two-list fixture: while GPU reads were pending,
the game's recycler fence stayed at 2 even after its queue accepted signal
3. It reached 3 only after the original batch completed. CPU and foreign
queue attempts to signal the protected fence were rejected.

Both ordinary runs used provider `535335f6` and backend `c77099d1`.
On AMD both lists closed, and the queue worker matched the original
vertex-data chunk and its fence. The render thread stopped at list `+238`
(marker, return `+4ed46aa`) before a submission result or batch completion
was logged. On RTX the producer closed; the successor stopped at list
`+240` (timestamp, return `+2482b37`) before reaching the queue check.
Both issued tokens 1 and 2. Only token 1 presented (display count 0→1);
neither capture showed recognizable title content.

### Token 2 submitted on both GPUs

With staged provider `cf68be0e` and backend `c77099d1`, both ordinary
runs submitted the two command lists for token 2. The game's subsequent
signal of value 3 succeeded on its original fence. Both games entered
`PresentX` for token 2, naming the submitted primary. Neither log recorded
completion of the batch-end fence or a successful return from that present.
The display count advanced only from 0 to 1. All six focused tests had passed before
the ordinary runs.

To check a saved log (including `.gz`), retain both the entry and the
return, and compare the producer fence with the submitted end in that run:

```sh
rg -zn 'PAIR (submitted|guest Signal|end=)|captured-window PresentX|WSI (init|producer token|lease:|count)|private PresentX result' "$LOG"
```

Compare the same window's decoded client images separately. A blank client
appearing or disappearing is not evidence of a second game frame.

AMD's first stop was list `+240`, return `+2482b37` from `+a1c36c`.
A separate diagnostic identified a fresh list with prefix value 2 and a
timestamp at index 1 of bank 3, offset `0x419f258` in the original WB
section. RTX first stopped at list `+238`, return `+4ed46aa` from
`+120d47`. Its diagnostic identified the post at `+0x9c` immediately
after the value-2 prefix at `+0x98` in the same control-page generation.

All 84 AMD capture frames were black. The RTX client was briefly blank
white; its log still showed only token 1 presented. Neither capture
contains a recognizable retail screen.

### Bank-3 timestamp and adjacent marker

On stage `4da78f7b`, the AMD fixture wrote the bank-3 timestamp into its
original allocation after address reuse; the RTX fixture did the same for
the `+0x9c` marker. The AMD timestamp fell between the two GPU clock samples
taken around execution. Both fixtures preserved neighboring data.

The subsequent ordinary AMD run stopped earlier on the release-side
timestamp (`+240`, caller `+a1c42c`), before the pair could submit. A
diagnostic found that the release receipt lacked the producer's timestamp
for the alternate bank-2 destination at `0x419aeb0`. On RTX the pair
submitted, `Signal(3)` succeeded, and the next list stopped at `+250`,
return `+2484b12`. Its diagnostic matched the original 64 KiB control
allocation and measured an unsigned 64-bit wait for value 1 at offset 0.
Neither ordinary run showed token-2 completion; both display counts
stopped at 1.

After fixing the missing AMD producer stamp and the RTX counter wait,
both ordinary runs on `a7bdd2b5` submitted token 2 and queued `Signal(3)`.
AMD stopped at the fresh-list timestamp (`+a1c36c`), RTX at a barrier
(`+490`, return `+32113`). Neither original end fence was observed to
complete. In a separate AMD run the timestamp succeeded and the game
requested the adjacent `+0x9c` marker on the same control section.

### Reusing render targets

On staged build `3adfd5b8`, both ordinary runs submitted token 2 and
queued the game's signal on its original fence. Neither showed completion
of that batch or a second flip. Both stopped at `+490`, return `+32113`,
but on different resource transitions:

| GPU | First stop | What the separate diagnostic identified |
| --- | --- | --- |
| AMD | Parent `+50a624f` | The game requested its first presented primary (RTV serial 14) as a render target again after the native counter wait. |
| RTX | Parent `+332e1` | The game had reacquired and cleared that primary. It then requested the offscreen 4K color target (RTV serial 119) for rendering after shader reads. |

On both GPUs a textured-composite fixture exercised the title's setter
order, from explicit pipeline bind through the three-cell table setter and
draw. Fenced readback found 64 textured pixels and 192 untouched guards.
The retail composite worker reached the matching factory request, but the
corresponding render list closed without a matching pipeline bind or draw.

The next staged provider (`7085e073`) passed the AMD primary-reuse and RTX
offscreen-target fixtures. Ordinary AMD stopped at the fresh-list timestamp;
ordinary RTX hit the earlier title breakpoint `+5a92b11`. Only token 1
presented on either GPU. Separate diagnostics on both adapters sampled the
original token-2 end fence directly: its value was still 0 at the first
refusal. This is a point-in-time sample, not evidence of a permanent stall.
The furthest RTX diagnostic reached `+190` (return `+33317`): the game
supplied the original offscreen RTV **and** DSV (serials 119 and 121).
The color-only bind rejected the DSV.

### Color and depth rebound

On staged provider `31b441c9`, four focused fixtures passed before the
ordinary pair. AMD stopped at the offscreen barrier (`+490`, return
`+32113` from `+332e1`). RTX passed the color/depth bind and stopped at
the next root shader-resource address (`+160`, return `+32327` from
`+3346d`). Both displayed token 1 only. No qualified composite factory
return or matching bind was observed in either retail run.

In the RTX fixture the carried depth/stencil contents were read back after
the fence along with a red offscreen target and black primary. The same
tested factory created and explicitly bound the private textured pipeline
on both GPUs. A separate fixture for the earlier color pass produced zero
RGBA with the measured zero multiplier, and the expected nonzero RGBA
with nonzero fixture inputs.

The reconciled opt-in provider (`ebb76880`) passed four hardware fixtures
before the ordinary pair. AMD stopped at the intermittent timestamp
(`+240`, return `+2482b37` from `+a1c36c`); RTX again stopped at `+160`.
Both submitted the pair, queued `Signal(3)` and entered `PresentX(2)`
with the submitted primary. Neither original end was observed to retire.
Only token 1 returned successfully; both display counts stayed at 1.

Separate operand captures on `31b441c9` identified the AMD offscreen
transition after the native wait and the RTX clear input in chunk 2.
On RTX the original 100 bytes fit at chunk offset `0x1ff80` (arena
offset `0x5ff80`); the old check required arena offset `0x3ff80`. This
capture says nothing about either subsequent vertex buffer.

On `eaf30add`, the AMD offscreen-restore and RTX clear-source fixtures
passed first. Both ordinary runs bound the color and depth targets. AMD
then stopped at `+160` (`+32327` from `+3346d`); RTX recorded the 100-byte
clear input and stopped at `+60` (`+bff06` from `+122a71`). On both, the
game entered `PresentX(2)` for the submitted primary after `Signal(3)`
succeeded. Neither call returned successfully; display count stayed at 1
and both captures remained blank.

The separate AMD refusal showed the source setter still required a split
at slot five rather than six. RTX requested `DrawInstanced(3,1,0,0)`;
its private views held the original attachments, but the clear path checked
the empty list-local attachment slots. It issued no host draw. Both
diagnostics sampled the submitted pair's original end at 0.

### Recording the next clear

On `6c6c93d0`, the AMD fixture copied the original 100-byte clear input
and the RTX fixture drew into the original color surface while preserving
depth and stencil. Both passed before ordinary retail ran.

| Ordinary run | First stop | Display |
| --- | --- | --- |
| AMD | Release-side timestamp, `+240`, return `+2482b37` from `+a1c42c`. Token 2 issued; no pair end was recorded. | Token 1 returned successfully, count 0→1. |
| RTX | Primary transition, `+490`, return `+32113` from `+50a624f`. The token-2 pair submitted and `Signal(3)` succeeded. | Token 1 returned `DXGI_ERROR_WAS_STILL_DRAWING` after 251 ms; count stayed at 0. |

RTX subsequently logged completion of the same token-1 fence it had
timed out on. The token-2 pair had a different fence; its retirement was
not observed. The log does not identify what delayed token 1.

Neither ordinary run returned `PresentX(2)` successfully or showed a
recognizable image. Separate diagnostics reached the next list: AMD
requested the three-vertex clear; RTX recorded it to RTV/DSV 119/121 and
reached the vertex-buffer setter (`+180`, return `+32966`). The original
token-2 end still read 0 at refusal on both. The later buffer's address
and extent were not established by those traces.

### Reusing the first vertex buffer

On `796d96a1` a separate RTX trace measured the first vertex view on the
new list: slot 0, 4,080 bytes, stride 24. It fit at offset `0x1ef30` of
the same original recycler chunk as the clear input. The fenced `+180`
fixture on `c87cbe6c` retained all 4,080 bytes and the surrounding guards
after address reuse. The ordinary RTX run then reached two transitions
of the same color target: render target → shader resource → render target.

On `2004bc54` the RTX fixture passed both transitions. Ordinary AMD
stopped at the bank-3 timestamp (`+240`, return `+2482b37`); ordinary
RTX reached the next color/depth bind (`+190`, return `+33317`). Both
entered `PresentX(2)` but only token 1 returned successfully. Each
display count stopped at 1 and both client captures were blank.

A separate AMD diagnostic identified why the timestamp sometimes passed:
the original bank-3 owner and index were unchanged, but this run's array
was at `+0x419f6d0` and the requested qword at `+0x419f6d8`. The original
WB section and full array extent matched. On RTX the new bind named the
original color and depth descriptors again. Neither observation establishes
the next vertex view or submission of this list.

### After the color and depth rebind

On staged provider `f1acc125` and backend `0045`, both ordinary retail
runs submitted the token-2 pair, queued `Signal(3)` successfully, and
entered `PresentX(2)`. Neither logged retirement of that pair or a return
from `PresentX(2)`. Both exited 2. Token 1 returned `S_OK` on both; the
same-window count advanced from 0 to 1 and the decoded clients stayed blank.

| GPU | First ordinary stop on the next command list |
| --- | --- |
| AMD (RADV) | First vertex-buffer setter (`+180`, return `+32966` from `+bfd93`). Its buffer range is still unknown. |
| RTX 3070 | Shader-table selection (`+120`, return `+32206` from `+3346d`), after rebinding the offscreen color and depth targets. |

A separate RTX diagnostic resolved the game's handle to one cell at slot
372600 in its active heap (root index 0, epoch 0). This identifies the
selected range, not its contents at draw time. The AMD diagnostic stopped
earlier at a pipeline bind. Each diagnostic sampled its own token-2 end
at 0 when recording stopped.

Before retail ran, the AMD fixture verified the relocated timestamp landed
in the original qword; the RTX fixture verified both pairs of color and
depth views survived a descriptor rewrite and list reset. Separately, both
GPUs passed the integrated source checkpoint: AMD drew the carried clear;
RTX retained the original 4,080 vertex bytes and recorded both color
barriers. Submission of the new list still requires its own source
publication and recycler fence.

### Release-side timestamp on both GPUs

On `632f07fe` with backend `0045`, the RTX fixture selected one cell in the
active heap at setter epoch 0 and rejected a repeat setter and replacement
vertex buffer. This checks range selection; it doesn't establish descriptor
contents or a native shader bind.

The ordinary runs on AMD and RTX both exited 2 at the release-side timestamp
(`+240`, return `+2482b37` from `+a1c42c`). Both issued token 2 and closed
the producer list, but neither logged a submitted token-2 pair end or
entered `PresentX(2)`. On both, `PresentX(1)` returned `S_OK` and the
same-window count advanced 0→1. The decoded AMD frames were black; the RTX
client was blank black/white. These runs did not reach the new setter.

In the separate AMD diagnostic the first refusal was a pipeline bind
(`+c8`); its submitted token-2 end sampled 0. The RTX diagnostic hit title
breakpoint `+5a92b11` during token 1. Neither diagnostic supplied a new
vertex-buffer view.

Two later diagnostics on the same baseline passed the release timestamp
and entered `PresentX(2)`. On both, guest `Signal(3)` succeeded; the
original pair end still read 0 at the first refusal. Neither logged a
token-2 return or a display count above 1.

* AMD rejected an explicit bind of color pipeline #42 while clear pipeline
  #4 was still bound. The rejected wrapper belonged to the same device;
  its root differed from the active one.
* RTX reached the replacement vertex view after selecting the one-cell
  table. The supplied view was slot 0, 4,080 bytes, stride 24. Its entire
  range at chunk offset `0x1df10` fit the original live allocation. It
  shared the first view's backing and generation but named different bytes
  (first view at `0x1ef30`). The diagnostic did not read the table payload.

### Rebuild the RTX returned-target and RADV first-vertex fixtures

The qualified provider and fixtures use the native and fixture sources from
`b35465b8`. Run this from a Git checkout containing this recipe and that
commit; `--qualified` builds from a temporary archive of the pinned commit:

```bash
scarlett/tests/build-colour-recorder.sh --qualified "$INPUTS" "$XMEM" "$OUT"
```

`INPUTS` holds the seven private fixture files listed in the script; `XMEM`
names the matched `xmem.dll`, and `OUT` must be a new directory. The exact
build used x86-64 MinGW GCC/G++ 16.2.0, GNU binutils 2.46 and DXC
`1.9(dev;4950-b106a961)`. Put `dxc` on `PATH` or export `DXC` with its
absolute path. Python 3 compiles the project #96 shaders via DXC and checks
their hashes before writing the provider header. The script checks the
qualified SHA-256 of the shared `provider/d3d12_xs.dll`, the RTX `returned/`
executable and the RADV `amd-first/` executable. It does not launch them.

### Binding the returned display target

On staged `320a5879` with backend `0045`, the AMD fixture switched from
clear pipeline #4 to color pipeline #42 and installed its root only at
the following explicit setter. The RTX fixture bound the replacement
vertex buffer and verified the original source bytes after reuse and
fence completion. Both fixtures exited 0.

Both ordinary runs submitted the token-2 pair, queued `Signal(3)`
successfully and entered `PresentX(2)`. Neither logged a return or pair
retirement. Both exited 2; token 1 returned `S_OK`, the same-window count
advanced 0→1 and the decoded clients stayed blank.

| Adapter | First ordinary stop | Operands measured in a separate diagnostic |
| --- | --- | --- |
| AMD | First vertex buffer (`+180`, return `+32966`). | Slot 0, 4,080 bytes, stride 24. The full view fit at `0x1ef30` in the original live allocation. No prior VB was bound; the retail #42 bind was not observed. |
| RTX 3070 | Output target (`+190`, return `+33317`). | One color target, no depth target. Its key, slot, serial 14 and writer queue matched the returned primary, distinct from offscreen color/depth serials 119/121. Both vertex buffers were present. |

Each diagnostic sampled its own original token-2 end at 0 at refusal.
The RTX target selection is a request; it does not show a draw into the primary.

In the ordinary AMD log the factory for the textured composite pipeline
(#96) returned `S_OK` and a non-null wrapper. That line appeared after the
producer list closed. The log does not tie the wrapper to the request
the renderer selected, or show the renderer binding it. In an earlier,
separate AMD trace the renderer selected the request while its pipeline
was still null. That list contained the offscreen clear draw but no
composite draw into the primary.

### After the returned display target

On `34f3d0dd` with backend `0045`, both ordinary runs presented token 1
successfully and advanced the same-window count from 0 to 1. Both exited 2.

| Adapter | First ordinary stop | Token 2 |
| --- | --- | --- |
| AMD | Release-side timestamp (`+240`, return `+2482b37` from `+a1c42c`). | Issued; no rendering pair submission or `PresentX(2)` entry logged. |
| RTX | Resource barrier (`+490`, return `+32113` from `+332e1`). | Pair submitted and guest `Signal(3)` succeeded. `PresentX(2)` entered; no return or original pair-end retirement logged. |

Neither decoded client showed a recognizable image. A separate AMD
diagnostic recorded the first vertex view, then requested two whole-resource
transitions on the same offscreen color target: render target → shader
resource → render target. Neither transition named depth. The RTX diagnostic
rebound the returned primary (serial 14) as the sole render target, then
stopped at `+4d9f369` before a new table range was measured. In each
diagnostic the original token-2 end still read 0 at refusal.

The affected fixtures passed before the ordinary runs. Both exact fixtures
also passed on the reconciled source: AMD retained all 4,080 original
vertex bytes after address reuse, and RTX wrote through the private primary
view after reset with both offscreen depth/stencil planes unchanged.

### Rebinding the offscreen target and describing the next buffer

On staged `7e8929a6` and backend `0045`, both ordinary retail runs
submitted the token-2 rendering pair, queued guest `Signal(3)` and entered
`PresentX(2)`. Neither logged its return or the original batch end retiring.
Both exited 2. Only token 1 returned `S_OK`; the same-window counts advanced
from 0 to 1 and the decoded clients remained blank.

| Adapter | First ordinary stop | What the separate diagnostic measured |
| --- | --- | --- |
| AMD | Offscreen target bind (`+190`, return `+33317`) after recording both color transitions. | One RTV and one DSV, matching the original slots and serials 119/121. |
| RTX 3070 | Raw-buffer shader view (`device +230`, return `+4d9f369`) after binding the returned primary, serial 14. | The writer rejected a 128-byte, 32-element raw view with `E_INVALIDARG`. The full range fit one live backing at chunk offset `0x1de20`; the destination was aligned CPU descriptor slot 690. No shader read was observed. |

At refusal each diagnostic sampled its own token-2 end at 0 and public
recycler at 2. The private post fence read 2 on AMD and 0 on RTX; the
RTX gate-satisfaction message came later. These samples do not establish
a permanent stall. In the AMD fixture the two transitions named only the
original color target; both depth and stencil were unchanged after the fence.

### After the raw buffer view

Both affected fixtures passed on `5590dfb3` with XMem `8f74d625` and
backend `0045`. On AMD, the game-supplied color and depth slots could be
rebound while preserving the earlier views. On RTX the fenced shader test
read all 32 late-written words from the 128-byte view after replacement
and address reuse. The next 32 out-of-range reads returned zero.

Ordinary RTX then submitted the token-2 pair and queued guest `Signal(3)`.
`PresentX(2)` entered but logged no return or original-end retirement.
The recorder bound the returned primary (RTV serial 14) and reached the
shader-table setter (`+120`, title return `+32206`) before exiting 2.
Only `PresentX(1)` returned `S_OK`; the same-window count advanced from
0 to 1. Decoded black/white client frames and a cursor showed no game screen.

The first ordinary AMD attempt timed out (124) after Wine reported that
it failed to start Explorer. Its log contains no frame-token, presentation
or display-count event. Its recording was black. This attempt supplies
no evidence about where the retail renderer stops on AMD.

One AMD retry used the identical stage with 22,751,040 KiB available
under the launcher lock. The renderer switched to color pipeline #42,
installed its matching root, then stopped at the vertex-buffer setter
(`+180`, return `+32966`); the view operands were not captured. The
token-2 pair submitted, `Signal(3)` succeeded, and `PresentX(2)` entered
without a logged return or pair-end retirement. The run exited 2. Only
token 1 returned `S_OK`, the count advanced 0→1 and the client stayed blank.

Separate diagnostics measured two different shader-table requests:

| Adapter | State at `+120`, return `+32206` | Checked range in the active heap |
| --- | --- | --- |
| AMD | After the offscreen color/depth rebind, root index 0, setter epoch 0 | Handle `0x1145ef00`, one cell `[372600,372601)` |
| RTX | After binding the returned primary (serial 14), root index 0, prior slot 372600 at setter epoch 1 | Handle `0xa5f5dea0`, three cells `[372469,372472)` |

Neither setter succeeded. The range checks establish which slots fit,
not their descriptor contents or a draw. Both diagnostics sampled their
own original token-2 end at 0 when recording stopped.

The RTX fixture rebuilt for `POST120_SLOT=372469` exited 0. The selection
advanced the logical table epoch from 1 to 2. Fenced readback found the
returned primary black before and after selection, then blue after the
fixture's explicit clear through the retained RTV. The original clear
and both vertex buffers survived address reuse; both depth/stencil planes
were unchanged. This was a fixture-only setter, not an ordinary retail run.

One subsequent AMD diagnostic took the first-VB path without the explicit
#42 bind. It recorded the 4,080-byte buffer, then refused the one-cell
`+120` above. Its original end sampled 0 and the display count stopped at 1.
The vertex-buffer operands after the explicit #42 bind remain unmeasured.

The staged production provider `c2cc52b9` then passed the corrected RTX
fixture `b6208d70`: `+120` selected `[372469,372472)` at epoch 2 on the
same recording and end as `+190`. The primary stayed black across selection
and turned blue on the explicit clear. The vertex-buffer and depth/stencil
readbacks passed.

Ordinary retail ran once on AMD and then RTX with that same build. Both
stopped at `+120`, return `+32206` from `+3346d`, and exited 2. RTX logged
the returned `+190` bind but no `+120` commit. Both submitted token 2
(ends `f55cd20` and `77b95eb0` respectively) and accepted guest `Signal(3)`.
Only `PresentX(1)` returned `S_OK`: the display count advanced 0→1 and
neither run logged the return of `PresentX(2)` or sampled end retirement.
Private review found only blank black/white frames on AMD and the same
with a cursor on RTX.

A single RTX diagnostic logged the first failing `+120` condition on the
same segment and original end as the returned `+190` bind. The exact
37-operation sequence passed. With slot 372600 still selected at epoch 1,
`xs_held_table_inputs(&s->source, &s->previous_vb)` returned false before
the setter looked up the three-cell range. The R2 fixture passed that
check and reached epoch 2. The diagnostic did not distinguish the helper's
internal capture and span checks. It exited 2; only token 1 returned and
the display count stayed at 1.

The next locked RTX diagnostic isolated `live.holds != 3` on the clear.
The same recording passed the three-hold check at the returned `+190`
bind; at `+120` the count was five. The retained clear (100 bytes, two
holds when recorded) and first VB (4,080 bytes, three holds when recorded)
shared a placed resource, capture, node and generation. The live clear
still matched its saved node, generation, stamp and chunk offset `0x1ff80`.
The helper returned before reading the live VB. R2 keeps the live count at
three at this point. The diagnostic did not identify the two additional
holds. The display count stayed at 1 and the capture was blank apart from
the cursor.

A single RTX write trace identified both allocations on the same recorder
thread and generation between the returned `+190` and `+120`. Both went
through the same counter increment. The distinct producer return addresses
and observed allocation ranges were:

| Live holds | Producer return | Chunk-local range (end exclusive) |
| --- | --- | --- |
| 3→4 | `+4d9f234`, 128-byte RAW32 input | `[0x1de20,0x1dea0)` |
| 4→5 | `+4d9f540`, 480-byte dirty constant buffer | `[0x1dc40,0x1de20)` |

Neither range overlapped the clear or either vertex buffer. The trace
ended at `+120` after exactly two writes, and the existing three-hold
check refused the call. The original token-2 pair's end fence was
sampled at 1 before the watch armed. Despite that completion,
`PresentX(2)` had no logged return and the display count stayed at 1.
The earlier pair's completion does not establish that the later
recording released either allocation or that a draw consumed it.

CPU tests of the staged `c2cc52b9` checks isolated two ways that this
state is refused. With matching registration and the expected saved clear
and first-VB spans, the held-input check returns on its first live read
when the count is five. Independently, the read-only returned-output
check accepts a pending fence (0) but rejects a completed fence (1).
Using its non-null destination mode at 1 would actually copy a descriptor.
The logical setter needs a validation-only test for the completed pair
and evidence for both new allocations.

The RTX baseline fixture `32f0ac27` reproduced this combination on
unchanged provider `c2cc52b9`. The RAW32 rewrite advanced its descriptor
serial from 5 to 6, the fixture published the 480-byte constant buffer,
and the matching earlier end fence read 1. The setter refused without
changing the prior selection. The unrelated-fifth-allocation control
also refused with the RAW32 record still live. Checked submissions
remained denied; the primary stayed black until the fixture cleared it
blue. Exit 0 confirms the baseline refusal, not acceptance of five holds.

With staged provider `55d559c9`, RTX fixture `ff62fe95` exited 0:
it selected `[372469,372472)` at epoch 2 after validating the RAW32
write, the 480-byte constant-buffer publication and the matching
completed end. An unrelated fifth allocation with a live RAW32 record
still refused. Checked submissions stayed denied, the primary stayed
black until the explicit blue clear, and both DSV planes were unchanged.

Ordinary retail ran once on AMD and then RTX with that same provider.
Both stopped at `+120`, return `+32206` from `+3346d`, and exited 2.
RTX recorded the RAW32 replacement (serial 138→149 at four holds)
after the returned `+190`, but no successful table selection.
Neither run sampled original-end retirement. Only token 1 returned
`S_OK`; both entered `PresentX(2)` with the display count still at 1.
Private pixel review found black on AMD and black/white with a cursor
on RTX.

One RTX diagnostic on the same provider source logged the first failed
check after the RAW32 replacement (serial 134→142, four holds). The
original end fence read 0 both on entry to `+120` and at the check that
requires 1. The passing fixture read 1 and selected `[372469,372472)`.
The recording stopped before validating the constant-buffer publication.
The samples do not establish when the fence later completed. Exit was 2;
only token 1 returned, count stayed at 1, and the six unique decoded
images showed blank black/white frames and a cursor.

Stage `c16862ab` allows the logical selection with the original fence
at 0 or 1, after the same queue, generation and allocation checks.
RTX fixture `eed25eb6` held the fence at 0 through selection and both
submission refusals. An unrelated fifth allocation with the RAW32 record
still live was rejected. The fixture opened the gate only after these
checks; the primary readback was black/black/blue and both DSV planes
remained unchanged.

On the identical ordinary build, RTX selected `[372469,372472)` at epoch
2 with original end `77b5e4e0` sampled at 0. Its next refusal was the
`+490` resource barrier, return `+32113` from `+24847a8`. Its operands
were not captured. AMD stopped at `+120`, return `+32206` from `+3346d`.
Both runs exited 2; both entered `PresentX(2)` without a logged return,
the display count stayed at 1, and neither capture showed game content.

A single locked RTX diagnostic captured `+490/+32113` after the
three-cell selection. One texture barrier covered all subresources of
the returned primary (RTV serial 14). Sync was `0x40→0`, access
`0x8→0x80000000`, layout `0x2→0x1f` (guest release), flags 0.
The resource matched neither offscreen attachment nor token 2's
serial-17 primary. The refusing list had a different face and segment
from the `+120` recorder, no original end of its own in the trace,
and returned `E_ACCESSDENIED`. Its face pointer matched the earlier
submitted successor, which does not establish ownership of this release.
Only token 1 returned; the count stayed at 1.

A follow-up RTX diagnostic traced the clock and offer list at the refused
release. The serial-14 producer closed successfully with table epoch 2
but offered no receipt. Token 2 was still the latest issued token in
clock epoch 1 on host queue `8c1f30`, and its batch end was already
assigned to the serial-17 pair. The sole offer was that pair's claimed
receipt. The serial-14 lookup therefore had zero matching offers and
returned no eligible queue or token. `PresentX(2)` did not log a return;
the 211 decoded client images contained only black or white.

### Cross-build the guarded token-2 checkpoint

The paired build also recognizes the alternate bank-2 WB head/tail at
`419aeb8/419aeb0` after checking the live owner record through the same
section. On the fresh bank-3 AMD list, the `+238` post from `+4ed46aa`
writes `29a5ee15` at `+9c`; the unsigned `+250` wait from `+2484b12`
splits after op six. The RTX alias post has already split after op five.
The native and alias cases keep the original control backing through the wait.

The RTX alias and AMD native wait paths can reacquire the returned primary at `+32113`.
Token 1 must have completed the release and COMMON-restoring copy and
flipped ring 0→1. Recording checks the owner, resource, unique slice and
RTV slot/serial. Submission checks and consumes the writer epoch under
the owner lock on the original queue; the private fence guards retirement.
The `TEST_REACQUIRE` ordered-present fixture checks an actual cleared pixel
after the held wait, plus pre-flip, wrong queue, stale, alias and invalid
barrier rejection. `TEST_NATIVE_THIRD_WAIT` and `TEST_RTX_THIRD_WAIT`
select the corresponding synthetic `test-marker-bank.c` cases.

The submitted pair's last offscreen RT→SR transition supplies the original
resource and descriptor generation. On the measured RTX recording, the
next SR→RT uses that image; submission checks the original end fence and
writer queue together with the primary proof. When the producer carried
depth, `+190` from `+33317` must also name the original distinct depth
registration, slot and descriptor serial. The consumer freezes the two
views and retains both heaps and registrations through its own fence.
The color barrier does not transition depth. Wrong owner, queue, fence,
serial, alias, intervening use or replay cannot qualify the pair.
`tests/build-ordered-display.sh DIR restore-radv` builds the split-six
primary fixture; `restore-rtx` checks depth `.375`, stencil `69`, a red
offscreen pixel and a black primary after releasing the source DSV heap
and list. `tests/test-restore-ticket.sh` exercises stale proofs on CPU.

The default bridge has no pair recorder or executor. `SCARLETT_PAIR_SUBMIT`
enables recording, inventory and guest Signal-3 tracing. Ordinary two-list
dispatch also requires `SCARLETT_GUARDED_PAIR_DISPATCH`; `PAIR_INVENTORY_ONLY`
vetoes dispatch even with both flags. `TEST_PAIR_FIXTURE` enables dispatch
inside the owned sealed fixture, not in a separately built provider.

From the repository root, in Bash with x86-64 MinGW GCC/G++, set `XMEM` to
the absolute path of the matched `xmem.dll` and `INPUTS` to the private
clear/colour input directory (the seven files and hashes in
`scarlett/tests/build-colour-recorder.sh`). The commands write a fresh scratch
directory and do not launch or install anything:

```bash
set -eu
n=$(realpath scarlett/native)
out=$(mktemp -d); echo "Guarded build: $out"; cd "$out"
mkdir obj bridge sealed alt clear bank bank3 rtx-post wb-query
cc=x86_64-w64-mingw32-gcc
f=(-Os -Wall -Wextra -Werror -DSCARLETT_DEVICE_BOOTSTRAP -DSCARLETT_STAGED_PLACED
   -DSCARLETT_EXPERIMENTAL_TAGGED_BATCH -DSCARLETT_EXPERIMENTAL_PRIMARY_RELEASE
   -DSCARLETT_EXPERIMENTAL_DIRECT_WAIT -DSCARLETT_EXPERIMENTAL_BRIDGE
   -DSCARLETT_PAIR_SUBMIT -DSCARLETT_GUARDED_PAIR_DISPATCH)
for s in d3d12_device d3d12_vm queue/queue; do
    "$cc" "${f[@]}" -c "$n/$s.c" -o "obj/${s##*/}.o"
done
"$cc" -shared -s -Wl,--no-insert-timestamp,--image-base,0x24d560000 \
    obj/{d3d12_device,queue,d3d12_vm}.o "$n/d3d12_xs.def" -ld3d12 -ldxgi -o bridge/d3d12_xs.dll
cp "$XMEM" bridge/xmem.dll
for t in bank bank3 rtx-post; do
    case $t in
        bank) extra=(-DTEST_THIRD_PREFIX);;
        bank3) extra=(-DTEST_THIRD_PREFIX -DTEST_BANK3_WB);;
        rtx-post) extra=(-DTEST_RTX_THIRD_POST);;
    esac
    "$cc" "${f[@]}" "${extra[@]}" -static -Wl,--no-insert-timestamp \
        "$n/queue/test-marker-bank.c" obj/{d3d12_device,d3d12_vm}.o \
        -ld3d12 -ldxgi -ldxguid -o "$t/game_scarlett_master.exe"
done
"$cc" -Os -Wall -Wextra -Werror -DSCARLETT_EXPERIMENTAL_PRIMARY_RELEASE \
    -static -Wl,--no-insert-timestamp "$n/queue/test-successor-query.c" \
    -ld3d12 -ldxguid -o wb-query/game_scarlett_master.exe
for t in bank bank3 rtx-post wb-query; do cp bridge/xmem.dll "$t/"; done
f+=(-DTEST_DEFER_RELEASE -DTEST_DISTINCT_PRIMARY -DTEST_SECOND_VB -DTEST_LATE_COLOUR -DTEST_INLINE_B27)
"$cc" "${f[@]}" -c "$n/test-clear-device.c" -o obj/test-device.o
for t in clear sealed alt; do
    extra=()
    if [[ $t != clear ]]; then extra+=(-DTEST_PAIR_FIXTURE); fi
    if [[ $t == alt ]]; then extra+=(-DTEST_RTX_Q0_SECOND_PAIR); fi
    "$cc" "${f[@]}" "${extra[@]}" -c "$n/queue/test-clear4-bind.c" -o "obj/$t.o"
    x86_64-w64-mingw32-g++ -std=c++17 -static -Wl,--no-insert-timestamp "${f[@]}" "${extra[@]}" \
        "$n/test-clear-pipeline.cpp" "obj/$t.o" obj/{test-device,d3d12_vm}.o bridge/xmem.dll \
        -lbcrypt -ld3d12 -ldxgi -ldxguid -o "$t/game_scarlett_master.exe"
    cp "$INPUTS"/{stream,vs,ps,emitter,colour-stream,colour-VS,colour-PS}.bin bridge/xmem.dll "$t/"
done
```

`sealed` checks the original pair through the batch-end fence; `clear` is the
clear/DSV control on both adapters. `alt` checks the second measured RTX q0
pair with independent mappings, queries and destinations. On RADV, `bank`
checks the `+0x98=2` write across VA reuse and rejects a timestamp before
the heaps are rebound. `bank3` then checks the exact bank-3/index-1 owner
through a typed alias of the retained WB section. Only the original
`+0x419f258` qword may change, within the queue calibration interval;
`wb-query` separately checks the original pair's WB successor.
On RTX, `rtx-post` checks the adjacent `+0x9c=0x29a5ee15` post in the
original `+0x98=2` prefix generation; the later wait is still refused.

The retail observations above belong to `cf68be0e`. The bank-3 and RTX
post fixtures passed with combined staged provider `4da78f7b`. Those
fixture results do not establish batch-end completion or a second flip,
and do not qualify freshly cross-built binaries. On the reconciled source,
the sealed-pair fixtures on both adapters completed at their original
ends; the AMD bank-3 and RTX third-post fixtures also passed.
