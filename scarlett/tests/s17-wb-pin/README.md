# AMD serial-17 write-back timestamps

Offline replay from the retained `bf6d6155` combined build directory:

```sh
bash scarlett/tests/s17-wb-pin/build.sh "$COMBINED_OUTPUT"
```

The recipe verifies its inputs, patches a private copy and cross-builds with x86_64 MinGW GCC/G++. With `XS_AMD_S17_WB` off the provider must match the parent byte-for-byte. With it on, `outputs.sha256` pins the qualified provider (`f2d6373a`) and sealed-pair fixture (`bca11c16`). The script runs neither.

**Scope.** The measured RADV `1002:164e` case is a token-2, serial-17, generation-0 direct pair. In bank 2, packed indices `1`, Q0 at `+0x419acc8` and Q1 at `+0x419acc0` belong to the same write-back section (owner record `+0x3fb3050`, flags `0x20001`). The producer pins its original allocator, two-slot query heap and destination across Close. After validating the receipt and live owner, the successor records Q1 into that same resource. The fixture rejects stale allocator generations, a different allocator, serial, owner or section, and mismatched flags, bank or indices.

**Qualified fixture (exit 0).** A separate view keeps the original bytes readable after the test replaces the exact 64-KiB address window. Before releasing the gate, original Q0 is nonzero and Q1 is zero. After retirement, Q0 must be unchanged and Q1 strictly later and within the GPU clock calibration bounds. Every original byte outside `[0xacc0,0xacd0)` must remain `0xad`; every replacement byte must remain `0x49`. The recorded Q0/Q1 were `7619708046679` and `7619708197514`; these are observations, not expected constants.

The resources and publication records are test-owned. This qualifies the measured AMD pair only; it does not establish RTX CPU-mapping behavior, other layouts, or ordinary title presentation.
