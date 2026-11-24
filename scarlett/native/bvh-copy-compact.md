# CopyAndCompactBVH

`XDXR CopyAndCompact` uses the original `0x1134`-byte container at builder
RVA `0x25700`, file offset `0x24b00`. The factory reuses the device's
`CopyShaders` root (`u0`, `u1`, 16 DWORDs at `b0`).

For a representable size, the shader copies `(src_dword[1] + 1) * 64` bytes.
Each lane starts at `16 * dispatchThreadID.x` and advances by `16 * b0.x`;
lane zero also writes the original size field to destination byte 4.
`test-bvh-copy-compact.cpp` checks a header alone, a 35-block copy with two
32-lane groups, and a one-group dispatch with the same 64-lane stride.
The last case must leave alternating 512-byte stripes untouched. All source,
destination and guard words passed fenced readback on RADV and NVIDIA.

This qualifies host PSO creation and private dispatch. The root UAVs have
no view bounds. A checked recorder would need the exact header visible at
execution, both buffer capacities, and non-wrapping 32-bit size and loop
arithmetic; a CPU count alongside a mutable source UAV does not suffice.
