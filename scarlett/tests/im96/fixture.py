"""Synthetic inputs and an analytic viewport/texture/colour oracle, independent of the VS/PS."""
import math
import struct
import sys
from pathlib import Path

object_rows = [0,-1,0,11, 2,0,0,22, 1,0,1,33, -3,5,1,44]
projection = [0,-.25,0,0, -.5,0,0,0, 0,.25,.25,0, 1.5,0,0,2]
vertices = [(x,y,z, (255 if x else 0,255 if y else 0,0 if x else 255,0 if y else 255),
             (1.75-x/8, -.75+y/8)) for x,y,z in [(0,0,1),(4,0,2),(0,4,3),(4,4,4)]]
sentinel = (-9,-7,-5,-3)

def pixel(g,x,y):
    if g == 'src17':
        # Locally affine texture; all four channels distinct, including negative and >1 values.
        return tuple(n/8192 for n in (11211+383*x+197*y, -5347+113*x-267*y,
                                      3451+887*x-23*y, 7811-189*x+71*y))
    if g == 'src18': return (8+x,16+y,32+x,64+y)
    if g == 'dst40': return (-40,-41,-42,-43)
    assert g == 'dst41'
    return sentinel

def rtz(x):
    if not x: return 0.
    unit = 2**max(-24, math.frexp(abs(x))[1]-11)
    return math.trunc(x/unit)*unit

def expected(x,y):
    if not (2 <= x < 6 and 2 <= y < 6): return sentinel
    i,j = x-2,y-2
    u,v = (i+.5)/4,(j+.5)/4
    return tuple(rtz(a*b) for a,b in zip(pixel('src17',5-i,2+j),(u,v,1-u,1-v)))

roles = ('src17','src18','dst40','dst41')
def write(folder):
    def floats(name,seq):
        values=list(seq)
        (folder/(name+'.bin')).write_bytes(struct.pack(f'<{len(values)}f',*values))
    floats('object',object_rows)
    floats('camera', [13]*88 + projection + [17]*24)
    (folder/'vertices.bin').write_bytes(b''.join(struct.pack('<3f4B2f',x,y,z,*c,*uv) for x,y,z,c,uv in vertices))
    for role in roles:
        floats(role,(k for y in range(8) for x in range(8) for k in pixel(role,x,y)))
    floats('expected',(k for y in range(8) for x in range(8) for k in expected(x,y)))
if __name__ == '__main__':
    write(Path(sys.argv[1]))
