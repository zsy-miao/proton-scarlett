# Control-page translation proof (synthetic, native x64)

`control.hpp` is a scratch dispatch boundary for the exact tuple
`(NULL, 65536, 0x20401000, 0x283, 0x42404)`. No public API is exported.
It requires the **test-only** creator in the release agent's Wine build,
`0x5354ff01`. Do not ship that creator. `release-only.patch` by itself
does not mark views or enable this prototype.

Role offsets are inputs from a trusted synthetic allocator: one aligned
qword and 54 nonoverlapping DWORDs. The test uses `0x120` and `0x324`;
these are deliberately **not** claims about the game layout. Each recording
acquires the current section under Wine's VM lock, checks base, span and byte
offset, and compares kernel object identity with the recorded incarnation.
A packet owns that incarnation and its GPU binding until host fence retirement.
The wait samples an independent CPU alias. The marker uses the imported
resource's GPU VA + checked offset. It never submits a title VA. CPU writes
and GPU markers share storage; there is no deferred bank copy-back.

The synthetic gate takes the unsigned 64-bit `>=` interpretation of mode
`(7,1)` on a monotonic CPU-only producer. After prefix retirement an observer
samples the cell without publisher notification. Retirements and cancellation
are serialized on a single private direct queue. Timeout is an explicit
failed submission: the private gate drains but the suffix does not execute.
The host write model uses direct-list `WriteBufferImmediate`, default mode;
it accepts only `(known DWORD address, value, 0, 0)`.

## Reproduce

Apply `01-control.patch` then `02-proof.patch` to a **scratch** Proton folder
containing `scarlett/native/xmem/gpu_buffer.hpp`. There are no main build changes.

```sh
bash scarlett/tests/control-translation/run.sh /tmp/opencode/ctrl64-release.qqXNVHSu/tool
bash scarlett/tests/control-translation/run.sh "$HOME/.local/share/Steam/compatibilitytools.d/scarlett-proton" unqualified
```

To use the supplied overlay directly set `SCARLETT_ROOT` to the Proton repo.
The runner creates a synthetic EXE in a new temp directory. Both release-patched
Wine files (kernelbase and ntdll) must match for the positive proof. Logs:

- `/tmp/opencode/control-proof.1Q8iJB69/scarlett-run.DHDUpOIO/logs/steam-999001.log`
- `/tmp/opencode/control-proof.AFRC4UqV/scarlett-run.YnRj4Zy1/logs/steam-999001.log`

## Help needed before integration

1. **Title/ABI:** qualify the actual command-list interface, slot `+0x250`
   predicate, flag `1` engine/scope, and slot `+0x238` stage/visibility and
   argument widths. Do not assume host default write stage means console
   top/bottom of pipe. Supply trusted runtime qword/bank registration, or
   prove fixed layout for this exact provider. Fence creation precedes the
   direct suballocations and may advance the bump pointer.
2. **Queue:** supply a recorder/submission owner that preserves the wait in the
   middle of a command list, engine overlap, multiple lists and queues, reset,
   repeated submissions, timeout/device loss and dependencies. The proof
   closes lists in advance on a private direct queue. Host device errors or
   watchdog expiry terminate the test; they are not production recovery.
3. **XMem/Wine:** only publish a translated control view once all consumers
   are owned, including the temporary allocator used by shared-fence creation.
   Confirm CPU WC and query/protection semantics or show why the full exposed
   contract is equivalent. This synthetic CPU-backed mark is write-back and
   is not proof of those properties. Use the exact-size release hook only for
   authenticated creator provenance; carry backing identity/ownership through
   retirement. No generic WB/WC buffer fallback or registration by size alone.

Unknown operations must reject the packet before partial submission. Until
these conditions are established the production request should continue
returning `ERROR_NOT_SUPPORTED`.
