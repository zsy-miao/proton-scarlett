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
        need(fields(p, 0, "I") == (52,) and fields(p, 56, "I") == (0,) and
             p[28] == stage and tuple(p[32:35]) == ((1,2,0) if stage else (2,1,0)),
             "resource-free PSV stage shape")
        size, = fields(p, 60, "I")
        names = span(p, 64, size)
        count, = fields(p, 64+size, "I")
        indices = span(p, 68+size, 4*count)
        base = 68+size+len(indices)
        need(fields(p, base, "I") == (16,), "PSV element stride")
        name, idx, n, reg, cols, semantic, ty, mode, dyn, reserved = fields(
            p, base+4+16*varying, "II8B")
        sig = sigs[varying]
        need(span(names, name, len(sig[0])+1) == sig[0]+b"\0" and
             fields(indices, 4*idx, "I") == (sig[1],) and
             (n,reg,cols,semantic,ty,mode,dyn,reserved) == (1,1,0x42,0,3,2,0,0),
             "expected one allocated, perspective-linear float2")


HALF = """float rtz16(float x){
    uint h = packHalf2x16(vec2(x,0)) & 65535u;
    if (abs(unpackHalf2x16(h).x)>abs(x)) --h;
    return unpackHalf2x16(h).x;
}
float legacy(float a,float b){return a==0.0 || b==0.0 ? 0.0 : a*b;}
"""


def translate(code, vs):
    need(0 < len(code) <= 4096 and len(code)%4 == 0, "word span")
    w = iter(fields(code, 0, f"{len(code)//4}I"))

    def take():
        x = next(w, None)
        need(x is not None, "missing operand, export or end")
        return x

    source = ["#version 450"]
    source += (["layout(location=0) out vec2 uv;"] if vs else
               ["layout(location=0) in vec2 uv; layout(location=0) out vec4 mrt;", HALF])
    source.append("void main(){")
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
                source.append("  gl_Position.y=-gl_Position.y;")  # D3D clip Y -> positive Vulkan viewport
            else:
                source.append(f"  uv=vec2({','.join(vals[:2])});")
        elif not vs and op == 0xbefc0309:
            m0 = True
        elif not vs and op & 0xfc000000 == 0xc8000000:
            step, attr, lane = op >> 16 & 3, op >> 10 & 63, op >> 8 & 3
            dest, weight = op >> 18 & 255, op & 255
            need(m0 and attr == 0 and lane < 2 and step in (0,1), "unsupported interpolator")
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
    need(exports == ({0xf80008cf,0xf800020f} if vs else {0xf8001c0f}) and
         (vs or channels == {0,1}) and all(t == 0xbf9f0000 for t in w), "exports/inputs/end padding")
    return "\n".join(source+["}"])


def link(vs, ps):
    interface(vs, ps)
    return translate(vs.code, True), translate(ps.code, False)
