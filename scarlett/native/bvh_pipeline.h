#include <stdint.h>
#include "bvh_record.h"
#include "shader_digest.h"
#include "bvh_init_dxil.h"

struct bvh_pipeline {
    IUnknown iface;
    LONG refs;
    struct xbox_device *owner;
    ID3D12RootSignature *root; /* private translation layout */
    ID3D12PipelineState *host;
};
static ULONG WINAPI bp_addref(IUnknown *i)
{
    return InterlockedIncrement(&CONTAINING_RECORD(i,struct bvh_pipeline,iface)->refs);
}
static ULONG WINAPI bp_release(IUnknown *i)
{
    struct bvh_pipeline *p=CONTAINING_RECORD(i,struct bvh_pipeline,iface);
    ULONG refs=InterlockedDecrement(&p->refs);
    if (!refs) {
        ID3D12PipelineState_Release(p->host);
        ID3D12RootSignature_Release(p->root);
        IUnknown_Release(&p->owner->iface);
        HeapFree(GetProcessHeap(),0,p);
    }
    return refs;
}
static HRESULT WINAPI bp_query(IUnknown *i,REFIID iid,void **out)
{
    if (!out) return E_POINTER;
    *out=NULL;
    IID id;
    if (!copy_in(&id,iid,sizeof id)) return E_POINTER;
    if (!IsEqualIID(&id,&IID_IUnknown) && !IsEqualIID(&id,&IID_ID3D12PipelineState)) return E_NOINTERFACE;
    bp_addref(i); *out=i;
    return S_OK;
}
static HRESULT WINAPI bp_name(IUnknown *i,const WCHAR *name)
{
    static const WCHAR expected[]=L"XDXR InitPartitionedHeaders";
    WCHAR s[ARRAYSIZE(expected)];
    if (!copy_in(s,name,sizeof s)) return E_POINTER;
    if (memcmp(s,expected,sizeof s)) return E_NOTIMPL;
    return ID3D12PipelineState_SetName(CONTAINING_RECORD(i,struct bvh_pipeline,iface)->host,expected);
}
static const struct bp_methods {
    IUnknownVtbl unknown;
    xs_stop_fn gap18[3];
    __typeof__(&bp_name) name;
    xs_stop_fn get_device, cached_blob;
} bp_methods={{bp_query,bp_addref,bp_release},{xs_stop,xs_stop,xs_stop},bp_name,xs_stop,xs_stop};
_Static_assert(offsetof(struct bp_methods,name)==0x30,"PSO name slot");

static HRESULT WINAPI bootstrap_bvh_pipeline(IUnknown *device,const void *desc,UINT64 x,UINT64 y,REFIID iid,void **out)
{
    static const BYTE approved[32]={0xad,0xa7,0x13,0x02,0x58,0x9d,0xae,0x66,0x27,0xfb,0x5d,0xa6,0xd0,0xe7,0xc1,0x34,
        0x00,0x10,0x7c,0x6a,0xe5,0x6b,0xb1,0x5e,0x1d,0xca,0x58,0x48,0x79,0x23,0x23,0xad};
    struct { IUnknown *root; const void *shader; SIZE_T n; BYTE tail[32]; } w;
    _Static_assert(sizeof w==0x38,"console +1f0 descriptor");
    if (!out) return E_POINTER;
    *out=NULL;
    IID id;
    if (!copy_in(&id,iid,sizeof id)) return E_POINTER;
    if (!IsEqualIID(&id,&IID_ID3D12PipelineState)) return E_NOINTERFACE;
    if (x || y) return E_NOTIMPL;
    if (!copy_in(&w,desc,sizeof w)) return E_POINTER;
    BYTE code[0x1410];
    if (w.n!=sizeof code || memcmp(w.tail,(BYTE[32]){0},sizeof w.tail)) return E_NOTIMPL;
    struct xbox_device *d=CONTAINING_RECORD(device,struct xbox_device,iface);
    AcquireSRWLockShared(&d->descriptor_lock);
    struct bvh_root *r=d->bvh_roots;
    while (r && &r->iface!=w.root) r=r->next;
    BOOL known=r!=NULL;
    ReleaseSRWLockShared(&d->descriptor_lock);
    if (!known) return E_INVALIDARG;
    if (!copy_in(code,w.shader,sizeof code)) return E_POINTER;
    if (!shader_digest_matches(code,sizeof code,approved)) return E_NOTIMPL;

    D3D12_DESCRIPTOR_RANGE range={D3D12_DESCRIPTOR_RANGE_TYPE_UAV,1,0,0,0};
    D3D12_ROOT_PARAMETER rp[2]={
        {.ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE,.DescriptorTable={1,&range}},
        {.ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS,.Constants={0,0,8}}};
    D3D12_ROOT_SIGNATURE_DESC rs={2,rp,0,NULL,D3D12_ROOT_SIGNATURE_FLAG_NONE};
    ID3DBlob *blob=NULL;
    HRESULT hr=D3D12SerializeRootSignature(&rs,D3D_ROOT_SIGNATURE_VERSION_1,&blob,NULL);
    if (FAILED(hr)) return hr;
    struct bvh_pipeline *p=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof *p);
    if (!p) { ID3D10Blob_Release(blob); return E_OUTOFMEMORY; }
    hr=ID3D12Device_CreateRootSignature(d->host,1,ID3D10Blob_GetBufferPointer(blob),ID3D10Blob_GetBufferSize(blob),
            &IID_ID3D12RootSignature,(void **)&p->root);
    ID3D10Blob_Release(blob);
    if (SUCCEEDED(hr)) {
        D3D12_COMPUTE_PIPELINE_STATE_DESC pd={.pRootSignature=p->root,.CS={bvh_init_dxil,sizeof bvh_init_dxil}};
        hr=ID3D12Device_CreateComputePipelineState(d->host,&pd,&IID_ID3D12PipelineState,(void **)&p->host);
    }
    if (FAILED(hr)) {
        if (p->root) ID3D12RootSignature_Release(p->root);
        HeapFree(GetProcessHeap(),0,p);
        return hr;
    }
    IUnknown_AddRef(device); p->owner=d; p->refs=1; p->iface.lpVtbl=&bp_methods.unknown;
    *out=&p->iface;
    return S_OK;
}

struct xs_bvh_hold { IUnknown *pipeline; ID3D12Resource *dst; ID3D12DescriptorHeap *heap; };
void xs_drop_bvh_hold(struct xs_bvh_hold *h)
{
    if (!h) return;
    ID3D12DescriptorHeap_Release(h->heap); ID3D12Resource_Release(h->dst); IUnknown_Release(h->pipeline);
    HeapFree(GetProcessHeap(),0,h);
}
HRESULT xs_record_bvh_init(IUnknown *pipeline,ID3D12GraphicsCommandList *list,const struct xs_bvh_range *range,
        const void *initializer,SIZE_T bytes,struct xs_bvh_hold **out)
{
    if (!out) return E_POINTER;
    *out=NULL;
    if (!pipeline || pipeline->lpVtbl!=&bp_methods.unknown || !list || !range || !range->resource) return E_INVALIDARG;
    if (ID3D12GraphicsCommandList_GetType(list)!=D3D12_COMMAND_LIST_TYPE_COMPUTE) return E_INVALIDARG;
    struct { UINT64 addr; UINT words[8]; } init;
    _Static_assert(sizeof init==40,"PartitionedHeaderInitializer");
    if (bytes!=sizeof init) return E_INVALIDARG;
    if (!copy_in(&init,initializer,sizeof init)) return E_POINTER;
    const UINT n=init.words[5];
    /* Bound single-lane work as well as all UINT byte offsets. */
    if (n>65536) return E_NOTIMPL;
    const UINT size=n ? 84+28*(n-1) : 80;
    ID3D12Resource *dst=range->resource;
    ID3D12Device *device=CONTAINING_RECORD(pipeline,struct bvh_pipeline,iface)->owner->host;
    ID3D12Device *other=NULL;
    HRESULT hr=ID3D12GraphicsCommandList_GetDevice(list,&IID_ID3D12Device,(void **)&other);
    if (FAILED(hr)) return hr;
    BOOL match=other==device; ID3D12Device_Release(other);
    if (!match) return E_INVALIDARG;
    hr=ID3D12Resource_GetDevice(dst,&IID_ID3D12Device,(void **)&other);
    if (FAILED(hr)) return hr;
    match=other==device; ID3D12Device_Release(other);
    if (!match) return E_INVALIDARG;
    D3D12_RESOURCE_DESC rd; dst->lpVtbl->GetDesc(dst,&rd);
    UINT64 delta=init.addr-range->guest_base;
    if (rd.Dimension!=D3D12_RESOURCE_DIMENSION_BUFFER || !(rd.Flags&D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) ||
        !range->guest_base || range->bytes>UINT64_MAX-range->guest_base || init.addr<range->guest_base ||
        (init.addr&3) || range->resource_offset>rd.Width || range->bytes>rd.Width-range->resource_offset ||
        delta>range->bytes || size>range->bytes-delta) return E_INVALIDARG;
    UINT64 first=range->resource_offset+delta;
    if (first%D3D12_RAW_UAV_SRV_BYTE_ALIGNMENT) return E_INVALIDARG;
    struct xs_bvh_hold *h=HeapAlloc(GetProcessHeap(),0,sizeof *h);
    if (!h) return E_OUTOFMEMORY;
    D3D12_DESCRIPTOR_HEAP_DESC hd={D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,1,D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,1};
    hr=ID3D12Device_CreateDescriptorHeap(device,&hd,&IID_ID3D12DescriptorHeap,(void **)&h->heap);
    if (FAILED(hr)) { HeapFree(GetProcessHeap(),0,h); return hr; }
    h->dst=dst; ID3D12Resource_AddRef(dst); h->pipeline=pipeline; IUnknown_AddRef(pipeline);
    D3D12_CPU_DESCRIPTOR_HANDLE cpu; D3D12_GPU_DESCRIPTOR_HANDLE gpu;
    h->heap->lpVtbl->GetCPUDescriptorHandleForHeapStart(h->heap,&cpu);
    h->heap->lpVtbl->GetGPUDescriptorHandleForHeapStart(h->heap,&gpu);
    D3D12_UNORDERED_ACCESS_VIEW_DESC view={.Format=DXGI_FORMAT_R32_TYPELESS,.ViewDimension=D3D12_UAV_DIMENSION_BUFFER,
            .Buffer={.FirstElement=first/4,.NumElements=size/4,.Flags=D3D12_BUFFER_UAV_FLAG_RAW}};
    ID3D12Device_CreateUnorderedAccessView(device,dst,NULL,&view,cpu);
    struct bvh_pipeline *p=CONTAINING_RECORD(pipeline,struct bvh_pipeline,iface);
    ID3D12GraphicsCommandList_SetDescriptorHeaps(list,1,&h->heap);
    ID3D12GraphicsCommandList_SetPipelineState(list,p->host);
    ID3D12GraphicsCommandList_SetComputeRootSignature(list,p->root);
    ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(list,0,gpu);
    ID3D12GraphicsCommandList_SetComputeRoot32BitConstants(list,1,8,init.words,0);
    ID3D12GraphicsCommandList_Dispatch(list,1,1,1);
    *out=h;
    return S_OK;
}
