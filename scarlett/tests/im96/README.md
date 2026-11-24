# Textured arm of `+125ba0`: `im:default_pos_cpv_tex`

**Tested on AMD and RTX:** technique 96/pass 0, VS71/PS86. On both GPUs the fixture shaded the 16 intended pixels, preserved the 48 border pixels and left both source generations and the previous destination unchanged. The local 1.0.2.1 selector and both whole-container digests match. `im_default_pos_cpv_tex.hlsl` is an independent shader pair. The retail caller reached this branch's vertex-buffer setup; a draw with this pair has not been observed.

### Selection evidence

For the optional transfer to the primary, `+125d52 → +31a7f0(0)` resolves the output, bound at `+125daf`. On the false arm at `+125ef9`, `+1261a9` loads the source texture pointer; `+1261e2` reads its binding at `+28h` on the non-null update path. `+12625b..+126270` selects the textured technique on non-null, otherwise #95. At `+126352..+126375` the producer requests three vertices, stride 24; the draw call at `+1263eb → +2aacf0` has parent return `+1263f0`. Parent alone doesn't distinguish #95. The #32 draw returns to `+126152` and uses different resources and math.

`sgd5_effects.py "$PACK" --effect im --technique default_pos_cpv_tex` selects the second database at 271085568, effect `0x105506d0`, technique hash `0e5df1b0`. Use the **record** offsets, not carve offsets. Header-delimited executable FSSM ranges decoded without invalid instructions; remaining section padding is not shader logic.

| Stage | Record (decimal), size inflated | FSSM range | Native container SHA-256 | Host DXIL SHA-256 (DXC b106a961) |
|---|---|---|---|---|
| VS71 | 271202363, 1712 | `[800,1024)` | `c65eb8c9af8fcf7389a20872ba42eb1357ccc92a5ae5bb71842ced435741fdb4` | `465f1398375291c203017dd2bd027ac447609c75cddcdac652a7033da233b607` |
| PS86 | 271942256, 1588 | `[3872,4040)` | `5cd73cc975d0976da64f3b1a1e882e49bc99c064db540fabb4eb1bb8614cfe9f` | `7d37c5ab2fff9fe24f1a1edfff11db2ae474afa6579825dbc2ddda34fe224db5` |

### Binding contract (all space1)

At technique-96 selection in the AMD diagnostic the group and the preceding
colour-table bind named the same live b27-only root. The build checks both
translated stages against that exact root and against the fixture layout:
DXC rejects the former (missing VS `b0`, PS `s14`) and accepts the latter.
The group's pending request had no published PSO at the sample. This
doesn't identify the shader stream eventually supplied to the worker.

| Stage | Register | Consumer |
|---|---|---|
| VS | `t7` | Structured SRV, stride 64, first **view-relative** element. Four float4 rows at bytes 0,16,32,48. Only XYZ used: start with translation, accumulate X, Y, Z by FMA. Row W is ignored even when nonzero. |
| VS | `b0` | Four float4 rows at `0x160..0x19f` of this CBV. Transform the preceding XYZ: legacy multiply X by first row, accumulate Y and Z by FMA, then add fourth row. Export XYZW and unchanged RGBA/UV. |
| PS | `t277` | Texture2D, sample all four lanes at interpolated UV with implicit derivatives (`image_sample`). |
| PS | `s14` | Inline words `00000400 00c00000 18f00000 00000000`: wrap XYZ, aniso-bilinear min/mag, linear mip, max aniso 4, LOD `[0,12]`, bias 0. Host uses a static anisotropic wrap sampler. |

Native VS descriptor loads use table bytes 0/64 for `t7/b0`; PS loads the texture descriptor at byte 32. These are **not** offsets into the resources. PS interpolates attr0 RGBA and attr1 UV; each sampled lane times its matching colour lane uses legacy zero multiplication. Paired RG/BA half conversions round toward zero before export to MRT0. No #32 display transform, forced LOD 0, or shader-side saturation. The test uses finite normal values and does not claim adapter-specific FMA, denorm or sampling bit parity.

### Reproduce

```sh
g=$(mktemp -d /tmp/opencode/im96-build.XXXXXXXX)/game
export DXC=/tmp/opencode/tombox-provider/dxc/bin/dxc
export CLANG=/tmp/opencode/scarlett-leonida/llvm/bin/clang
bash scarlett/tests/im96/build.sh "$g"
python3 -IB scarlett/tests/im96/qualify.py "$PACK"  # optional local archive, read-only
```

The sealed source is `src17`, **not** its newer `src18`. The current primary is `dst41`, **not** its retired `dst40`. All four allocations are 8×8 RGBA32F with distinct contents. The synthetic transform rotates, translates and scales in two stages and produces `clip.w=2`; the 4×4 interior is produced by the transform, not a scissor. Varying Z exercises depth; poison values occupy unused object W lanes and the CB prefix. Wrapped flipped-U coordinates land on distinct texel centres. Four different vertex-colour gradients modulate every RGBA lane, including signed texture values.

The CPU test runs both DXC-generated entry point bodies; only DX operations, vertex fetch, raster interpolation and sampling are supplied by the fixture. It compares clip positions, varyings, all 64 destination pixels and all three unmodified images against an analytic oracle. Rebinding `src18` or writing `dst40` must fail comparison. The sampler shim requires exact wrap texel centres at 1:1 scale on a single mip; this does not qualify minification or hardware anisotropy. The PE paints all four independent RTVs, samples sealed `src17`, draws a test triangle strip, fences and checks all four readbacks. It uses no retail code or data.

Before retail integration require the parent **and** pass/digests, independently resolve source and destination resource/view/subresource generations including source mip and destination ring, verify no alias, and retain both through completion. Validate object SRV stride/range, CBV extent, fetch, rectangle handling, sampler, view formats, blend and viewport against the actual draw. Test IA/topology, descriptor placement and role numbers are synthetic; there is no general rectangle, table or barrier implementation here.

### Attribution

Selector/disassembly tools: [Leonida](https://github.com/ItsNotRexed/Leonida/tree/7b7d73f890f9fbbf5967bbb694390bbc99e33ba6/tools), GPL-3.0-or-later. Sampler field definitions cross-checked against Leonida `14b2e4462`, `runtime/src/gfx10_descriptors.h`. See also the existing `../im-transfer/README.md` references: [XCL](https://github.com/ivall/XCL), GPL-2.0-only (sampler bits), and [VIctory](https://github.com/Marvisak/VIctory/tree/ffe310eb2098e52169f4c44db5ffdabf43a72622), GPL-3.0-or-later (explicit host views). Their code and license files remain upstream; none is vendored here. Fixture setup follows the project's `im-transfer/draw.cpp` and CPU DX-op test approach. The patch contains project source, tests and metadata only.
