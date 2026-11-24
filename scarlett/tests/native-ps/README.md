# Native PS offline pixel fixture

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
the undrawn magenta border. No title vertex shader, resources, game pipeline
state, Wine/D3D device or present path is involved.
