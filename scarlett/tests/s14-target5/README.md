# S14 target-5 RTX fixtures (cross-build only)

From the repository root, use the output directory printed by the [bank3 recipe](../s14-bank3/build.sh). Each command builds in a fresh directory. The original qualification used `x86_64-w64-mingw32-gcc` 16.2.0. The build checks the bank3 control hashes, the source preimages and the final SHA-256 outputs. It needs no runtime or title assets.

```sh
bash scarlett/tests/s14-bank3/build.sh "$GUARDED_STAGE"
# Set BANK3_OUTPUT to the "Build directory" from that command.
bash scarlett/tests/s14-target5/build.sh fence "$BANK3_OUTPUT"
bash scarlett/tests/s14-target5/build.sh dispatch "$BANK3_OUTPUT"
```

| Recipe | Qualified output | What the recorded RTX run established |
| --- | --- | --- |
| `fence` | `rtx/game_scarlett_master.exe` (`c3d49f74`) | A fixture callback submitted the closed head and tail and queued signal 5. While gated, the recycler still read 4. The independent readback found green serial 14, red serial 17, ordered timestamps and intact guards. |
| `dispatch` | `fixture/game_scarlett_master.exe` (`49441389`), `on/d3d12_xs.dll` (`df32e6f1`) | The fixture's embedded checked dispatcher submitted the split producer prefix, producer tail and recorded successor under a fresh token-3 batch fence. Both list faces reset while gated; the allocator stayed pinned until retirement. The publisher, outside the dispatcher, queued 5. Pixels, all three source sentinels, timestamps and guards passed. |

`dispatch` first reproduces `fence`, then applies the observer and dispatcher deltas to the private source copy. It also rebuilds the flag-off observer DLL (`041095b6`) before enabling `XS_RTX_S14_JOB`. The runtime XMem hash in both recorded runs was `8f74d6255dc72fe7cf31c4ffcc0898299a86fa561101adde53bec550b7fcc5e9`.

**Seed limits.** The producer profile, prior serial-17 key, capture identities, source spans, publication records/book and writer-clock context are test-owned. The dispatcher fixture exercises native recording, Close, checked submission and retirement; it does not replay all 37 producer opcodes or establish title eligibility.

**Completion constraint.** Both qualified paths require the prior publisher to have signalled **and completed 4** at admission. The dispatcher additionally checks book high-water ≥5. A queued 4 sampled at 3 does not qualify; the queued-4 experiment is separate.
