"""Fixture inputs and independent double-precision display transform reference."""
from pathlib import Path
import math, struct, sys

W,H = 10,8  # one-pixel clear border, active 8x6; source 4x3
clear = (-7.,-5.,-3.,-1.)
def texel(x,y): return ((1+13*x+8*y)/64,(2+15*y+x)/64,(31-5*x+2*y)/64,(2+x+3*y)/16)
def dither(x,y,z): return ((3*x+5*y+7*z)%31+1)/32
def expected(x,y,k):
    if not (0<x<W-1 and 0<y<H-1): return clear
    # Positive-W passthrough, horizontally reversed UV.
    r,g,b,a = texel(3-(x-1)//2,(y-1)//2)
    def dec(v): return v**2.2 if k==1 else (v/12.92 if v<=.04045 else ((v+.055)/1.055)**2.4)
    # Independently composed 709 -> XYZ, white adaptation, AP0, then output primaries.
    def mul(rows,v): return [sum(w*t for w,t in zip(row,v)) for row in rows]
    v=mul(((.412390888,.357584298,.180480838),(.212639064,.715168595,.0721923336),(.0193308201,.119194724,.950532317)),(dec(r),dec(g),dec(b)))
    v=mul(((1.01303005,.00610530982,-.0149710001),(.00769822998,.998165011,-.00503202993),(-.00284131011,.00468515977,.924507022)),v)
    v=mul(((1.04981101,0,-.0000974799987),(-.495902956,1.37331295,.0982400328),(.00000004,0,.991252124)),v)
    v=mul(((1.4514395,-.236510813,-.214928553),(-.0765538812,1.17623007,-.0996759385),(.00831613038,-.00603245012,.997716248)),v)
    y0=sum(w*q for w,q in zip((.272228718,.674081683,.0536895208),v))
    t=min(1,max(0,(y0-.0625)/.9375))
    gain=.75+.5*t*t*(3-2*t)
    scale=(.015625+(gain*y0-.015625)/1.25)/(max(1e-6,y0)*.875)
    n=0
    if k>=2:
        n=2*dither(x,y,5 if k==3 else 31)-1
        if k==3: n=math.copysign(1-math.sqrt(1-abs(n)),n)
    return (*(q*scale+n*.0625 for q in v),a**2)
def constants(k):
    c=bytearray(b'\xcd'*512)
    for off,value in ((80,1.25),(104,.75),(108,.0625),(120,.015625),(140,.875),(148,1.25),(256+44,.0625),(256+48,2.)):
        struct.pack_into('<f',c,off,value)
    for off,value in ((32,k>=2),(36,k==3),(40,k==3),(52,k!=1),(92,37)):
        struct.pack_into('<I',c,256+off,value)
    return c
def rtz(x):
    h,=struct.unpack('<H',struct.pack('<e',x))
    if abs(struct.unpack('<e',struct.pack('<H',h))[0])>abs(x): h-=1
    return struct.unpack('<e',struct.pack('<H',h))[0]
def write(folder):
    def floats(name,vals):
        vals=list(vals); (folder/name).write_bytes(struct.pack(f'<{len(vals)}f',*vals))
    floats('ui.bin',(v for y in range(3) for x in range(4) for v in texel(x,y)))
    floats('noise.bin',(dither(x,y,z) for z in range(32) for y in range(H) for x in range(W)))
    for k in range(4):
        (folder/f'cb{k}.bin').write_bytes(constants(k))
        floats(f'gold{k}.bin',(rtz(v) for y in range(H) for x in range(W) for v in expected(x,y,k)))
if __name__=='__main__': write(Path(sys.argv[1]))
