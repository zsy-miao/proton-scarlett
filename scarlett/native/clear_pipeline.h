#include "clear_pair_dxil.h"
#include "clear_srgb_dxil.h"

/* Reuse PSO lifetime/identity, but never identify graphics PSOs as BVH kernels. */
static HRESULT WINAPI clear_name(IUnknown *i,const WCHAR *name)
{
    (void)i; (void)name;
    return E_NOTIMPL;
}
static const struct bp_methods clear_methods={{bp_query,bp_addref,bp_release},
    {xs_stop,xs_stop,xs_stop},clear_name,xs_stop,xs_stop};

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
    if (rtv==DXGI_FORMAT_R8G8B8A8_UNORM && (UINT)(dsv_word>>32)==DXGI_FORMAT_D32_FLOAT_S8X24_UINT)
        q[0x2a8/8]=(UINT)dsv_word;
    if (srgb) q[0x280/8]=(UINT)q[0x280/8]|((UINT64)DXGI_FORMAT_R8G8B8A8_UNORM<<32);
    if (!shader_digest_matches((BYTE *)q,sizeof q,fixed_digest)) return E_NOTIMPL;
    q[0x2a8/8]=dsv_word;
    if (srgb) q[0x280/8]=(UINT)q[0x280/8]|((UINT64)rtv<<32);
    if (!copy_in(vs,v,sizeof vs) || !copy_in(ps,s,sizeof ps)) return E_POINTER;
    if (!shader_digest_matches(vs,sizeof vs,vs_digest) || !shader_digest_matches(ps,sizeof ps,ps_digest))
        return E_NOTIMPL;
    struct xbox_device *d=CONTAINING_RECORD(device,struct xbox_device,iface);
    AcquireSRWLockShared(&d->descriptor_lock);
    struct bvh_root *r=d->bvh_roots;
    while (r && &r->iface!=guest_root) r=r->next;
    ID3D12RootSignature *host=r && r->owner==d && r->kind==ROOT_SRV_SPACE3 ? r->host : NULL;
    if (host) ID3D12RootSignature_AddRef(host);
    ReleaseSRWLockShared(&d->descriptor_lock);
    if (!host) return E_INVALIDARG;
    q[1]=(UINT_PTR)host;
    q[3]=(UINT_PTR)clear_vs_dxil; q[4]=sizeof clear_vs_dxil;
    q[6]=(UINT_PTR)(srgb?clear_srgb_dxil:clear_ps_dxil); q[7]=srgb?sizeof clear_srgb_dxil:sizeof clear_ps_dxil;
    sd.pPipelineStateSubobjectStream=q;
    ID3D12Device2 *d2=NULL;
    ID3D12PipelineState *pso=NULL;
    HRESULT hr=ID3D12Device_QueryInterface(d->host,&IID_ID3D12Device2,(void **)&d2);
    if (SUCCEEDED(hr)) {
        hr=ID3D12Device2_CreatePipelineState(d2,&sd,&IID_ID3D12PipelineState,(void **)&pso);
        ID3D12Device2_Release(d2);
    }
    if (FAILED(hr)) { ID3D12RootSignature_Release(host); return hr; }
    struct bvh_pipeline *p=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof *p);
    if (!p) {
        ID3D12PipelineState_Release(pso);
        ID3D12RootSignature_Release(host);
        return E_OUTOFMEMORY;
    }
    p->iface.lpVtbl=&clear_methods.unknown; p->refs=1;
    p->owner=d; IUnknown_AddRef(device);
    p->root=host; p->host=pso;
    *out=&p->iface;
    OutputDebugStringA("scarlett: created qualified clear-pair graphics PSO\n");
    return S_OK;
}
