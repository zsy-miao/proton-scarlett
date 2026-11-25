"""Build only project-owned fixture stages, verifying digests before emitting the provider header."""
import hashlib,os,sys,subprocess as sp,tempfile
from pathlib import Path

s=Path(__file__).resolve().parent
n=s.parents[1]/'native'
stages=[('vs',n/'im_default_pos_cpv_tex.hlsl','465f1398375291c203017dd2bd027ac447609c75cddcdac652a7033da233b607'),
        ('ps',n/'im_default_pos_cpv_tex.hlsl','7d37c5ab2fff9fe24f1a1edfff11db2ae474afa6579825dbc2ddda34fe224db5'),
        ('gs',s/'rect.hlsl','bfe064b3ca2d0f6366b6390e0c11f3051c258da79d877cd6a6406e7f79edd7b5')]
with tempfile.TemporaryDirectory() as tmp:
    h=['/* Project DXIL; regenerate with tests/im96/factory-shaders.py. */']
    for e,src,want in stages:
        out=Path(tmp)/e
        sp.run([os.getenv('DXC','dxc'),'-WX','-Ges','-Gis','-O3','-Qstrip_debug','-Qstrip_reflect','-T',e+'_6_0','-E',e,'-Fo',out,src],check=True)
        b=out.read_bytes()
        if hashlib.sha256(b).hexdigest()!=want:raise SystemExit('unexpected DXIL '+e)
        h+=['static const unsigned char im96_'+e+'[]={']
        h+=['    '+','.join(f'0x{x:02x}' for x in b[i:i+24])+',' for i in range(0,len(b),24)]
        h+=['};']
    Path(sys.argv[1]).write_text('\n'.join(h)+'\n')
