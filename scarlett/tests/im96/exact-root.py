"""Generate a DXC root signature from the project-owned sampler field declarations."""
import pathlib, re, sys
src=pathlib.Path(__file__).resolve().parents[2]/'native/im96_root.h'
samplers=re.findall(r'^\s*IM96_S\((\d+),([^)]*)\)',src.read_text(),re.M)
assert len(samplers)==22 and {int(reg) for reg,_ in samplers}==set(range(22))
parts=['RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT)',
       'DescriptorTable(SRV(t7,space=1,flags=DESCRIPTORS_VOLATILE), SRV(t277,space=1,flags=DESCRIPTORS_VOLATILE), CBV(b0,space=1,flags=DESCRIPTORS_VOLATILE))']
for reg,fields in samplers:
    filt,u,v,w,aniso,cmp,border=fields.split(',')
    parts.append(f'StaticSampler(s{reg},space=1,visibility=SHADER_VISIBILITY_ALL,filter=FILTER_{filt},'
                 f'addressU=TEXTURE_ADDRESS_{u},addressV=TEXTURE_ADDRESS_{v},addressW=TEXTURE_ADDRESS_{w},'
                 f'mipLODBias=0,maxAnisotropy={aniso},comparisonFunc=COMPARISON_{cmp},'
                 f'borderColor=STATIC_BORDER_COLOR_{border},minLOD=0,maxLOD=12)')
pathlib.Path(sys.argv[1]).write_text('#define EXACT "'+', '.join(parts)+'"\n')
