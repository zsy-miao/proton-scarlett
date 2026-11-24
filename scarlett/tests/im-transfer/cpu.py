"""Execute the dumped PS math/control flow natively, with CPU fixture DX op shims."""
import ctypes as C
import math, os, re, struct, subprocess as sp, sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
from data import W,H,expected,rtz

game=Path(sys.argv[1]).resolve()
dxc=os.environ.get('DXC','dxc')
text=sp.check_output([dxc,'-dumpbin',game/'ps.dxil'],text=True)
body=re.search(r'define void @ps\(\) \{.*?\n\}',text,re.S)[0]
assert all(re.search(rf'\b{binding},space1\b',text) for binding in ('t257','t274','s0','cb26','cb27'))
assert 'textureLoad.f32(i32 66' in body and 'sampleLevel.f32(i32 62' in body
assert 'float 0.000000e+00)  ; SampleLevel' in body
# Replace API boundaries only; arithmetic, CB indices, branches and SSA values are from DXC.
ir=re.sub(r', !dx\.precise !\d+', '', body)
ir=re.sub(r'^; <label>:(\d+).*',r'\1:',ir,flags=re.M)
ir=ir.replace('%dx.types.Handle','i32')
types=re.findall(r'^%dx\.types\.(?!Handle)[^\n]* = type .*$',text,re.M)
defs=list(types)+['@xyuv = global [4 x float] zeroinitializer','@output = global [4 x float] zeroinitializer',
                   '@buffers = global [128 x i32] zeroinitializer','@ui = global [48 x float] zeroinitializer',
                   '@noise = global [2560 x float] zeroinitializer']
def fn(sig,*lines): defs.append('define '+sig+' {\n'+'\n'.join(lines)+'\n}')
fn('i32 @dx.op.createHandle(i32 %op,i8 %class,i32 %range,i32 %index,i1 %div)','ret i32 %index')
fn('float @dx.op.loadInput.f32(i32 %op,i32 %reg,i32 %row,i8 %col,i32 %axis)',
   '%c = zext i8 %col to i32','%idx = add i32 %reg,%c','%p = getelementptr float,ptr @xyuv,i32 %idx','%x = load float,ptr %p','ret float %x')
fn('void @dx.op.storeOutput.f32(i32 %op,i32 %reg,i32 %row,i8 %col,float %v)',
   '%p = getelementptr float,ptr @output,i8 %col','store float %v,ptr %p','ret void')
for typ in ('f32','i32'):
    ret='%dx.types.CBufRet.'+typ
    fn(ret+' @dx.op.cbufferLoadLegacy.'+typ+'(i32 %op,i32 %handle,i32 %reg)',
       '%b = sub i32 %handle,26','%p = getelementptr [16 x [4 x i32]],ptr @buffers,i32 %b,i32 %reg',
       '%v = load '+ret+',ptr %p','ret '+ret+' %v')
ret='%dx.types.ResRet.f32'
def aggregate(n):
    lines=[]
    for i in range(n):
        lines.extend([f'%p{i} = getelementptr float,ptr %ptr,i32 {i}',f'%x{i} = load float,ptr %p{i}',
                      f'%r{i} = insertvalue {ret} '+('undef' if i==0 else f'%r{i-1}')+f',float %x{i},{i}'])
    return lines+['ret '+ret+f' %r{n-1}']
fn(ret+' @dx.op.sampleLevel.f32(i32 %op,i32 %handle,i32 %s,float %u,float %v,float %c,float %d,i32 %ox,i32 %oy,i32 %oz,float %lod)',
   '%a = fmul float %u,4.0','%b = fmul float %v,3.0','%x = fptoui float %a to i32','%y = fptoui float %b to i32',
   '%ptr = getelementptr [4 x [4 x float]],ptr @ui,i32 %y,i32 %x',*aggregate(4))
fn(ret+' @dx.op.textureLoad.f32(i32 %op,i32 %handle,i32 %mip,i32 %x,i32 %y,i32 %z,i32 %ox,i32 %oy,i32 %oz)',
   '%ptr = getelementptr [8 x [10 x float]],ptr @noise,i32 %z,i32 %y,i32 %x',*aggregate(1))
for n,argc in [('fabs',1),('maxnum',2),('minnum',2),('sqrt',1),('log2',1),('exp2',1),('fma',3)]:
    defs.append(f'declare float @llvm.{n}.f32('+','.join(['float']*argc)+')')
fn('float @dx.op.unary.f32(i32 %op,float %v)',
   'switch i32 %op,label %bad [i32 6,label %abs i32 7,label %sat i32 21,label %exp i32 23,label %log i32 24,label %sqrt]',
   'bad: unreachable','abs: %a = call float @llvm.fabs.f32(float %v)','ret float %a',
   'sat: %l = call float @llvm.maxnum.f32(float 0.0,float %v)','%s = call float @llvm.minnum.f32(float 1.0,float %l)','ret float %s',
   'exp: %e = call float @llvm.exp2.f32(float %v)','ret float %e','log: %g = call float @llvm.log2.f32(float %v)','ret float %g',
   'sqrt: %r = call float @llvm.sqrt.f32(float %v)','ret float %r')
fn('float @dx.op.binary.f32(i32 %op,float %x,float %y)','%v = call float @llvm.maxnum.f32(float %x,float %y)','ret float %v')
fn('float @dx.op.tertiary.f32(i32 %op,float %x,float %y,float %z)','%v = call float @llvm.fma.f32(float %x,float %y,float %z)','ret float %v')
fn('i32 @dx.op.legacyF32ToF16(i32 %op,float %x)','%h = fptrunc float %x to half','%b = bitcast half %h to i16','%i = zext i16 %b to i32','ret i32 %i')
fn('float @dx.op.legacyF16ToF32(i32 %op,i32 %x)','%b = trunc i32 %x to i16','%h = bitcast i16 %b to half','%f = fpext half %h to float','ret float %f')
assert set(re.findall(r'binary.f32\(i32 (\d+)',body))=={'35'} and set(re.findall(r'tertiary.f32\(i32 (\d+)',body))=={'46'}
# Both sample shims only model valid fixture coordinates/LOD and assigned registers.
for op,bind in [('sampleLevel','257'),('textureLoad','274')]:
    handle=re.search(r'(%\d+) = call .*?createHandle\(i32 57, i8 0, i32 \d+, i32 '+bind+',',body)[1]
    assert re.search(rf'{op}\.f32\(i32 \d+, %dx.types.Handle {handle},',body)
base=game.parent/'cpu-transfer';base.mkdir(exist_ok=True)
(base/'ps.ll').write_text('\n'.join(defs+[ir]))
sp.run([os.environ.get('CLANG','clang'),'-Wno-override-module','-O2','-march=native','-fPIC','-c',base/'ps.ll','-o',base/'ps.o'],check=True)
sp.run(['gcc','-shared','-Wl,-z,defs',base/'ps.o','-lm','-o',base/'ps.so'],check=True)
lib=C.CDLL(str(base/'ps.so'))
def copy_input(name,file,size):
    raw=file.read_bytes()
    if len(raw)!=size: raise ValueError(f'{file}: expected {size} bytes')
    C.memmove((C.c_char*size).in_dll(lib,name),raw,size)
copy_input('ui',game/'ui.bin',48*4)
copy_input('noise',game/'noise.bin',2560*4)
xy=(C.c_float*4).in_dll(lib,'xyuv');out=(C.c_float*4).in_dll(lib,'output')
for k in range(4):
    copy_input('buffers',game/f'cb{k}.bin',512)
    for y in range(1,H-1):
        for x in range(1,W-1):
            xy[:]=[x+.5,y+.5,1-(x-.5)/(W-2),(y-.5)/(H-2)];lib.ps()
            for got,ref in zip(out,expected(x,y,k)):
                want=rtz(ref)
                assert got==rtz(got) and abs(got-want)<=2**max(-24,math.frexp(abs(want))[1]-11),(k,x,y,got,want)
    print(f'PASS CPU mode5 case {k}: 48 point samples, 192 output lanes')
