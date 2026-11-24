# Optional offscreen → primary: `im:CS_BLIT_UI_SDR_TO_BACKBUFFER`, technique 32

`im_ui_sdr.hlsl` and `im_ui_sdr_bind.hpp` implement one shader pair and its private D3D12 bindings. Both AMD and RTX passed all four transfer tests. The shader pair does no generic blit, presentation, geometry completion, or replacement of other `im` techniques.

### Which pair

`+4fd1e76 → +125ba0(sourceView,NULL)` resolves the destination via `+125d52 → +31a7f0(0)`, output binding `+125daf`. The inner `+125ef9` true arm calls `+126026 → +b39840(5, [source.texture+28h])`, draw **parent return `+126152`**. Query `sgd5_effects.py shaders.rpf --effect im --technique CS_BLIT_UI_SDR_TO_BACKBUFFER`: second database, effect `0x105506d0`, hash `5720674b`, technique 32/pass 0, VS25/PS29. `qualify.py` independently checks both complete inflated DXBC digests at the selected records. In-memory FSSM disassembly used `shader_disasm.code_range` + RDNA2 decoder from Leonida; header-delimited VS `[800,848)`, PS `[3872,4896)`, no invalid instructions. No native bytecode in this patch.

| Stage | Native whole-container SHA-256 | Project DXIL SHA-256 (DXC b106a961) |
|---|---|---|
| VS25 `VS_Blit_8e9c2e8a_Wrapped` | `d3b69854f269aab074c6c683db709dbeb0b3fb55444fbe65be4a98ff858d40c2` | `94f16a8a8b3bd5248096b40fff415fe19af43d4d42200b2eedf0c3f8cf0adbeb` |
| PS29 `PS_BlitUISDRToBackbuffer_8e9c2e8a_Wrapped` | `08995b8a32d9c44f398c9af9abc3893eee7f5edff637493d62beb302e76e9c98` | `ff03aa0576bff6ead14ba3afd07c2b8445229853f69e6d4c09d9e73bd88cf5a9` |

**Other arm (decoded, excluded).** False at `+125ef9`, source texture non-null: `+126270`, draw parent return `+1263f0`; `im:default_pos_cpv_tex`, technique 96/pass 0, hash `0e5df1b0`. VS71 (1712 B, SHA-256 `c65eb8c9af8fcf7389a20872ba42eb1357ccc92a5ae5bb71842ced435741fdb4`) transforms XYZ using four rows of structured `t7` starting at byte 0, then four float4s of `b0` starting at `0x160`, passes colour/UV. PS86 (1588 B, `5cd73cc975d0976da64f3b1a1e882e49bc99c064db540fabb4eb1bb8614cfe9f`): implicit-LOD `t277/s14` sample at interpolated UV times interpolated vertex RGBA using legacy multiplies, RG/BA half-RTZ export. All in space1. Source null selects #95 instead. Neither is #32, nor mode 15's #42/no-texture shader. Do not call the #32 implementation for either.

### What PS29 actually consumes

VS25 has no PSV resources. After calling external vertex fetch it exports `(xyz,1)` and passes vertex colour/UV unchanged. PS29 ISG1 has **unused colour**, used `TEXCOORD0.xy` and `SV_Position.xy`; compact native attr0.xy is the UV. Test IA uses stride 24 (xyz@0, packed colour@12, uv@16). This doesn't prove a particular retail fetch program.

| Register (space1) | Actual consumer |
|---|---|
| `t257` | source Texture2D; `image_sample_lz`, RGBA at interpolated UV, explicit LOD 0 |
| `s0` | inlined sampler words `(0x92,0xc00000,bitreverse(40),0)`, XYZ clamp-last, point min/mag/point mip, LOD `[0,12]` |
| `b26` | byte 80 high gain, 104 low gain, 108 knee, 120 pivot, 140 luminance divisor, 148 contrast divisor (names describe arithmetic, not title symbols) |
| `b27` | bytes 32/36/40 uint noise enable/vary slice/shape; 44 float amplitude; 48 float alpha exponent; 52 uint selects sRGB vs gamma; 92 uint frame, read only if dithering |
| `t274` | distinct Texture2DArray, `image_load` R with `(uint(SV_Position.xy)&63, varySlice ? frame&31 : 31)` if enabled. No filtering. |

PS native descriptor-table loads at byte `0/32/64/96` select `t274/t257/b26/b27` respectively, **not contents offsets**. Match in native resource rows by dword slot `0/8/16/24` and register `274/257/26/27`; sampler has separate immediate row. The project host table intentionally has a different order. All cbuffer offsets above are relative to each bound CBV. No CB contents inferred from the `0x60` argument block alone.

Shader order: per-channel piecewise sRGB decode (`x>.04045`) or exponent 2.2; four transforms with original component weights and evaluation order; smooth gain between the low/high values, pivot/contrast and luminance renormalization; alpha `exp2(alphaExponent*log2(sample.a))` with legacy zero rule; optional signed scalar noise added to RGB only (optionally `sign(n)*(1-sqrt(1-abs(n)))`); pack RG/BA toward zero to half before MRT0. No early clamp, byte rounding, attachment sRGB encode, or vertex-colour multiplication. Non-finite/denormal display constants and adapter transcendental/reciprocal bit parity are not qualified. `mad` requests multiply-add; bitwise fused behavior of every backend is not asserted.

### Build, test

In a scratch checkout with this patch applied (or from this scratch tree), run:

```bash
g=$(mktemp -d /tmp/opencode/im-test.XXXXXXXX)/game
export DXC=/tmp/opencode/tombox-provider/dxc/bin/dxc
export CLANG=/tmp/opencode/scarlett-leonida/llvm/bin/clang
bash scarlett/tests/im-transfer/build.sh "$g"
# Optional, from a project checkout with tests/native-ps/local_record.py present:
python3 -IB scarlett/tests/im-transfer/qualify.py "$PACK"
```

The build cross-compiles the PE and both shaders, then runs the actual compiled PS body with CPU implementations of its DX ops. The CPU sampler covers the fixture's interior point samples at mip 0; it is not a driver, GPU rasterizer or descriptor lifetime test. Cases 0–3 are sRGB, gamma 2.2, fixed-slice raw noise, and frame-slice shaped noise. Double-precision reference uses matrix algebra, scalar transfer functions and independent half quantization.

**GPU fixture:** use the existing project launcher on a scratch tool, one adapter at a time:

```bash
SCARLETT_TEST_GPU=radv SCARLETT_TEST_DISPLAY=sway "$PROJECT/scarlett/test-launch-nixos.sh" "$g" "$TOOL"
SCARLETT_TEST_GPU=nvidia SCARLETT_TEST_DISPLAY=xvfb "$PROJECT/scarlett/test-launch-nixos.sh" "$g" "$TOOL"
```

Require all four `PASS im transfer mode5 control` lines in each run log. The source is a distinct 4×3 float offscreen RTV painted with twelve distinct channel-coded texels, then transitioned to PS SRV. The target is a 10×8 float RTV with a sentinel border. An ordinary fixture triangle samples flipped U and unflipped V into its 8×6 interior. Vertex colours deliberately differ and are irrelevant. Decorations of the dither texture vary along X, Y and slice. The fixture remaps **noncontiguous** source descriptors into an owned table and compares all four lanes of every target pixel after a fence, plus all 12 unmodified source pixels. Checks include exact untouched border and half-representable output within one half step for finite approximations. This distinguishes reverse direction, wrong texture/slice, raw copy, ignored colour conversion and wrong alpha.

### Retail integration gate

Select by the actual `+126152` parent, technique/pass, full stage digests and owned root/PSO. Resolve `sourceView → texture(+8) → resource(+20h), binding(+28h)` to the prior sealed offscreen RTV; independently resolve the current primary destination at `+125daf` to its ring resource, checking generation/subresources and no alias. Validate actual fetch, viewport, blend, attachment and source view formats (including sRGB), sampler, both CBV ranges and dither SRV. Copy only from owned, same-device CPU descriptors into a fresh shader-visible table using `im_ui_sdr::populate`; retain table, resources and allocation generations through completion. Use actual source states/barriers and preserve fixed pipeline state; no hard-coded global source, primary ring index or texture inferred from dimensions. The rectangle recorder remains separate. A retail call, draw, submission or presented image has not been observed for this branch.

[XCL](https://github.com/ivall/XCL) `c4d37be` (GPL-2.0-only), `gpu/recomp/emit.rs` (explicit-LZ → SampleLevel; sampler bit fields), and VIctory `ffe310e` (GPL-3.0-or-later), `victory-gpu/src/descriptor/host.rs` (explicit host views), are behavioral references, not copied implementations. They don't identify this pass's bindings or supply its colour transform; those come from the selected containers.
