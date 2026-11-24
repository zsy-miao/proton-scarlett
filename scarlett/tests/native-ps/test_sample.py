"""Independent sampler oracle and alternate register, binding and filter fixture."""
import hashlib
import math
import struct
from local_record import read_record
from native_pair import link_sample
from native_ps import need
from test_pair import own_pair, words

TABLE = {72:"texture2D",73:"texture2D",3:"linear-clamp",4:"point-clamp"}


def own_sample():
    vs,p = own_pair()
    t,s = 73,4
    rows = words(24,1,1,s,s,14,0,3,1,t,t,2,0)
    psv = p.psv[:56]+words(2)+rows+p.psv[60:]
    # Keep the project-owned P1/P2 operations, sample to v[20:23], then export BGRA.
    setup = words(0xbe8c047e,0xbefe0a7e,0xbe820309,0xbefc0302,0xbe890380,0xf40c0104,0xfa000000)
    sampler = words(0xb0000092,0xbe8103ff,0xc00000,0xbe8203ff,0x14000000,0xbe830380)
    def pack(dst,lo,hi):
        return 0x5e000100|dst<<17|hi<<9|lo
    code = (setup + p.code[4:20] + words(0x87fe0c7e) + sampler +
            words(0xbf8cc07f,0xf0800f08,1<<16|20<<8|11,0xbf8c3f70,
                  pack(3,22,21),pack(5,20,23),0xf8001c0f,5<<8|3,0xbf810000))
    return vs,p._replace(code=code,psv=psv,resources=((0x401,0,s,0,0x60004),(0x300,0,t,4,0x808)))


def cases(path, *, hlsl=False):
    vs,p = own_sample()
    def changed(old,new):
        need(p.code.count(words(old))==1,"fixture mutation point")
        return p._replace(code=p.code.replace(words(old),words(new)))
    for bad,table in [
            (p,{4:"point-clamp"}), (p,TABLE|{73:"sampler"}), (p,TABLE|{4:"linear-clamp"}),
            (p._replace(resources=p.resources[::-1]),TABLE),
            (p._replace(psv=p.psv[:100]+words(74)+p.psv[104:]),TABLE),
            (changed(0xfa000000,0xfa000020),TABLE), (changed(0xbf8cc07f,0xbf800000),TABLE),
            (changed(0x87fe0c7e,0xbf800000),TABLE), (changed(0xbf8c3f70,0xbf800000),TABLE),
            (changed(0xf0800f08,0xf0800708),TABLE), (changed(0x1140b,0x2140b),TABLE),
            (changed(0x1140b,0x11440),TABLE)]:
        try:
            link_sample(vs,bad,table,hlsl=hlsl)
        except ValueError:
            continue
        raise AssertionError("invalid sample pipeline accepted")
    yield "project-owned t73/s4, VU, point, BGRA", link_sample(vs,p,TABLE,hlsl=hlsl), False
    if path:
        names=("VS_BlurFilter_1a08fc46_Wrapped","PS_PassthroughCopy_1a08fc46_Wrapped")
        vs,p=[read_record(path,n,kind) for n,kind in zip(names,(1,0))]
        need([hashlib.sha256(r.code).hexdigest() for r in (vs,p)]==[
            "ed345d39ecde8c9f413cb946aa8080b17f6a5353c876c70efc498865247b0542",
            "5d3d8b3de556b4d831b6d7cd56115c7f1962c04795d7495a12eb2b68f205cfed"],"wrong fixture")
        yield " + ".join(names), link_sample(vs,p,TABLE,hlsl=hlsl), True


def check(data,linear):
    def texel(x,y,k):
        x=min(max(x,0),3); y=min(max(y,0),2)
        return ((x+4*y)/16,(3-x+k)/8,(2*y+k)/8,((x^y)+k)/8)
    need(len(data)==16*16*8,"sample image extent")
    colors=set()
    for i,pixel in enumerate(struct.iter_unpack("<4e",data)):
        x,y=i%16,i//16
        u,v=(x+.5)/16,(y+.5)/16
        if x in (0,15) or y in (0,15):
            rgba=(1,0,1,1)
        elif linear:
            a,b=4*u-.5,3*v-.5
            ix,iy=math.floor(a),math.floor(b)
            weights=[((1-abs(a-xx))*(1-abs(b-yy)),texel(xx,yy,0))
                     for xx in (ix,ix+1) for yy in (iy,iy+1)]
            rgba=tuple(sum(k*c[j] for k,c in weights) for j in range(4))
        else:
            r,g,b,a=texel(int(4*v),int(3*u),1)
            rgba=b,g,r,a
        need(pixel==rgba,f"sample pixel {x},{y}: {pixel} != {rgba}")
        if 0<x<15 and 0<y<15:
            colors.add(pixel)
    print(f"  (1,1)={struct.unpack_from('<4e',data,17*8)}; "
          f"(5,9)={struct.unpack_from('<4e',data,149*8)}; {len(colors)} distinct interior colors")
