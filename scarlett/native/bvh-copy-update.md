# CopyForUpdate (ninth static utility selection)

The `XDXR CopyForUpdate` PSO uses `CopyForBVHUpdate`, builder RVA `+b5be0`,
file offset `0xb4fe0`, size `0x126c`. The exact-digest factory submits the
original bytecode. Its only resources are raw SRV `t0`, raw UAV `u0` and two
DWORDs at `b0`: the thread count used for the loop stride and the bounds-reset flag.

The private recorder accepts a coherent CPU snapshot of the source prefix.
It checks that the prefix is exactly `64 * (1 + src_dword[1])` bytes, with
a 1 MiB fixture limit, and retains a private upload until retirement. The
destination view covers only that prefix. A single 32-thread group copies
16 bytes per lane with a 512-byte stride; when requested, lane zero resets
the six scene-bound DWORDs at bytes 16–39 to three `+FLT_MAX` and three
`-FLT_MAX` values.

`test-bvh-copy-update.cpp` checks 640-byte copies in both modes and a
header-only copy against 384 fenced destination words on RADV and NVIDIA.
The test changes the original source before execution and checks canaries,
short output, a wrapping header count, and wrong shader bytes. This qualifies
the private dispatch, not a guest dispatch or a retail constructor run.
