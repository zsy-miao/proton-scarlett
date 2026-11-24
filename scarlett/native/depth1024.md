## 1024-square D32S8 owner sizing on RTX

After the two 2048-square BGRA targets, +344182 queries one 1024×1024 R32G8X24_TYPELESS 2D image, one layer/mip/sample, default layout/alignment, quality zero, flags 2. +c8 reports 8 MiB at 64 KiB alignment. +164dee receives the result; the compact format is 20 (D32_FLOAT_S8X24_UINT), with owner constructor flags 0x48. Owner +8 is 0x100000 elements, stride 8. +1df0080 publishes the owner at list-node +0x10. The next same-thread +c8 is 994×624 BGRA8, flags 0xc00005. At that call the list tail contained the exact owner, whose resource field +0x20 was null. In a diagnostic run that answered additional sizing probes it remained null through list growth from 3 to 11 entries. No 1024-target +360, +428, +3f8, guest clear value, guest view or first use was established. Only +c8 is enabled.

`tests/depth1024.sh` checks that exact query and its rejection controls. On a native reference placement at a 64 KiB offset, it creates a D32_FLOAT_S8X24_UINT DSV and R32_FLOAT_X8X24_TYPELESS / X32_TYPELESS_G8X24_UINT plane SRVs. It clears in DEPTH_WRITE, reads in NON_PIXEL_SHADER_RESOURCE, and copies both complete subresources in COPY_SOURCE. A comparison sampler tests 0.5 against 0.25 and 0.75; stencil values 0x31 and 0xa5 select between outputs. The test does not assert a retail heap offset or retail view state.

RADV query control: +c8 and the host agree on 0x520000 bytes at 0x10000 alignment. Both full planes and all four shader probes matched the RTX fixture. This verifies the desktop DSV/SRV path, not a title placement.

## 994×624 depth owner

`+344188` returns the size of one 994×624 R32G8X24_TYPELESS texture, one layer/mip/sample, alignment/layout/quality zero and flags exactly `0x800002`. Only the low depth/stencil bit maps to a desktop resource flag. The guarded `+c8` query agrees with desktop allocation info: RTX `0x4f0000`, RADV `0x350000`, both at `0x10000` alignment. Swapping the flags of this record and the 1024-square record is rejected.

On RTX, `+164dee` receives the result with compact format 20 (D32_FLOAT_S8X24_UINT) and constructor flags `0x48`. The group stores the first title object at `+18` (`+1da8359`), then passes its owner pointer at object `+8` to a second constructor and stores its result at group `+20` (`+1da8386`). Both distinct objects reference the same owner. At the next query the owner is the list tail, its count is `0x9e000` at stride 8, and its resource field `+20` is null. No matching rich query, placement or committed resource was observed.

The GPU fixture uses a native reference placement with a D32_FLOAT_S8X24_UINT DSV, R32_FLOAT_X8X24_TYPELESS depth SRV and X32_TYPELESS_G8X24_UINT stencil SRV. It verifies all 620256 texels in both planes and four comparison/selection samples on each adapter. Those native views are test evidence, not a decode of the two title objects.
