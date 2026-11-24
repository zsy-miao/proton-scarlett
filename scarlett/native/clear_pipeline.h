#include "clear_pair_dxil.h"
#include "clear_srgb_dxil.h"
#include "clear_diag_dxil.h"

/* Reuse PSO lifetime/identity, but never identify graphics PSOs as BVH kernels. */
static HRESULT WINAPI clear_name(IUnknown *i,const WCHAR *name)
{
    (void)i; (void)name;
    return E_NOTIMPL;
}
struct clear_wrapper {struct bvh_pipeline pso; IUnknown *root; struct clear_wrapper *next; ID3D12PipelineState *diagonal; BOOL colour;};
static ULONG WINAPI clear_release(IUnknown *i)
{
    struct clear_wrapper *p=CONTAINING_RECORD(i,struct clear_wrapper,pso.iface);
    struct xbox_device *d=p->pso.owner;
    AcquireSRWLockExclusive(&d->descriptor_lock);
    ULONG refs=InterlockedDecrement(&p->pso.refs);
    if(!refs && p->root){
        struct clear_wrapper **link=p->colour?&d->colour_psos:&d->clear4;while(*link!=p)link=&(*link)->next;
        *link=p->next;
    }
    ReleaseSRWLockExclusive(&d->descriptor_lock);
    if(!refs){
        if(p->diagonal)ID3D12PipelineState_Release(p->diagonal);
        if(p->root)IUnknown_Release(p->root);
        ID3D12PipelineState_Release(p->pso.host);ID3D12RootSignature_Release(p->pso.root);
        IUnknown_Release(&d->iface);HeapFree(GetProcessHeap(),0,p);
    }
    return refs;
}
static const struct bp_methods clear_methods={{bp_query,bp_addref,clear_release},
    {xs_stop,xs_stop,xs_stop},clear_name,xs_stop,xs_stop};

#include "blit_colour_dxil.h"
static ID3D12RootSignature *retain_graphics_kind(IUnknown *,IUnknown *,enum bvh_root_kind);
/* Factory only. No colour PSO goes into the #4 list registry. Rectangle draws require assembly. */
static HRESULT blit_colour_pipeline(IUnknown *dev,UINT_PTR *q,IUnknown *root,const void *vs,const void *ps)
{
    static const BYTE fixed[]={0x98,0x64,0x8d,0xd6,0xe6,0x16,0x32,0x4d,0x39,0x91,0x4a,0xe6,0x83,0xef,0xcc,0x6a,0x18,0xc1,0x10,0x65,0x83,0xc4,0xe3,0xde,0xcf,0x2f,0xb6,0x1f,0xae,0x53,0x90,0x83},vhash[]={0x67,0x2f,0x38,0x9b,0x67,0xf2,0x17,0xe8,0xcf,0x80,0x54,0x42,0xd2,0xc7,0x7f,0xab,0x41,0x5b,0x0b,0x01,0xf6,0x92,0x41,0xd1,0x83,0x86,0x63,0x9a,0x31,0x3e,0x12,0x98},phash[]={0xda,0x48,0x29,0xb1,0x4a,0xfd,0xf3,0xd0,0xf6,0xdc,0x8d,0x44,0xc8,0xfd,0xbf,0x3c,0xfb,0x8b,0x84,0xbc,0xd7,0xb8,0xda,0xcd,0xf4,0x03,0xc0,0xd4,0x56,0xa8,0x11,0xea};
    static const D3D12_INPUT_ELEMENT_DESC ia[]={
        {"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
        {"COLOR",0,DXGI_FORMAT_R8G8B8A8_UNORM,0,12,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
        {"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,16,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0}};
    const void *layout=(void*)q[0x268/8];q[0x268/8]=0;
    if(!shader_digest_matches((BYTE*)q,0x2b0,fixed))return E_NOTIMPL;
    D3D12_INPUT_ELEMENT_DESC in[3];BYTE v[1100],p[1180];
    if(!copy_in(in,layout,sizeof in)||!copy_in(v,vs,sizeof v)||!copy_in(p,ps,sizeof p))return E_POINTER;
    for(UINT k=0;k<3;k++){
        char name[9];SIZE_T len=strlen(ia[k].SemanticName)+1;
        if(!copy_in(name,in[k].SemanticName,len))return E_POINTER;
        if(memcmp(name,ia[k].SemanticName,len)||in[k].SemanticIndex!=0||in[k].Format!=ia[k].Format||in[k].InputSlot ||
            in[k].AlignedByteOffset!=ia[k].AlignedByteOffset||in[k].InputSlotClass!=ia[k].InputSlotClass||in[k].InstanceDataStepRate)return E_NOTIMPL;
    }
    if(!shader_digest_matches(v,sizeof v,vhash)||!shader_digest_matches(p,sizeof p,phash))return E_NOTIMPL;
    ID3D12RootSignature *host=retain_graphics_kind(dev,root,ROOT_BLIT_COLOUR);if(!host)return E_INVALIDARG;
    q[1]=(UINT_PTR)host;q[3]=(UINT_PTR)colour_vs;q[4]=sizeof colour_vs;q[6]=(UINT_PTR)colour_ps;q[7]=sizeof colour_ps;
    q[0x268/8]=(UINT_PTR)ia;q[0x278/8]=(UINT)q[0x278/8]|((UINT64)D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE<<32);
    return S_OK;
}
static HRESULT WINAPI clear_pipeline(IUnknown *device,const D3D12_PIPELINE_STATE_STREAM_DESC *desc,
        REFIID iid,void **out)
{
    static const BYTE fixed_digest[32]={
        0x30,0x4e,0xe0,0x18,0x72,0x99,0x80,0x6e,0x5b,0x01,0x44,0x6c,0x5e,0xe6,0xe5,0x47,
        0x64,0x91,0xc9,0x7c,0x96,0x7b,0x18,0xdf,0x8f,0xeb,0x18,0xd7,0x9b,0x01,0x00,0x0f};
    static const BYTE vs_digest[32]={
        0x85,0x74,0xa8,0x17,0xbd,0x27,0x54,0xb2,0x60,0x39,0x87,0xe4,0x5c,0x84,0x51,0xdd,
        0xc2,0xec,0x15,0x64,0xa0,0x4e,0x7e,0x54,0x7f,0x68,0xa5,0xc6,0x34,0xa4,0x6b,0xd9};
    static const BYTE ps_digest[32]={
        0xc9,0xf9,0x9c,0xce,0x12,0x96,0x03,0xaf,0xe7,0x95,0x04,0x2c,0xce,0xd5,0x90,0xb5,
        0xf2,0x5e,0xb6,0x64,0x15,0x0a,0xbf,0xf1,0x91,0xf1,0x49,0x85,0x39,0xaa,0xb8,0x19};
    D3D12_PIPELINE_STATE_STREAM_DESC sd;
    UINT_PTR q[0x2b0/8];
    BYTE vs[992],ps[964];
    IID id;
    if (!out) return E_POINTER;
    *out=NULL;
    if (!copy_in(&id,iid,sizeof id)) return E_POINTER;
    if (!IsEqualIID(&id,&IID_ID3D12PipelineState)) return E_NOINTERFACE;
    if (!copy_in(&sd,desc,sizeof sd)) return E_POINTER;
    if (sd.SizeInBytes!=sizeof q) return E_NOTIMPL;
    if (!copy_in(q,sd.pPipelineStateSubobjectStream,sizeof q)) return E_POINTER;
    IUnknown *guest_root=(IUnknown *)q[1];
    const void *v=(void *)q[3],*s=(void *)q[6];
    q[1]=q[3]=q[6]=0;
    /* All tags, lengths, absent stages, input layout and fixed state are pinned. */
    UINT rtv=(UINT)(q[0x280/8]>>32);
    BOOL srgb=rtv==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    /* Prewarm #4 changes only DSVFormat; the linear shader and disabled depth/stencil state are identical. */
    UINT_PTR dsv_word=q[0x2a8/8];
    struct xbox_device *d=CONTAINING_RECORD(device,struct xbox_device,iface);
    BOOL colour=q[4]==1100 && q[7]==1180;ID3D12RootSignature *host;
    if(colour){
        HRESULT hr=blit_colour_pipeline(device,q,guest_root,v,s);if(FAILED(hr))return hr;
        host=(ID3D12RootSignature*)q[1];
    }else{
        if (rtv==DXGI_FORMAT_R8G8B8A8_UNORM && (UINT)(dsv_word>>32)==DXGI_FORMAT_D32_FLOAT_S8X24_UINT)
            q[0x2a8/8]=(UINT)dsv_word;
        if (srgb) q[0x280/8]=(UINT)q[0x280/8]|((UINT64)DXGI_FORMAT_R8G8B8A8_UNORM<<32);
        if (!shader_digest_matches((BYTE *)q,sizeof q,fixed_digest)) return E_NOTIMPL;
        q[0x2a8/8]=dsv_word;
        if (srgb) q[0x280/8]=(UINT)q[0x280/8]|((UINT64)rtv<<32);
        if (!copy_in(vs,v,sizeof vs) || !copy_in(ps,s,sizeof ps)) return E_POINTER;
        if (!shader_digest_matches(vs,sizeof vs,vs_digest) || !shader_digest_matches(ps,sizeof ps,ps_digest))
            return E_NOTIMPL;
        host=xs_retain_graphics_root(device,guest_root);
        if (!host) return E_INVALIDARG;
        q[1]=(UINT_PTR)host;
        q[3]=(UINT_PTR)clear_vs_dxil; q[4]=sizeof clear_vs_dxil;
        q[6]=(UINT_PTR)(srgb?clear_srgb_dxil:clear_ps_dxil); q[7]=srgb?sizeof clear_srgb_dxil:sizeof clear_ps_dxil;
    }
    sd.pPipelineStateSubobjectStream=q;
    ID3D12Device2 *d2=NULL;
    ID3D12PipelineState *pso=NULL,*diag=NULL;
    BOOL clear4=!colour && (UINT)(dsv_word>>32)==DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
    HRESULT hr=ID3D12Device_QueryInterface(d->host,&IID_ID3D12Device2,(void **)&d2);
    if (SUCCEEDED(hr)) {
        hr=ID3D12Device2_CreatePipelineState(d2,&sd,&IID_ID3D12PipelineState,(void **)&pso);
        if(SUCCEEDED(hr) && clear4){
            q[6]=(UINT_PTR)clear_diag_dxil;q[7]=sizeof clear_diag_dxil;
            hr=ID3D12Device2_CreatePipelineState(d2,&sd,&IID_ID3D12PipelineState,(void**)&diag);
        }
        ID3D12Device2_Release(d2);
    }
    if (FAILED(hr)) { if(pso)ID3D12PipelineState_Release(pso);IUnknown_Release(guest_root); return hr; }
    struct clear_wrapper *wrap=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof *wrap);
    if (!wrap) {
        if(diag)ID3D12PipelineState_Release(diag);
        ID3D12PipelineState_Release(pso);
        IUnknown_Release(guest_root);
        return E_OUTOFMEMORY;
    }
    struct bvh_pipeline *p=&wrap->pso;
    ID3D12RootSignature_AddRef(host);
    p->iface.lpVtbl=&clear_methods.unknown; p->refs=1;
    p->owner=d; IUnknown_AddRef(device);
    p->root=host; p->host=pso;
    if (colour || clear4){
        wrap->colour=colour;
        wrap->diagonal=diag;
        wrap->root=guest_root;
        AcquireSRWLockExclusive(&d->descriptor_lock);
        struct clear_wrapper **list=colour?&d->colour_psos:&d->clear4;wrap->next=*list;*list=wrap;
        ReleaseSRWLockExclusive(&d->descriptor_lock);
    }else IUnknown_Release(guest_root);
    *out=&p->iface;
    OutputDebugStringA(colour?"scarlett: created qualified colour PSO\n":"scarlett: created qualified clear-pair graphics PSO\n");
    return S_OK;
}
