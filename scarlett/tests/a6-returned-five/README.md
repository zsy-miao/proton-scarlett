# Split-six AMD logical table selection (offline replay)

From the repository root:

```sh
bash scarlett/tests/a6-returned-five/build.sh "$QUALIFIED_47002876_STAGE"
```

Use the qualified split-six OM native source tree with the attributed-five helpers, plus the three link/build inputs listed in `preimages.sha256`. The canonical OM sources alone lack those helpers. The script checks the preimages, patches a fresh copy, and verifies all four [PE hashes](outputs.sha256). The replay used MinGW-w64 GCC/G++ 16.2.0 and GNU ld 2.46. The executable name is the fixture's required module name; the retail executable and shader blobs are not build inputs.

`XS_AMD_RETURNED_FIVE` admits one additional case: RADV `1002:164e`, split six, original end still pending, op 37, index 0. The observed `+32206` setter resolved to slot 372469 in the active heap. The same recording's bounds, committed RAW32 replacement and class-2 allocation validated a 3→4→5 hold witness. Only the logical selection changes, from `372600/1/1` to `372469/3/2` (slot/cells/epoch). A count alone cannot authorize the setter or a consuming submit.

Both fixture revisions link the same candidate. The historical `d0993a7c` run selected once, then exited 1 at compound assertion 630; its matrix was withheld. The `eff9822d` correction separates the two controls: at five live holds and tick 0 the stale bind returned `E_NOTIMPL`; at three restored holds and tick 1 it returned `E_ACCESSDENIED`. Both left the output null and the bind count unchanged. The corrected fixture passed; ordinary AMD stopped before reaching this setter and RTX selected the three cells. See the [queue evidence record](../../native/queue/README.md). Rebuilding either fixture does not run it.
