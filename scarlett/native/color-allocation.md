# Device +0xc8: startup color texture allocation info

`344182` calls with RCX = hidden 16-byte output, RDX = device, R8D = node
mask 1, R9D = count 1, and the description as the fifth argument. `344188`
copies both qwords to the caller; `344190` requires the size's high DWORD
to be zero. `51fbdb5` and `51fbdbb` forward the low DWORDs of size (bytes)
and alignment (bytes); `164ec2`–`164ece` and `164e2d` divide the size by
bytes per element (four here) for the texture owner's initialization.
There is no descriptor-slot write or resource pointer in this ABI. The
caller sets alignment zero at `344157` on the default-alignment path.

The trace `scarlett-run.49PD1ImQ/logs/steam-999001.log:3836` records one
3840×2160 `R8G8B8A8_UNORM` Texture2D, depth/mips/samples 1, quality 0,
unknown layout, flags `0x300005`. `4ecc9df` extracts the low three
capability bits; `4ecca2b`/`4ecca7e` select and add `0x300000` on a
format/options-dependent path. The low `0x5` translates to render-target
and unordered-access capability. The individual console high bits have
no established desktop meaning. For this exact size query the translator
asks the host about an RT+UAV texture using its native layout. This does
not assert binary layout compatibility or authorize use of raw guest tiles.
Only the 52-byte prefix ending at flags is consumed.

`test-allocation-info.cpp` calls the public slot with a guarded input and
checks both qwords against the native query. It creates a real heap of
exactly that size/alignment, places the matching texture at offset zero,
clears it, and verifies the bottom-right texel after a GPU fence.
