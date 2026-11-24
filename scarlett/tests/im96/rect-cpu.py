"""Execute the emitted VS and GS bodies; only fetch and stream ops are supplied."""
import ctypes as C
import os
import re
import struct
import subprocess as sp
import sys
from pathlib import Path
from tempfile import TemporaryDirectory

g = Path(sys.argv[1]).resolve()
ir = Path(__file__).with_name('ops.ll').read_text()
for name,stage in [('vs','vs'), ('rect','gs')]:
    dump = sp.check_output([os.environ.get('DXC','dxc'),'-dumpbin',g/(name+'.dxil')],text=True)
    body = re.search(r'define void @'+stage+r'\(\) \{.*?\n\}', dump, re.S)[0]
    if stage == 'gs':
        for op in ('loadInput.f32','isSpecialFloat.f32','emitStream','cutStream','emitThenCutStream'):
            body = body.replace('dx.op.'+op, op.split('.')[0])
        ir += '''
declare float @loadInput(i32,i32,i32,i8,i32)
declare i1 @isSpecialFloat(i32,float)
declare void @emitStream(i32,i8)
declare void @cutStream(i32,i8)
declare void @emitThenCutStream(i32,i8)
'''
    ir += re.sub(r', !dx\.precise !\d+', '', body).replace('%dx.types.Handle','i32')+'\n'

with TemporaryDirectory(dir=g.parent) as td:
    obj = Path(td)/'rect.o'
    sp.run([os.environ.get('CLANG','clang'),'-O2','-march=native','-fPIC','-Wno-override-module',
            '-x','ir','-c','-','-o',obj],input=ir,text=True,check=True)
    shim = '''
#include <assert.h>
#include <math.h>
#include <string.h>
extern float outputs[12];
float src[3][12], dst[6][12]; int count,cut;
float loadInput(int op,int reg,int row,char col,int vertex) {
    assert(op==4 && !row && vertex>=0 && vertex<3 && reg>=0 && reg<3 && col>=0 && col<4);
    return src[vertex][4*reg+col];
}
_Bool isSpecialFloat(int op,float v) {assert(op==10);return isfinite(v);}
void emitStream(int op,char s){assert(!s && op==97 && count<6);memcpy(dst[count++],outputs,sizeof outputs);}
void cutStream(int op,char s){assert(!s && op==98);cut=count;}
void emitThenCutStream(int op,char s){assert(op==99);emitStream(97,s);cutStream(98,s);}
'''
    so = obj.with_suffix('.so')
    sp.run(['gcc','-O2','-shared','-fPIC','-Wall','-Werror','-Wl,-z,defs',obj,'-x','c','-','-lm','-o',so],
           input=shim,text=True,check=True)
    lib = C.CDLL(str(so))

def array(name,n):
    return (C.c_float*n).in_dll(lib,name)

for name,size in [('object',16),('camera',128)]:
    array(name,size)[:] = struct.unpack(f'<{size}f',(g/(name+'.bin')).read_bytes())
src, dst, inp, out = (array(name,size) for name,size in [('src',36),('dst',72),('inputs',12),('outputs',12)])
count, cut = (C.c_int.in_dll(lib,k) for k in ('count','cut'))
vb = (g/'rect-vb.bin').read_bytes()
for case,first in enumerate([167,1,4,7]):
    for i in range(3):
        x,y,z,*a = struct.unpack_from('<3f4B2f',vb,24*(first+i))
        inp[:] = [x,y,z,0,*(c/255 for c in a[:4]),*a[4:],0,0]
        lib.vs()
        assert out[:4] == [.5*x-1,1-.5*y,.5,2+.25*z] and out[4:10] == inp[4:10]
        src[i*12:i*12+12] = out[:]
    count.value=cut.value=0
    lib.gs()
    if case >= 2:
        assert (count.value,cut.value)==(0,0)
        continue
    assert (count.value,cut.value)==(6,3)
    for i,(u,v) in enumerate([(0,0),(0,1),(1,1),(0,0),(1,1),(1,0)]):
        gold = [2*u-1,1-2*v,.5,2, *([u,v,1-u,1-v] if case else [1]*4), u,v]
        assert dst[12*i:12*i+10] == gold
print('PASS CPU VS71 + rect GS: producer and gradient emitted two correctly wound triangles with exact UV/RGBA; wrong order / unequal W emitted nothing')
