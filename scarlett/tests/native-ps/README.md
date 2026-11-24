# Native graphics offline pixel fixtures

Run `python3 test.py` here with a C11 compiler, Vulkan headers/loader/graphics
device, `glslangValidator` and `spirv-val`. Set `VK_DRIVER_FILES` to select an
ICD. Pass a local `shaders.rpf` path to additionally exercise the original
`PS_clearRipple_57383fac_Wrapped`. Generated GLSL, modules and readback are
temporary. No original bytecode file is written.

`local_record.py` selects a unique length-prefixed record, bounds both inflates,
checks the chunk and subentry extents, PS signature, zero resource count and
native PS descriptor before taking the ISA span. This is a reader for one record
layout, not an archive index or general FSSM loader. The fixture pins the 256-byte
input by SHA-256 after extraction. `native_ps.lower` knows neither the name nor
the digest. It tracks typed register writes and constant-folds the two half
lanes of each pack into the final float4. Inline inputs are restricted to
binary16-exact floats; no general fp32->fp16 rounding is claimed. The only
export is full, compressed, final MRT0 with a valid-mask bit. Annotated no-op
prefixes, observed scheduling hints and end padding are consumed explicitly.
Other opcodes, undefined registers and incomplete programs fail before compilation.

Oracle: independently decoding the ripple-clear instruction operands gives
half pairs `(0.5, 0.5)` and `(1, 0)`, selected as RG and BA. The oracle is fixed
in the test, not generated from the translator. The project-owned input exercises
overwrite, different VGPR numbers and reversed export order, yielding
`(-1, 4, 0.5, -2)`. Both go through SPIR-V validation and a Vulkan graphics
pipeline; readback checks all 48 RGBA16F pixels after a queue fence, including
the undrawn magenta border.

## Linked stages

The same command also runs a project-owned VS/PS UV-swap case. With the local
pack it additionally runs `VS_Passthrough_071514dd_Wrapped` (256 bytes) and
`PS_LUT3DIdentity_071514dd_Wrapped` (320 bytes) as one pipeline. Each module
is generated from that stage's native instructions. The VS uses vertex ID,
integer masks and shifts, byte conversion and FMA, and exports clip position
and PARAM0. Clip Y is inverted for the fixture's positive Vulkan viewport.
The PS's matched two-step interpolation reads the exported XY as a smooth
float2. It then calculates LUT channels and exports two RTZ-half pairs.
The linker checks semantic name/index, component type/mask, registers and
PSV interpolation mode. Both stage resource counts must be zero.

The oracle for texel `(x,y)` of a 256×16 identity LUT is
`((x % 16)/15, y/15, (x // 16)/15, 1)`, independently quantized toward zero.
The fenced readback checks all 3,556 unique interior colors and the 540
untouched border pixels. Names and hashes identify inputs only in tests.
The supported ABI is a non-indexed three-vertex draw starting at vertex ID
zero, one position, one perspective-linear float2 varying and one RGBA16F
target. This does not reconstruct retail PSO, binding, or presentation state.

## Sampled copy

The optional local run also translates `VS_BlurFilter_1a08fc46_Wrapped` and
`PS_PassthroughCopy_1a08fc46_Wrapped`. The PS has one full-mask 2D `image_sample`.
The linker checks the two space-1 singleton PSV records against the FSSM
resource rows, then maps their logical registers to separate Vulkan set-0
image and sampler bindings. A descriptor load at table offset zero is symbolic:
the fixture supplies a checked host view instead of dereferencing a guest address.
The translator checks the interpolation base, quad/exact-mask sequence, wait
instructions, descriptor SGPR groups and inline sampler state. The coordinate
VGPRs, four sampled results, two half packs and export operands drive the
generated fragment program. Names and digests are not translation inputs.

The fixture binds both 4×3 RGBA32F views and two clamp-to-edge samplers.
The original selects `t72/s3` and linear filtering. The project-owned case
selects `t73/s4` and nearest filtering, swaps UV and exports BGRA. The 16×16
target magnifies the asymmetric input. Independent texel and four-tap oracles
check every half-float channel of 196 sampled pixels and 60 border pixels
after the queue fence. The texture values and weights are binary fractions.

Scope is one normalized, single-level, non-array color view, one implicit-LOD
sample and one of two checked sampler descriptor tuples. No dynamic indexing,
arbitrary descriptor heaps, mip behavior, comparison sampling or general wave
execution is modeled. Tests reject absent or mistyped bindings, disagreement
between stages of binding resolution, wrong sampler state, altered descriptor
address, missing waits or mask restore, wrong image mask/SGPR tuple and missing
coordinate registers before Vulkan submission.

## Wine / vkd3d-proton

From this directory, on the NixOS test host with MinGW-w64, DXC and the
installed Scarlett Proton tool:

```sh
SCARLETT_TEST_GPU=radv python3 test_d3d12.py --dxc "$DXC" "$PACK"
SCARLETT_TEST_GPU=nvidia python3 test_d3d12.py --dxc "$DXC" "$PACK"
```

`PACK` is a local `shaders.rpf`; omit it to run only the project-owned controls.
`DXC` is native `dxc` (the default on PATH) or x64 `dxc.exe` with adjacent
`dxcompiler.dll` and `dxil.dll` (uses Wine). `--tool` overrides the launcher's
installed-tool default. The runner builds a fixture PE for `test-launch-nixos.sh`.
The same linked instructions emit VS/PS DXIL, with D3D clip coordinates and
space-1 register bindings. After queue retirement it unpacks each RGBA16F row
using the host copy footprint and checks every pixel with the existing oracles.

The supplied executable (SHA-256 `e95020b2fa4a721db3df59fdecf75621730ac559d8835252f6c3b658d5a08993`)
constructs a graphics **pipeline-state stream** at RVA `86f090`. It writes a
root pointer at `+8`, VS pointer/length at `+18/+20` and PS at `+30/+38` (hex).
At `2479a87` it calls the device's vtable byte offset `+2b0` with a descriptor
`{size=0x2b0, stream}`, `IID_ID3D12PipelineState` and an output address.
`2479acc` checks the HRESULT before publishing the result.

The fixture submits that stream shape to the bootstrap wrapper's *host*
`ID3D12Device2::CreatePipelineState`. Root, fixed state, descriptor tables and
draw are test-supplied. This does not qualify a live retail root, identify a
particular retail draw, or implement the guest vtable slot.

## First observed retail stream at +2479a8d (AMD)

`python3 test_retail_clear.py /path/to/game_scarlett_master.exe [capture-dir]`
checks the two exact embedded spans used by this call. With a local capture
(`stream.bin`, `vs.bin`, `ps.bin`) it also checks both stages byte-for-byte and
all stream bytes except the three process-specific pointers. No title bytes
are needed in the source tree. This is a **negative qualification oracle**.

The 688-byte stream requested the pipeline-state IID. A lookup under the
calling device's live root registry lock found a non-null host signature for
the supplied guest root: one root SRV `t0,space3`, visibility ALL, flags NONE.
Only VS/PS are present. Input layout empty, triangle topology type, one
R8G8B8A8_UNORM RTV, no DSV, sample count 1 quality 0, mask `ffffffff`.
No blend/logic operations, RT0 write mask RGBA; solid, no cull, front CCW,
no depth clip; depth/stencil disabled, no depth writes or bounds test.

VS_Clear_f05fef9c_Wrapped / PS_Clear_f05fef9c_Wrapped both contain NOBC
instead of host bitcode. The VS PSV row `(4,3,0,0,11,0)` is one raw SRV
at `t0,space3`; native resource row `(0x100,0,0,1,0x804)`. At native
offsets `38`/`80` (hex) scalar buffer loads read header words at 0/12.
The respective low nibbles must equal 3/4 to take the position/color fetch.
The vector byte address is `((word >> 7) & 0x1fffffc) +
VertexID * ((word >> 3) & 60)` (32-bit arithmetic). Buffer loads at `78`/`c8`
fetch 3/4 dwords. The nonmatching paths retain zero components. VS exports
position with W=1 and all of PARAM0. PS takes a **constant** interpolation
mode float4 TEXCOORD0: `s_mov_b32 m0,s10`, four `v_interp_mov_f32 ... p0`,
two RTZ half packs, compressed MRT0. PS has no resource bindings.

The current reader rejects VS resources; inspecting this specific mapping
does not enable lowering. Even with it parsed, `native_pair.link` rejects
the varying masks (VS `000f`, PS `0f0f`, not `0c03`/`0303`), and the HLSL
translator rejects both instruction streams beginning at offset `1c`.
The sampled-copy and constant-PS profiles do not cover this fetch, linkage
or flat interpolation. A faithful positive oracle needs a real root buffer
with distinguishable per-vertex data, both header branches, flat-color
selection and RGBA8 readback; the earlier space3 root draw alone proves
none of these shader semantics.
