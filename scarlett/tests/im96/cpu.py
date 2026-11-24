"""Run both DXC-generated entry point bodies with fixture-only DX operation shims."""
import ctypes as C
import os
import re
import struct
import subprocess as sp
import sys
from pathlib import Path
from tempfile import TemporaryDirectory

g = Path(sys.argv[1]).resolve()
bodies = []
for stage, bindings in [('vs',('cb0','t7')),('ps',('s14','t277'))]:
    dump = sp.check_output([os.environ.get('DXC','dxc'),'-dumpbin',g/(stage+'.dxil')],text=True)
    assert sorted(re.findall(r'\b((?:cb|s|t)\d+),space1\b',dump)) == sorted(bindings)
    body = re.search(r'define void @'+stage+r'\(\) \{.*?\n\}',dump,re.S)[0]
    bodies.append(re.sub(r', !dx\.precise !\d+', '',body).replace('%dx.types.Handle','i32'))

with TemporaryDirectory(dir=g.parent) as temp:
    libpath = Path(temp)/'draw.so'
    ops = (Path(__file__).with_name('ops.ll')).read_text()
    sp.run([os.environ.get('CLANG','clang'),'-O2','-march=native','-fPIC','-Wno-override-module',
            '-x','ir','-c','-','-o',libpath.with_suffix('.o')],input='\n'.join([ops,*bodies]),text=True,check=True)
    sp.run(['gcc','-shared','-Wl,-z,defs',libpath.with_suffix('.o'),'-lm','-o',libpath],check=True)
    lib = C.CDLL(str(libpath))

def array(name,n):
    return (C.c_float*n).in_dll(lib,name)
def data(name):
    return (g/(name+'.bin')).read_bytes()
def floats(name):
    return [x for x, in struct.iter_unpack('<f',data(name))]

inp,out = array('inputs',12),array('outputs',12)
array('object',16)[:] = floats('object')
array('camera',128)[:] = floats('camera')
corners = []
for x,y,z,*attrs in struct.iter_unpack('<3f4B2f',data('vertices')):
    inp[:] = [x,y,z,0,*(v/255 for v in attrs[:4]),*attrs[4:],0,0]
    lib.vs()
    assert out[:4] == [.5*x-1,1-.5*y,.25*(z+1),2]
    assert out[4:10] == inp[4:10]
    corners.append(out[:10])

def draw(source,destination):
    images = {key:floats(key) for key in ('src17','src18','dst40','dst41')}
    array('texture',256)[:] = images[source]
    # Coverage follows VS clip output. This single axis-aligned strip has constant W.
    a,b,c,d = corners
    def screen(p): return ((1+p[0]/p[3])*4,(1-p[1]/p[3])*4)
    l,t = screen(a); r,h = screen(d)
    assert screen(b)==(r,t) and screen(c)==(l,h)
    for y in range(8):
        for x in range(8):
            u,v = (x+.5-l)/(r-l),(y+.5-t)/(h-t)
            if not (0<u<1 and 0<v<1): continue
            weights = (1-u-v,u,v,0) if u+v<=1 else (0,1-v,1-u,u+v-1)
            inp[4:10] = [sum(w*p[k] for w,p in zip(weights,corners)) for k in range(4,10)]
            lib.ps()
            base = (8*y+x)*4
            images[destination][base:base+4] = out[:4]
    return {key for key,image in images.items() if image != floats('expected' if key=='dst41' else key)}

assert draw('src17','dst41') == set()
assert draw('src18','dst41') == {'dst41'}
assert draw('src17','dst40') == {'dst40','dst41'}
print('PASS CPU VS71: 4 transformed clip positions + RGBA/UV; PS86: 16 shaded pixels, 48 border pixels')
print('PASS src17 -> dst41: both sources and old dst40 unchanged; stale-source / stale-destination controls rejected')
