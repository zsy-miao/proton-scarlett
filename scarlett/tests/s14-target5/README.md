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

**Completion constraint.** `fence` and `dispatch` require the prior publisher to have signalled **and completed 4** at admission. The dispatcher additionally checks book high-water ≥5.

### Queued 4 and the two timestamp layouts

```sh
bash scarlett/tests/s14-target5/build.sh queued4 "$BANK3_OUTPUT"
bash scarlett/tests/s14-target5/build.sh layouts "$COMBINED_OUTPUT"
```

`queued4` extends `dispatch`. The flag-off DLL matches `df32e6f1`. The RTX run of EXE `45c41e82` with DLL `4998adf0` exited 0: the writer had queued 4, the fence still read 3, and the book high-water was 5. The fresh batch end retired and the original spans and timestamps were verified before signal 5.

`layouts` starts from the retained combined build output (provider `bf6d6155`, RTX fixture `ecf6c002`; see `combined-preimages.sha256`). It builds the flag-off control and the two-layout provider `a694bc9d` once, then reproduces both fixture revisions:

| Fixture | Recorded RTX result on `a694bc9d` |
| --- | --- |
| `original/game_scarlett_master.exe` (`ccc684b5`) | Exit 1. The alternate tail/head (`0x419f250/0x419f258`) retired and passed readback after address reuse. The next setup expected token 1 but received 4 (`S14 FAIL 134: token==i`), before testing the original layout. |
| `fixture/game_scarlett_master.exe` (`baeccf0b`) | Exit 0. Each layout ran in a fresh child process with tokens 1, 2, 3. The alternate pair and the original tail/head (`0x419f6d0/0x419f6d8`) each passed ordered timestamp, fresh retirement fence, green14/red17 and guard checks. |

The clock correction changes only the fixture entry point. Both commands cross-build and verify hashes without running either executable.

### Independent RTX component recipes

Use the output of the `layouts` command above for both builds:

```sh
bash scarlett/tests/s14-target5/build.sh indexed "$LAYOUTS_OUTPUT"
bash scarlett/tests/s14-target5/build.sh sibling "$LAYOUTS_OUTPUT"
```

Both modes check the `baeccf0b` fixture preimage and rebuild the flag-off DLL byte-for-byte against `a694bc9d`. Each enables just its own change:

| Mode | Provider / fixture | Recorded RTX result, both layouts |
| --- | --- | --- |
| `indexed` | `a306bf6f` / `91e3473b` | The indexed producer with alias marker value 2 qualified; six malformed producers per layout stayed generic. Ordered timestamps and guarded pixel readback passed. |
| `sibling` | `07f11f9f` / `e27a80de` | Two disjoint imported surfaces exercised A→B→A. The fixture **simulated B's successful WSI return callback**; it performed no live flip. The generic epoch expired. Missing return, wrong fence/depth serial/queue, overlap and alias were rejected; the valid submission retired and both colors passed. |

These hashes identify the standalone components. The merged owner's provider `d432e1ba` and RTX fixture `8797be40` have a separate build and result record.
