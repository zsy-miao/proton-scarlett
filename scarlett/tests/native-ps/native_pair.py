"""Closed vertex-ID / one smooth float2 varying / MRT0 native graphics profile."""
from local_record import fields, span
from native_ps import need


def rows(blob):
    n, start = fields(blob, 0, "II")
    need(1 <= n <= 2 and start == 8, "unsupported signature width")
    result = []
    for i in range(n):
        stream, at, idx, sv, ty, reg, mask, prec = fields(blob, start+32*i, "8I")
        end = blob.find(b"\0", at, at+32)
        need(stream == prec == 0 and start+32*n <= at < end, "signature element")
        result.append((blob[at:end], idx, sv, ty, reg, mask))
    return result


def interface(vs, ps):
    bindings = None
    vertex = (b"SV_VertexID", 0, 6, 1, 0, 0x101)
    pos = (b"SV_Position", 0, 1, 3, 0, 15)
    target = (b"SV_Target", 0, 64, 3, 0, 15)
    vi, vo, pi, po = map(rows, (vs.inputs, vs.outputs, ps.inputs, ps.outputs))
    need(vi == [vertex] and po == [target] and len(vo) == len(pi) == 2 and
         vo[0] == pi[0] == pos, "vertex-ID / position / target ABI")
    need(vo[1][:5] == pi[1][:5] and vo[1][2:] == (0,3,1,0xc03) and pi[1][-1] == 0x303,
         "unmatched float2 semantic/index/register/mask")
    for stage, record, sigs, varying in ((1,vs,vi+vo,2), (0,ps,pi+po,1)):
        p = record.psv
        count, = fields(p,56,"I")
        need(count == len(record.resources) and fields(p, 0, "I") == (52,) and
             p[28] == stage and tuple(p[32:35]) == ((1,2,0) if stage else (2,1,0)),
             "PSV stage shape")
        start = 60
        if count:
            need(not stage and count == 2 and fields(p,60,"I") == (24,), "singleton sampler + SRV")
            s,t = fields(p,64,"6I"),fields(p,88,"6I")
            si,ti = s[2],t[2]
            need(s==(1,1,si,si,14,0) and t==(3,1,ti,ti,2,0) and
                 si != ti and max(si,ti)<128, "space1 single sampler/Texture2D")
            need(record.resources==((0x401,0,si,0,0x60004),(0x300,0,ti,4,0x808)),
                 "native logical slots or SGPR tuple mapping")
            bindings = (ti,si)
            start = 112
        size, = fields(p, start, "I")
        names = span(p, start+4, size)
        count, = fields(p, start+4+size, "I")
        indices = span(p, start+8+size, 4*count)
        base = start+8+size+len(indices)
        need(fields(p, base, "I") == (16,), "PSV element stride")
        name, idx, n, reg, cols, semantic, ty, mode, dyn, reserved = fields(
            p, base+4+16*varying, "II8B")
        sig = sigs[varying]
        need(span(names, name, len(sig[0])+1) == sig[0]+b"\0" and
             fields(indices, 4*idx, "I") == (sig[1],) and
             (n,reg,cols,semantic,ty,mode,dyn,reserved) == (1,1,0x42,0,3,2,0,0),
             "expected one allocated, perspective-linear float2")
    return bindings


HALF = """float rtz16(float x){
    uint h = packHalf2x16(vec2(x,0)) & 65535u;
    if (abs(unpackHalf2x16(h).x)>abs(x)) --h;
    return unpackHalf2x16(h).x;
}
"""
LEGACY = "float legacy(float a,float b){return a==0.0 || b==0.0 ? 0.0 : a*b;}"
HLSL = """#define vec2 float2
#define vec4 float4
#define uintBitsToFloat asfloat
#define fract frac
#define fma mad
float rtz16(float x){
    uint h = f32tof16(x);
    if (abs(f16tof32(h))>abs(x)) --h;
    return f16tof32(h);
}"""


def translate(code, vs, bindings=None, linear=True, *, hlsl=False):
    need(0 < len(code) <= 4096 and len(code)%4 == 0, "word span")
    w = iter(fields(code, 0, f"{len(code)//4}I"))

    def take():
        x = next(w, None)
        need(x is not None, "missing operand, export or end")
        return x

    if hlsl:
        source = [HLSL, LEGACY]
        main = ("void main(uint gl_VertexIndex:SV_VertexID, out float4 gl_Position:SV_Position, "
                "out float2 uv:TEXCOORD0){" if vs else
                "void main(float4 pos:SV_Position, float2 uv:TEXCOORD0, out float4 mrt:SV_Target0){")
    else:
        source = ["#version 450"]
        source += (["layout(location=0) out vec2 uv;"] if vs else
                   ["layout(location=0) in vec2 uv; layout(location=0) out vec4 mrt;", HALF, LEGACY])
        main = "void main(){"
    if bindings:
        tex,samp = bindings
        source += ([f"Texture2D<float4> srcImage:register(t{tex},space1);",
                    f"SamplerState srcSampler:register(s{samp},space1);"] if hlsl else
                   [f"layout(set=0,binding={tex}) uniform texture2D srcImage;",
                    f"layout(set=0,binding={samp}) uniform sampler srcSampler;"])
    source.append(main)
    regs = {0: ("uint", "uint(gl_VertexIndex)")} if vs else {0: ("I", ""), 1: ("J", "")}

    def read(reg, typ):
        need(reg in regs and regs[reg][0] == typ, "wrong or undefined register type")
        return regs[reg][1]

    def write(reg, typ, expr):
        name = f"r{len(source)}"
        source.append(f"  {typ} {name} = {expr};")
        regs[reg] = (typ,name)

    def fp(arg):
        if arg >= 256:
            return read(arg-256, "float")
        if arg == 255:
            literal = take()
            need(literal & 0x7f800000 != 0x7f800000, "nonfinite literal")
            return f"uintBitsToFloat(0x{literal:08x}u)"
        inline = {128:"0.0",240:"0.5",241:"-0.5",242:"1.0",243:"-1.0",244:"2.0",245:"-2.0"}
        need(arg in inline, "unknown float source")
        return inline[arg]

    op = take()
    while op >> 16 == 0xd580:
        args = take()
        need(args >> 18 == 255 and args & 0x20100 == 0x20100, "annotated V_NOP operands")
        take()
        op = take()
    exports, channels = set(), set()
    m0 = False
    sg = {8:"table",9:"param"}
    wave = "exact"
    loaded = visible = sampled = ready = False
    while op != 0xbf810000:
        dst, a, b = op >> 17 & 255, op & 511, op >> 9 & 255
        if op in (0xbfa00003,0xbf800000):
            pass
        elif op & 0xfe01fe00 == 0x7e000200:
            write(dst, "float", fp(a))
        elif vs and op >> 25 in (0x1a, 0x1b):
            need(128 <= a < 160, "integer inline source")
            v = read(b, "uint")
            write(dst, "uint", f"{v} {'<<' if op >> 25 == 0x1a else '&'} {a-128}u")
        elif vs and op & 0xfe01fe00 == 0x7e002200 and a >= 256:
            write(dst, "float", f"float({read(a-256,'uint')} & 255u)")
        elif vs and op & 0xffffff00 == 0xd54b0000:
            args = take()
            need(args >> 27 == 0, "FMA modifiers")
            operands = [(args >> shift) & 511 for shift in (0,9,18)]
            need(operands.count(255) <= 1, "multiple literal sources")
            write(op & 255, "float", f"fma({','.join(map(fp, operands))})")
        elif vs and op in (0xf80008cf,0xf800020f):
            need(op not in exports, "duplicate VS export")
            exports.add(op)
            args = take()
            vals = [read(args >> shift & 255,"float") for shift in (0,8,16,24)]
            if op == 0xf80008cf:
                source.append(f"  gl_Position=vec4({','.join(vals)});")
                if not hlsl:  # D3D clip Y -> positive Vulkan viewport
                    source.append("  gl_Position.y=-gl_Position.y;")
            else:
                source.append(f"  uv=vec2({','.join(vals[:2])});")
        elif bindings and op == 0xbe8c047e and wave=="exact":
            wave = "saved"
        elif bindings and op == 0xbefe0a7e and wave=="saved":
            wave = "quad"
        elif bindings and op == 0x87fe0c7e and wave=="quad" and channels=={0,1}:
            wave = "restored"
        elif bindings and op & 0xff80ff00 == 0xbe800300:
            d,src = op>>16&127,op&255
            need(d in (0,1,2,3,9,124) and src in (2,9,128,255), "scalar move profile")
            sg[d] = take() if src==255 else 0 if src==128 else sg.get(src)
            if d==124:
                need(sg[d]=="param","interpolation parameter base")
                m0 = True
        elif bindings and op & 0xffff0000 == 0xb0000000:
            sg[0] = op & 65535
        elif bindings and op == 0xf40c0104:
            need(not loaded and take()==0xfa000000 and sg[8]=="table" and sg[9]==0,
                 "single descriptor dwordx8 load at table+0")
            loaded = True
            sg.update({i:"descriptor" for i in range(4,12)})
        elif bindings and op == 0xbf8cc07f and loaded:
            visible = True
        elif bindings and op == 0xf0800f08:
            args = take()
            need(wave=="restored" and visible and not sampled and args>>16==1 and
                 all(sg[i]=="descriptor" for i in range(4,12)), "image/sampler SGPR tuple or load")
            sampler = tuple(sg.get(i) for i in range(4))
            need(sampler==(0x92,0xc00000,0x14000000 | (0x500000 if linear else 0),0),
                 "sampler descriptor does not match supplied clamp/filter")
            va,vd = args&255,args>>8&255
            need(va<=254 and vd<=252, "sample register extent")
            value = f"sample{len(source)}"
            sample = "srcImage.Sample(srcSampler," if hlsl else "texture(sampler2D(srcImage,srcSampler),"
            source.append(f"  vec4 {value}={sample}"
                          f"vec2({read(va,'float')},{read(va+1,'float')}));")
            for i,c in enumerate("xyzw"):
                regs[vd+i] = ("pending", f"{value}.{c}")
            sampled = True
        elif bindings and op == 0xbf8c3f70 and sampled:
            for i,(typ,value) in regs.items():
                if typ=="pending":
                    regs[i] = ("float",value)
            ready = True
        elif not vs and not bindings and op == 0xbefc0309:
            m0 = True
        elif not vs and op & 0xfc000000 == 0xc8000000:
            step, attr, lane = op >> 16 & 3, op >> 10 & 63, op >> 8 & 3
            dest, weight = op >> 18 & 255, op & 255
            need(m0 and (not bindings or wave=="quad") and attr == 0 and lane < 2 and step in (0,1),
                 "unsupported interpolator")
            read(weight, ("I","J")[step])
            if step == 0:
                regs[dest] = ("p1",lane)
            else:
                need(read(dest,"p1") == lane, "P1/P2 varying mismatch")
                write(dest,"float","uv."+"xy"[lane])
                channels.add(lane)
        elif not vs and op >> 25 in (3,7,43):
            x,y = fp(a),read(b,"float")
            if op >> 25 == 3:
                expr = f"{x}+{y}"
            elif op >> 25 == 7:
                expr = f"legacy({x},{y})"
            else:
                expr = f"fma({x},{y},{read(dst,'float')})"
            write(dst,"float",expr)
        elif not vs and op & 0xffffff00 == 0xd5078000:
            args = take()
            need(args >> 18 == 0 and args & 511 == 255 and (args >> 9) >= 256, "mul clamp form")
            write(op & 255,"float",f"clamp(legacy({fp(255)},{fp(args>>9)}),0.0,1.0)")
        elif not vs and op & 0xfe01fe00 == 0x7e004000:
            write(dst,"float",f"fract({fp(a)})")
        elif not vs and op >> 25 == 0x2f:
            need(not bindings or ready,"sample result not ready")
            write(dst,"vec2",f"vec2(rtz16({fp(a)}),rtz16({read(b,'float')}))")
        elif not vs and op == 0xf8001c0f:
            args = take()
            need(args >> 16 == 0 and not exports, "compressed MRT0")
            source.append(f"  mrt=vec4({read(args&255,'vec2')},{read(args>>8,'vec2')});")
            exports.add(op)
            op = take()
            need(op == 0xbf810000, "expected end after MRT")
            break
        else:
            raise ValueError("unsupported native instruction")
        op = take()
    need((not bindings or ready) and exports == ({0xf80008cf,0xf800020f} if vs else {0xf8001c0f}) and
         (vs or channels == {0,1}) and all(t == 0xbf9f0000 for t in w), "exports/inputs/end padding")
    return "\n".join(source+["}"])


def link(vs, ps, *, hlsl=False):
    need(interface(vs, ps) is None,"resource-free pipeline")
    return translate(vs.code, True, hlsl=hlsl), translate(ps.code, False, hlsl=hlsl)


def link_sample(vs, ps, table, *, hlsl=False):
    bindings = interface(vs,ps)
    need(bindings is not None,"missing texture/sampler")
    tex,samp = bindings
    need(table.get(tex)=="texture2D" and table.get(samp) in ("linear-clamp","point-clamp"),
         "missing or mistyped host bindings")
    return (translate(vs.code,True,hlsl=hlsl),
            translate(ps.code,False,bindings,table[samp]=="linear-clamp",hlsl=hlsl))
