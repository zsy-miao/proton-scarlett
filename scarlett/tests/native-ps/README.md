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
