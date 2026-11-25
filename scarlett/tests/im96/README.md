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

A later AMD diagnostic matched the worker, selection and return on the
same request, group, root and device. Both full shader hashes matched the
table above. The 688-byte stream specified one RGBA8 UNORM target, no
depth target, one sample, and per-vertex POSITION, COLOR and TEXCOORD
at offsets 0, 12 and 16. The factory returned `E_NOTIMPL` and no PSO.

A CPU replay found one way these roots can alias. The two colour shaders
and the two textured shaders all have inner lookup key `0x00010001`.
With the colour root already in the cache, all four retrieved that
wrapper without invoking the decoder. On a miss the layouts differed:
the 16-byte colour payload decoded to `b27` alone; the 220-byte textured
payload decoded to one table (`t7`, `t277`, `b0`, in order) and 22 static
samplers, including `s14`. An AMD retail diagnostic subsequently saw both
groups miss the group and outer caches, then hit the same inner node and
receive the same root wrapper despite the different payloads.

The device can create the exact 1,304-byte textured root when asked.
On both GPUs, the fixture called device `+80`, rejected a separate
mutation of each of its 22 samplers and drew with the returned root.
All 16 shaded pixels and 48 guard pixels matched after the fence.
Creating this layout does not repair the title's cached selection.

A second fixture retained the original guest colour-root object and
created a private textured layout on the same device. On AMD and RTX a
draw through the private layout passed the same pixel checks; the original
object still resolved to its original host root. The fixture supplied the
three descriptors. Retail consumption of that table has yet to be observed.

The live producer's camera payload is 480 bytes; the following 32 bytes
belong to the object transform, not to camera padding. In a separate
AMD/RTX fixture the shader read the camera rows at `0x160..0x19f` from a
512-byte aligned constant buffer. Fenced readback matched all 480 original
bytes and all 32 independently filled padding bytes after address reuse.
The object SRV used a separate 64-byte copy starting at `0x3de20`;
rounding that address down to a 64-byte stride boundary would read 32
wrong bytes. Both adapters matched the 16 shaded pixels and 48 guards.
Set `XMEM_DLL` to the matching staged XMem DLL when running `build.sh`
to include the placed-buffer lifetime and padding checks.

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

### Three-vertex topology-17 fixture

Set `IM96_RECT=1` and `XMEM_DLL` when running `build.sh` for the separate
three-vertex PE. The `+126275..+1262ac` producer derives width and height
from view dimensions shifted by the mip selector, each clamped to at least 1.
`+1262f6..+126375` selects `0x11`, count 3, stride 24. In increasing address
order `+1263ad..+1263e6` writes `(0,0,0)`, `(0,h,0)`, `(w,h,0)`, RGBA8
`ffffffff` each, with UVs `(0,0)`, `(0,1)`, `(1,1)`; see also the second-VB
shape in `native/queue/test-clear4-bind.c`. The second setter recorded
slot 0, 4080/24, topology 17. Neither that setter nor the producer identifies
the final clip positions, W, or first vertex on the retail draw.

The test uses the unchanged VS71 and PS86, exact private table, synthetic
4×4 dimensions and two-stage transform. A separate post-VS GS requires
finite TL/BL/BR clip positions with constant depth and equal positive W.
At W=2 the missing TR takes BR.x and TL.y; each smooth UV/colour lane
continues by the horizontal difference BR−BL from TL. Both triangles
start at TL with the same winding. Unsupported shapes emit nothing. There
is no inference from the mode-15 position guard or just the topology
number. The colour word is unpacked by IA before interpolation.

The producer-shaped case uses the last legal three-record window at
fixture index 167; a second case has four colour gradients. A 4×4 binary
RGBA8 pattern distinguishes all 16 texel centres at 1:1 scale. The PE
checks every RGBA32F pixel of four targets after the fence: 16 texture
results and 48 guards in each valid case, all 64 unchanged for reversed
order and unequal W. It retains the existing table-generation and padding
checks. `rect-cpu.py` executes DXC's VS and GS bodies and inspects the two
emitted triangles in each valid case and the empty streams in both rejects.
No host fixture dimensions, projection or readbacks identify retail pixels.
On both adapters the equal-W cases matched all 16 shaded pixels and 48
guards; all 128 guard pixels in the two rejection cases remained intact.

Behavioural sources: AMD PAL [hardware enum](https://github.com/GPUOpen-Drivers/pal/blob/c5e800072a32f68b6ccc4422936d96167c6e0728/src/core/hw/gfxip/gfx9/chip/gfx9_plus_merged_enum.h)
and [RectList API](https://github.com/GPUOpen-Drivers/pal/blob/c5e800072a32f68b6ccc4422936d96167c6e0728/inc/core/palCmdBuffer.h)
(MIT; API ordinal 5 is distinct from hardware 17);
[VIctory](https://github.com/Marvisak/VIctory/blob/ffe310eb2098e52169f4c44db5ffdabf43a72622/crates/victory-gpu/src/primitive.rs#L1-L17)
(GPL-3.0-or-later; second vertex is the elbow for this ordering);
[XCL](https://github.com/ivall/XCL/blob/c4d37bec1bb4286eb373aea963404a23f13401fa/crates/xcl/src/gpu/recomp/emit.rs#L4725-L4747)
(GPL-2.0-only; post-VS equal-W smooth outputs, other orders possible).
No upstream implementation copied.

### F96 factory and explicit bind

`XS_IM96_FACTORY` is a build-time opt-in. The 688-byte stream must match
the normalized fixed-state digest, both entire guest stages, the three
IA elements and a live canonical #96 root or b27 alias on the same device.
The factory retains the canonical host root directly; for b27 it creates
the private #96 layout. The returned wrapper owns the guest identity, host
root and PSO. Only an
explicit `+c8` can install them on the recorder. This path does not
admit a draw, bind the used table or qualify a CBV lifetime.

In an opt-in build, `F96 factory ... step=returned hr=00000000` records
the actual wrapper return. Join it to `F96 installed-c8` in the same
process by owner, guest alias and wrapper. The latter is emitted after
the host root and PSO setters and identifies the recorder and segment.
`PSO refused-c8` alone is generic. A factory return is not a bind, and a
bind is not a draw. The staged RADV and RTX fixtures recorded both; the
ordinary runs of that stage had neither a successful return nor a
matching install before their first stops.

From the project root, with a local DXC and the six previously qualified
private files listed by `factory/inputs.sha256`:

```sh
export DXC=/path/to/dxc
python3 -IB scarlett/tests/im96/test-factory-cpu.py "$PRIVATE_F96"
python3 -IB scarlett/tests/im96/factory/test-install.py
bash scarlett/tests/im96/factory/build.sh "$PRIVATE_F96" "$NEW_OUTPUT"
```

The generator compiles only this project's VS/PS and rect3 GS, verifies
each SHA-256 and emits the header in the build directory. The existing
private fixture directory supplies the inputs; they are not distributed.
`game/` contains the hardware fixture; the sibling `d3d12_xs.dll` is
the opt-in paired provider built from the same device source. The fixture
links the recorder directly. It checks both factory root kinds, foreign and
stale roots, and guest/host cleanup after a fixture-only host PSO failure.
It exercises real device creation, checked
factory inputs, an explicit bind on a synthetic post-clear list, repeated
alias selection and wrapper retirement. The CPU installer test separately
checks root-before-PSO ordering. A successful hardware run exits zero
and prints `PASS F96 factory, explicit +c8 and wrapper retirement`.
It issues no draw or queue submission. The earlier b27-only fixture (`d082991f`)
passed on RADV and RTX: creation and publication succeeded, the explicit
bind selected the private root and PSO, and closing and releasing the list
retired the wrapper.

Fixture `ab2cbb8d` passed on both RADV and RTX. It created a real host
pipeline with the registered canonical root and verified that both root
kinds released their references after a host creation failure. The b27
path also installed the pipeline on explicit `+c8`. The measured retail
request used b27; this canonical-root result does not explain why the
pipeline was unavailable when the game selected the pass.

The factory fixture also passes on both GPUs with `XS_IM96_ONESHOT`, which
uses `BCryptHash` for the same three SHA-256 checks. Mutated inputs were
rejected. In one AMD retail timing comparison, worker entry to the host
dispatch point fell from 2.116 ms to 0.386 ms. The game still sampled a
null pipeline on two recordings; neither installed or drew with this
pipeline. The second run did not log the host call's return.

Tool pin: DXC `1.9(dev;4950-b106a961)`; `LICENSE-LLVM.txt` (UI/NCSA)
and `LICENSE-MS.txt` (Microsoft DirectX Shader Compiler terms) accompany
the separately installed compiler. Source and behavioral attribution for
VS71/PS86 and rect3 follows the pinned Leonida, PAL, VIctory and XCL
revisions in this README. No compiler, private inputs or comparison code
are bundled here.

### Attribution

Selector/disassembly tools: [Leonida](https://github.com/ItsNotRexed/Leonida/tree/7b7d73f890f9fbbf5967bbb694390bbc99e33ba6/tools), GPL-3.0-or-later. Sampler field definitions cross-checked against Leonida `14b2e4462`, `runtime/src/gfx10_descriptors.h`. See also the existing `../im-transfer/README.md` references: [XCL](https://github.com/ivall/XCL), GPL-2.0-only (sampler bits), and [VIctory](https://github.com/Marvisak/VIctory/tree/ffe310eb2098e52169f4c44db5ffdabf43a72622), GPL-3.0-or-later (explicit host views). Their code and license files remain upstream; none is vendored here. Fixture setup follows the project's `im-transfer/draw.cpp` and CPU DX-op test approach. The patch contains project source, tests and metadata only.
