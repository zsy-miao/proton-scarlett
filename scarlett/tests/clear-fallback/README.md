# Pinned clear writer → offscreen readback

From a scratch checkout on the NixOS GPU test host:

```sh
scarlett/tests/clear-fallback/run.sh /path/to/original/game_scarlett_master.exe \
  /path/to/clear-stream-capture /path/to/test-proton
```

The capture is the same `stream.bin`, `vs.bin`, `ps.bin` used by `native/test-clear-pipeline.cpp`. The runner writes only in its new temp directory. `prepare.py` checks the original EXE digest; the fixture separately checks the extracted ranges' digest before marking code pages executable. Neither executable fragments nor capture data belong in the patch.

This single-threaded fixture uses controlled shader, view, chunk and explicit-request bookkeeping. It enters the complete original clear routine at RVA `122630`. The title allocates the chunk, writes the two packed headers and three XYZ/RGBA vertices, derives the alias VA, flushes state, draws and releases the chunk. Its list pointer is an isolated capacity-zero receiver with a trailing guard page. The nine typed fallback calls must be topology, PSO, root, RTV, depth bounds, scissor, viewport, SRV, draw. Any missing or unexpected argument/order rejects the transaction before recording; unknown methods trap. The guest PSO is the real output of the qualified `+2b0` factory. No fast-path packet is decoded here.

The authority supplied by the fixture is a **retained** device, original PSO/root pair, registered RTV heap/slot/serial/texture, and an allocation-generation identity, base, offset and upload resource. Slot serial, tag and backing, device ownership and the live buffer binding are checked together before any draw is emitted. An ordinary committed RGBA8 offscreen texture is registered only for the public descriptor writer in this test. The source is CPU-owned mapped UPLOAD memory and already synchronized with this thread. The checked 100 bytes are copied to a private immutable upload, so producer reuse cannot affect execution. Depth bounds 0/1 require no host command for this exact depth-disabled PSO. The fixture supplies a recording Direct list with target already in `RENDER_TARGET`; it owns transition and readback ordering.

While the zero-token, flags-zero public submit is held behind a fence, the test changes the original source bytes, unregisters that generation, registers a replacement, rewrites the original RTV slot, and drops the producer bindings. It compares every pixel of both targets and checks retained identities after retirement. The tested input RGBA is `(1/8,3/8,5/8,7/8)`, expected bytes `20 60 9f df`; no per-driver allowances.

A retail hookup still needs a trusted generation lease for the actual guest chunk/alias, recorded texture transitions, the complete fresh-list state protocol and qualified batch/token semantics. The fixed test receiver is never returned by the production list factory. This patch changes neither that factory nor queue submission acceptance.
