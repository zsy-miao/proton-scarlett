/* Title +2481bce creates a COPY_DEST; +ba37b1 reads the entire buffer after completion. */
struct query_readback {
    IUnknown iface;
    LONG refs;
    IUnknown *owner;
    ID3D12Resource *host;
    UINT width;
};
static ULONG WINAPI readback_addref(IUnknown *iface)
{
    return InterlockedIncrement(&CONTAINING_RECORD(iface,struct query_readback,iface)->refs);
}
static ULONG WINAPI readback_release(IUnknown *iface)
{
    struct query_readback *r=CONTAINING_RECORD(iface,struct query_readback,iface);
    ULONG refs=InterlockedDecrement(&r->refs);
    if (!refs) {
        ID3D12Resource_Release(r->host);
        IUnknown_Release(r->owner);
        HeapFree(GetProcessHeap(),0,r);
    }
    return refs;
}
static HRESULT WINAPI readback_query(IUnknown *iface,REFIID iid,void **out)
{
    IID id;
    if (!out) return E_POINTER;
    *out=NULL;
    if (!iid) return E_INVALIDARG;
    if (!copy_in(&id,iid,sizeof id)) return E_POINTER;
    if (!IsEqualIID(&id,&IID_IUnknown) && !IsEqualIID(&id,&IID_ID3D12Resource)) return E_NOINTERFACE;
    readback_addref(iface); *out=iface;
    return S_OK;
}
static HRESULT WINAPI readback_map(IUnknown *iface,UINT sub,const D3D12_RANGE *range,void **out)
{
    struct query_readback *r=CONTAINING_RECORD(iface,struct query_readback,iface);
    D3D12_RANGE read;
    if (!out) return E_POINTER;
    *out=NULL;
    if (sub || !range) return E_INVALIDARG;
    if (!copy_in(&read,range,sizeof read)) return E_POINTER;
    if (read.Begin || read.End!=r->width) return E_INVALIDARG;
    return ID3D12Resource_Map(r->host,0,&read,out);
}
static void WINAPI readback_unmap(IUnknown *iface,UINT sub,const D3D12_RANGE *written)
{
    struct query_readback *r=CONTAINING_RECORD(iface,struct query_readback,iface);
    if (sub || written) xs_stop();
    ID3D12Resource_Unmap(r->host,0,NULL);
}
static const struct query_readback_table {
    IUnknownVtbl unknown;
    xs_stop_fn inherited[5];
    __typeof__(&readback_map) map;
    __typeof__(&readback_unmap) unmap;
    xs_stop_fn unsupported[64];
} readback_vtbl={{readback_query,readback_addref,readback_release},{[0 ... 4]=xs_stop},
    readback_map,readback_unmap,{[0 ... 63]=xs_stop}};
_Static_assert(offsetof(struct query_readback_table,map)==0x40 &&
        offsetof(struct query_readback_table,unmap)==0x48,"query readback ABI");

static HRESULT WINAPI bootstrap_readback(IUnknown *iface,const void *props,UINT flags,const void *desc,
        UINT state,const void *clear,REFIID iid,void **out)
{
    IID id;
    D3D12_HEAP_PROPERTIES hp;
    D3D12_RESOURCE_DESC rd={0};
    if (!out) return E_POINTER;
    *out=NULL;
    if (!iid || !props || !desc) return E_INVALIDARG;
    if (!copy_in(&id,iid,sizeof id)) return E_POINTER;
    if (!IsEqualIID(&id,&IID_ID3D12Resource)) return E_NOINTERFACE;
    if (!copy_in(&hp,props,sizeof hp) || !copy_in(&rd,desc,offsetof(D3D12_RESOURCE_DESC,Flags)+sizeof rd.Flags))
        return E_POINTER;
    if (flags || state!=D3D12_RESOURCE_STATE_COPY_DEST || clear || hp.Type!=D3D12_HEAP_TYPE_READBACK ||
            hp.CPUPageProperty || hp.MemoryPoolPreference || hp.CreationNodeMask!=1 || hp.VisibleNodeMask!=1 ||
            rd.Dimension!=D3D12_RESOURCE_DIMENSION_BUFFER || rd.Alignment || rd.Height!=1 || rd.DepthOrArraySize!=1 ||
            rd.MipLevels!=1 || rd.Format || rd.SampleDesc.Count!=1 || rd.SampleDesc.Quality ||
            rd.Layout!=D3D12_TEXTURE_LAYOUT_ROW_MAJOR || rd.Flags) return E_NOTIMPL;
    if (!rd.Width || rd.Width>UINT_MAX || rd.Width%sizeof(UINT64)) return E_INVALIDARG;
    struct query_readback *r=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof *r);
    if (!r) return E_OUTOFMEMORY;
    struct xbox_device *owner=CONTAINING_RECORD(iface,struct xbox_device,iface);
    HRESULT hr=ID3D12Device_CreateCommittedResource(owner->host,&hp,0,&rd,state,NULL,&id,(void **)&r->host);
    if (FAILED(hr)) { HeapFree(GetProcessHeap(),0,r); return hr; }
    r->iface.lpVtbl=&readback_vtbl.unknown;
    r->refs=1; r->width=(UINT)rd.Width; r->owner=iface;
    IUnknown_AddRef(iface);
    *out=&r->iface;
    return S_OK;
}
