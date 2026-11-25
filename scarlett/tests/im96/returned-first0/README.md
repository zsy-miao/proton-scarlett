# Returned target: first vertex 0 (offline replay)

```sh
bash scarlett/tests/im96/returned-first0/build.sh "$READY96_STAGE" "$SPLIT6_OM_STAGE"
```

Use the qualified READY96 stage (control PE `ef58096b`) and the split-six
OM provider stage (`47002876` provider DLL). The latter supplies its native
source, project DXIL header and VM object; the provider DLL is not linked.
The former supplies its control manifest, three test headers and private
fixture inputs. The recipe copies inputs into a new directory, applies the
test overlay there and verifies [all output hashes](outputs.sha256). It
neither extracts nor distributes title resources. Reproduced with MinGW-w64
GCC/G++ 16.2.0 and GNU ld 2.46.

Only the PE changes (`ef58096b` → `d0661006`); the first-167 `rect-vb.bin`
is omitted because `main.cpp` supplies three new vertices at index 0.
The control's inode, link count and digest must survive the build.

**Pixel oracle.** In the 16×16 viewport the projected corners are
`(6,4), (6,10), (12,10)`. The rectangle shader generates the fourth corner.
With white vertex color and constant UV `(0.75,0.5)`, pixel centers in
`[6,12) × [4,10)` sample the green half. Check all RGBA8 pixels after both
fences reach 1:

| 3840×2160 surface | Expected |
| --- | --- |
| Returned target | 36 opaque green pixels; all 8,294,364 others transparent black |
| Sampled texture | Every row opaque magenta for `x < 1920`, opaque green otherwise |

RADV and RTX each exited 0 on the pinned PE. Each recorded one table bind
and one `DrawInstanced(3,1,0,0)` on the split-six tail; the original 64 object
bytes, 480 camera bytes and 32 padding canaries also passed fenced readback
after VA reuse.

**Limit.** The fixture supplies the prior clear history, returned-target
registration, sealed table and submission gate. It rejects first vertex 167
and stale tickets before emitting. These results qualify that one synthetic
draw; they do not establish a naturally selected wrapper, production queue
admission or a presented frame. No launcher or default build hook is included.
