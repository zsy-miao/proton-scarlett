"""Run the actual private stream translator natively. Original stages stay in memory."""
import ctypes as C
import hashlib
from pathlib import Path
import subprocess as sp
import sys
import tempfile

n=Path(__file__).resolve().parents[2]/'native'
f = Path(__file__).resolve().parent / 'factory'
private = Path(sys.argv[1]).resolve()
sp.run(['sha256sum','--check','--status',f/'inputs.sha256'],cwd=private,check=True)
with tempfile.TemporaryDirectory() as tmp:
    libpath=Path(tmp)/'cpu.so'
    sp.run([sys.executable,'-IB',f.parent/'factory-shaders.py',Path(tmp)/'im96_host_dxil.h'],check=True)
    sp.run(['gcc','-std=c11','-O2','-Wall','-Wextra','-Werror','-fPIC','-shared','-I',n,'-I',tmp,'-x','c','-','-o',libpath],check=True,text=True,input=r'''
#include <stdint.h>
#include <string.h>
typedef uint8_t BYTE;typedef uintptr_t UINT_PTR,SIZE_T;typedef uint64_t UINT64;typedef unsigned UINT;
typedef void IUnknown,ID3D12RootSignature;typedef int HRESULT;
#define DXGI_FORMAT_R32G32B32_FLOAT 6
#define DXGI_FORMAT_R8G8B8A8_UNORM 28
#define DXGI_FORMAT_R32G32_FLOAT 16
#define D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA 0
#define D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE 3
#define E_NOTIMPL (-1)
#define E_POINTER (-2)
#define S_OK 0
#define FAILED(x) ((x)<0)
typedef struct {const char* SemanticName; UINT SemanticIndex,Format,InputSlot,AlignedByteOffset,InputSlotClass,InstanceDataStepRate;} D3D12_INPUT_ELEMENT_DESC;
int (*check_hash)(const void*,UINT,const BYTE*);
#define shader_digest_matches check_hash
static int copy_in(void*d,const void*s,size_t n){if(!s)return 0;memcpy(d,s,n);return 1;}
int acquired=0;
HRESULT xs_im96_private_layout(IUnknown *device,IUnknown *alias,ID3D12RootSignature **root){
    if(device!=(void*)33 || alias!=(void*)42)return -3;
    ++acquired;*root=(void*)96;return 0;}
#include "im96_pipeline.h"
int test(UINT_PTR *q,const void *vs,const void *ps,UINT_PTR owner,UINT_PTR alias){
    return im96_prepare((void*)owner,q,(void*)alias,vs,ps);}
const BYTE *expected(void){return im96_fixed;}
''')
    lib=C.CDLL(str(libpath))

vs=C.create_string_buffer((private/'vs96.bin').read_bytes())
ps=C.create_string_buffer((private/'ps96.bin').read_bytes())

fn=C.CFUNCTYPE(C.c_int,C.c_void_p,C.c_uint,C.c_void_p)
hash_check=fn(lambda p,n,h:hashlib.sha256(C.string_at(p,n)).digest()==C.string_at(h,32))
C.c_void_p.in_dll(lib,'check_hash').value=C.cast(hash_check,C.c_void_p).value
lib.expected.restype=C.c_void_p
measured=C.string_at(lib.expected(),32).hex()
class Element(C.Structure):
    _fields_=[('name',C.c_char_p)]+[(k,C.c_uint) for k in ('index','fmt','slot','offset','cls','step')]
ia=(Element*3)(Element(b'POSITION',0,6,0,0,0,0),Element(b'COLOR',0,28,0,12,0,0),Element(b'TEXCOORD',0,16,0,16,0,0))
Q=C.c_uint64*86
base=Q.from_buffer_copy((private/'fixed96.bin').read_bytes())
assert hashlib.sha256(bytes(base)).hexdigest()==measured
lib.test.argtypes=[C.c_void_p]*3+[C.c_size_t]*2
def run(want,owner=33,alias=42,mut=None):
    q=Q.from_buffer_copy(base);q[77]=C.addressof(ia)
    if mut:q[mut[0]]^=mut[1]
    refs=C.c_int.in_dll(lib,'acquired');before=refs.value
    assert lib.test(q,vs,ps,owner,alias)==want and refs.value==before+(want==0)
    if want==0:
        assert q[1]==96 and q[77] not in (0,C.addressof(ia)) and q[79]==3<<32|14
        for i in set(range(86))-{1,3,4,6,7,15,16,77,79}:assert q[i]==base[i],i
        for i,sha in [(3,'465f1398375291c203017dd2bd027ac447609c75cddcdac652a7033da233b607'),(6,'7d37c5ab2fff9fe24f1a1edfff11db2ae474afa6579825dbc2ddda34fe224db5'),(15,'bfe064b3ca2d0f6366b6390e0c11f3051c258da79d877cd6a6406e7f79edd7b5')]:
            assert hashlib.sha256(C.string_at(q[i],q[i+1])).hexdigest()==sha
run(0);run(-3,owner=0);run(-3,alias=41)
for i in set(range(86))-{77}:run(-1,mut=(i,1))
for k in ('index','fmt','slot','offset','cls','step'):
    old=getattr(ia[1],k);setattr(ia[1],k,old+1);run(-1);setattr(ia[1],k,old)
ia[0].name=b'NORMAL';run(-1);ia[0].name=None;run(-2);ia[0].name=b'POSITION'
for b in (vs,ps):
    k=len(b)-2;old=b[k];b[k]=bytes([old[0]^1]);run(-1);b[k]=old
print('PASS actual fixed digest + both stage hashes in guarded translator; private VS/PS/rect GS digests, alias/owner, exact IA, untouched fixed bytes and mutation negatives')
