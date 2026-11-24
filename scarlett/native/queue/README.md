# Direct queue and shared fence adapters

`xs_direct_queue` creates a normal-priority Direct queue on node 1.
`xs_shared_fence` creates a zero-initialized `D3D12_FENCE_FLAG_SHARED` fence on
the supplied desktop device. Each returns one owned `IUnknown` reference; the
objects have independent identities and release their own vkd3d objects. Only
`IID_IUnknown` succeeds in `QueryInterface`. These internal constructors have
no DLL exports and are not wired into the Xbox device factory.

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

**Retail blocker:** the title uses a different CPU wait path. At `0x4fb0a75`
it takes a queue bookkeeping object's fence, then at `0x4fb0aec` calls
`fence[+0x60]`. It dereferences the returned **address**, repeatedly reads its
64-bit contents and uses `MONITORX`/`MWAITX` (`0x4fb0d21`–`0x4fb0d55`).
A completed-value snapshot or event cannot be returned as this address.
Exposing the fence requires evidence of the address's lifetime, update and
device-loss semantics, plus a coherent implementation. Exposing the queue
also requires the additional calls (including `+0x100` and `+0x118` in the
submission worker), their argument layouts and child-object contracts.

## Scratch GPU test

From the repository root on the NixOS test host:

```sh
src="$PWD/scarlett/native/queue"
work=$(mktemp -d)
mkdir "$work/game"
x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -Werror \
  "$src/queue.c" "$src/test.c" -ld3d12 -o "$work/game/game_scarlett_master.exe"
TMPDIR="$work" "$src/../../test-launch-nixos.sh" "$work/game"
grep -F 'PASS queue adapter:' "$work"/scarlett-run.*/logs/steam-999001.log
```

Passed on GeForce RTX 3070, vkd3d-proton 3.0.0. The test calls the queue
through the two verified slots, checks distinct COM identities and rejects a
foreign fence. A submitted GPU wait gates the subsequent queue signal: the
completion event times out while only the low 32 bits match, then fires after
the CPU signals the full 64-bit value. Releasing both fences and the queue
leaves no test-owned reference on the host device. Cross-process sharing is
not exercised.
