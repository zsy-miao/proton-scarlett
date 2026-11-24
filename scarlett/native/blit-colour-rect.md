# Slot 42 rectangle geometry stage

`blit_colour_rect_gs` (in `blit_colour_rect_dxil.h`) runs after the host
`blit_colour.hlsl` VS. In a D3D12 graphics PSO the GS bytecode is
`{blit_colour_rect_gs, sizeof blit_colour_rect_gs}`; for a PSO stream use
the GS bytecode subobject. Keep the **host PSO** topology type TRIANGLE
and the **host list** topology TRIANGLELIST. Keep the original live VB
view and `DrawInstanced(3,1,firstVertex,0)`. No CPU read, rebase,
expanded VB, or second draw. No GS resources or root changes.

Only select this PSO variant for the mode-15 `im:CS_BLIT_COLOUR` slot-42
pair, full fixed-state/IA qualification and owned b27-space1 root, with
guest topology 17 on the matching draw from the `+124760` producer
(parent return `+124a7a`). Retain the actual GPU VB, table, RTV/DSV and
resource states using the recorder's ownership rules. The draw span
from firstVertex must fit the bound slot-0, 24-byte stride vertex view.
The PSO stream's topology type 19 alone does not establish any of these.
Do not attach this stage to clear #4, a general triangle PSO, or the
later transfer/composite. If both ordinary and rectangle draws need the
same translated shader pair, use separate host PSOs. This patch does
not change the guest recorder's admission logic.

The observed clip-space TL, BL, BR inputs pass the exact-position guard;
another order/shape emits nothing. With constant W=1, TR is TL plus
the offset BR−BL for position and both smooth varyings. Emitting two
independent triangles, each starting at TL, preserves the winding and
avoids strip-parity and double-coverage surprises. The stage does not
sample the pixel shader's CBV or assume its value.

`scarlett/tests/build-blit-colour-rect.sh OUTPUT` rebuilds and compares
the embedded GS and checks both exact VS/PS hashes before cross-linking
the standalone host test into `OUTPUT/game_scarlett_master.exe` (DXC
1.9 b106a961; set `DXC` to its native path). The shared VS/PS source is
the sole dependency from the slot-42 colour translation. The test uses
a DEFAULT-heap VB, GPU copy + barrier, firstVertex 1, exactly 3 remaining
records, and the same back-cull/CCW winding as the captured stream.
The probe PS exposes UV and additive fragment count; the real PS uses
b27 space1 c2 with four different coefficients and disabled blending.
Every pixel in the 6×6 target is checked, including the 1-pixel border.
The pitched readback has canaries before/after its footprint and checks
only actual texels within each row. AMD and RTX both returned 6, 16 and
32 covered samples for the triangle, rectangle and double draw; 16 for
the varying-colour test, 16 for the exact PS (`1e 4b 7d be`), and zero
for the wrong corner order. These are host draws from the fixture.

References for the topology and attribute math:
[AMD PAL `RectList`](https://github.com/GPUOpen-Drivers/pal/blob/dev/inc/core/palCmdBuffer.h)
(MIT; 3 axis-aligned vertices); [VIctory `primitive.rs`](https://github.com/marvisak/VIctory/blob/ffe310eb2098e52169f4c44db5ffdabf43a72622/crates/victory-gpu/src/primitive.rs)
(GPL-3.0-or-later; observed TL/BL/BR input expansion); [XCL `emit.rs`](https://github.com/ivall/xcl/blob/f4ce27ceb6d1b0576d0c659b713b674778bf1968/crates/xcl/src/gpu/recomp/emit.rs#L4547-L4569)
(GPL-2.0-only; post-VS affine varyings at equal positive W). Those
implementations were consulted for behavior; no code was imported.
