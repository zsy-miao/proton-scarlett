"""Write fixture-owned three-record windows and independent pixel-centre oracles."""
import struct
import sys
from pathlib import Path

from fixture import projection, sentinel

g = Path(sys.argv[1])
# Preserve the strip's two-stage XY transform; clip.w = 2 + z/4.
proj = projection.copy()
proj[11], proj[15] = .25, 1.75
# Constant clip depth even for the unequal-W control, so only W rejects it.
proj[10], proj[14] = 0, .5
(g/'camera.bin').write_bytes(struct.pack('<128f', *([13]*88 + proj + [17]*24)))
vb = bytearray([0xcd]*4080)
for case, first in enumerate([167, 1, 4, 7]):
    corners = [(0,0),(0,4),(4,4)]
    if case == 2:
        corners.reverse()
    for n,(x,y) in enumerate(corners):
        colour = (bool(x), bool(y), not x, not y) if case == 1 else (1,1,1,1)
        struct.pack_into('<3f4B2f', vb, 24*(first+n), x,y,int(case==3 and n==2),
                         *(c*255 for c in colour), x/4,y/4)
    gold = bytearray()
    for y in range(8):
        for x in range(8):
            c = sentinel
            if case < 2 and 2<=x<6 and 2<=y<6:
                i,j = x-2,y-2
                # Each of the 16 RGBA8 texels has a unique binary signature.
                c = (i%2,i//2,j%2,j//2)
                if case == 1:
                    u,v = (i+.5)/4,(j+.5)/4
                    c = tuple(a*b for a,b in zip(c,(u,v,1-u,1-v)))
            gold.extend(struct.pack('<4f',*c))
    (g/f'rect{case}.bin').write_bytes(gold)
(g/'rect-vb.bin').write_bytes(vb)
