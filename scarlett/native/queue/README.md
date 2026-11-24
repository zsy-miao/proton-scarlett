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
