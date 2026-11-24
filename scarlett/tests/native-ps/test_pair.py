"""Project-owned ISA and linkage fixture, plus the optional named local pair."""
import hashlib
import struct

from local_record import Record, read_record
from native_pair import link
from native_ps import need


def words(*w):
    return struct.pack(f"<{len(w)}I", *w)


def signature(rows):
    body, names = b"", b""
    for name, system, component, reg, mask in rows:
        body += words(0, 8+32*len(rows)+len(names), 0, system, component, reg, mask, 0)
        names += name+b"\0"
    return words(len(rows), 8)+body+names


def psv(stage):
    # Minimal profile fields consumed by the offline linker, not a full DXBC fixture.
    data = bytearray(60)
    data[:4] = words(52)
    data[28] = stage
    data[32:34] = bytes([1,2] if stage else [2,1])
    el = bytearray(48)
    at = 16*(2 if stage else 1)
    el[at:at+16] = words(1,0) + bytes([1,1,0x42,0,3,2,0,0])
    return bytes(data)+words(8)+b"\0UVPAIR\0"+words(1,0,16)+el


def own_pair():
    def vop2(op,dst,a,b):
        return (op<<25) | (dst<<17) | (b<<9) | a

    def vop1(op,dst,a):
        return 0x7e000000 | dst<<17 | op<<9 | a

    def fma(dst,x,y,z):
        return 0xd54b0000|dst, x | (y<<9) | (z<<18)

    def interp(step,dst,weight,lane):
        return 0xc8000000 | dst<<18 | step<<16 | lane<<8 | weight

    # Split vertex IDs into X=(id<<1)&2, Y=id&2. Deliberately export Y,X.
    vs = words(vop2(26,17,129,0),vop2(27,18,130,0),vop2(27,17,130,17),
               vop1(17,20,273),vop1(17,21,274),vop1(1,22,128),vop1(1,23,242),
               *fma(8,276,244,243),*fma(9,277,245,242),
               0xf800020f,int.from_bytes([21,20,22,23],"little"),
               0xf80008cf,int.from_bytes([8,9,22,23],"little"),0xbf810000)
    ps = words(0xbefc0309,interp(0,12,0,1),interp(1,12,1,1),
               interp(0,11,0,0),interp(1,11,1,0),vop1(1,8,240),vop1(1,9,242),
               vop2(47,10,267,12),vop2(47,14,264,9),0xf8001c0f,14<<8|10,0xbf810000)
    pos = (b"SV_Position",1,3,0,15)
    uv = (b"UVPAIR",0,3,1)
    return (Record(vs,signature([(b"SV_VertexID",6,1,0,257)]),signature([pos,(*uv,0xc03)]),psv(1)),
            Record(ps,signature([pos,(*uv,0x303)]),signature([(b"SV_Target",64,3,0,15)]),psv(0)))


def cases(path):
    vs,ps = own_pair()
    for v,p in [(vs, ps._replace(inputs=ps.inputs.replace(b"UVPAIR",b"OTHER!"))),
                (vs, ps._replace(psv=ps.psv[:-19]+bytes([1])+ps.psv[-18:])),
                (vs._replace(code=vs.code[:-4]), ps),
                (vs, ps._replace(code=ps.code[:8]+words(0xc8310001)+ps.code[12:])),
                (vs, ps._replace(code=words(0)+ps.code))]:
        try:
            link(v,p)
        except ValueError:
            continue
        raise AssertionError("bad stage, varying or instruction stream accepted")
    yield "project-owned UV swap", link(vs,ps), lambda x,y: ((y+.5)/16,(x+.5)/256,.5,1.)
    if path:
        names = ("VS_Passthrough_071514dd_Wrapped", "PS_LUT3DIdentity_071514dd_Wrapped")
        vs,ps = [read_record(path,name,kind) for name,kind in zip(names,(1,0))]
        need(tuple(hashlib.sha256(r.code).hexdigest() for r in (vs,ps)) == (
            "3ff0fb1cfc7dd8a96c665684adbf5429f730cfeb07a2aead4dd4448717a65240",
            "58c8b8812d68d695fc52f7463d19b02c0ab087f0c2b831dd2815c23c11a74957"),
            "wrong native fixture")
        # Independent identity-LUT definition: texel (tile*16+r,g) represents (r,g,tile)/15.
        yield " + ".join(names), link(vs,ps), lambda x,y: ((x%16)/15,y/15,(x//16)/15,1.)


def half_bits(x):
    h, = struct.unpack("<H",struct.pack("<e",x))
    nearest, = struct.unpack("<e",struct.pack("<H",h))
    return h-(nearest>x)  # oracle domain [0,1], truncate toward zero


def check(data,oracle):
    need(len(data)==256*16*8, "pair image extent")
    colours = set()
    for i,pixel in enumerate(struct.iter_unpack("<4H",data)):
        x,y = i%256,i//256
        interior = 0<x<255 and 0<y<15
        expected = tuple(map(half_bits,oracle(x,y) if interior else (1,0,1,1)))
        need(pixel==expected, f"pixel ({x},{y}): {pixel} != {expected}")
        if interior:
            colours.add(pixel)
    need(len(colours)==3556, "expected unique interior pixels")
    for x,y in ((31,3),(240,12)):
        print(f"  ({x},{y}) = {struct.unpack_from('<4e',data,8*(y*256+x))}")
