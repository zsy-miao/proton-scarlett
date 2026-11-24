/* Device-local, identity-qualified resource bridge for a future owning resource factory.
 * A binding is removed before its identity is destroyed; title objects are never cast to host COM. */
struct xs_buffer_binding {
    struct xbox_device *owner;
    IUnknown *identity;
    ID3D12Resource *host;
    UINT64 bytes;
    struct xs_buffer_binding *next;
};
HRESULT xs_bind_view_buffer(IUnknown *device,IUnknown *identity,ID3D12Resource *host,struct xs_buffer_binding **out)
{
    if (!out) return E_POINTER;
    *out = NULL;
    if (!device || !identity || !host) return E_INVALIDARG;
    struct xbox_device *d = CONTAINING_RECORD(device,struct xbox_device,iface);
    D3D12_RESOURCE_DESC rd;
    ID3D12Device *hd = NULL;
    host->lpVtbl->GetDesc(host,&rd);
    if (rd.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER ||
        (rd.Flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE) ||
        FAILED(ID3D12Resource_GetDevice(host,&IID_ID3D12Device,(void **)&hd))) return E_INVALIDARG;
    BOOL same = hd == d->host;
    ID3D12Device_Release(hd);
    if (!same) return E_INVALIDARG;
    struct xs_buffer_binding *b = HeapAlloc(GetProcessHeap(),0,sizeof *b);
    if (!b) return E_OUTOFMEMORY;
    AcquireSRWLockExclusive(&d->descriptor_lock);
    for (struct xs_buffer_binding *p=d->buffers;p;p=p->next) if (p->identity == identity) {
        ReleaseSRWLockExclusive(&d->descriptor_lock);
        HeapFree(GetProcessHeap(),0,b);
        return E_INVALIDARG;
    }
    *b=(struct xs_buffer_binding){d,identity,host,rd.Width,d->buffers};
    IUnknown_AddRef(device);
    IUnknown_AddRef(identity);
    ID3D12Resource_AddRef(host);
    d->buffers=b;
    ReleaseSRWLockExclusive(&d->descriptor_lock);
    *out=b;
    return S_OK;
}
void xs_unbind_view_buffer(struct xs_buffer_binding *b)
{
    struct xbox_device *d=b->owner;
    AcquireSRWLockExclusive(&d->descriptor_lock);
    struct xs_buffer_binding **p=&d->buffers;
    while (*p!=b) p=&(*p)->next;
    *p=b->next;
    ReleaseSRWLockExclusive(&d->descriptor_lock);
    ID3D12Resource_Release(b->host);
    IUnknown_Release(b->identity);
    IUnknown_Release(&d->iface);
    HeapFree(GetProcessHeap(),0,b);
}

enum { CBV=0x88, SRV=0x90, UAV=0x98 };
static const UINT64 inline_null_cbv[2]={0x10000000000000ull,0x2403c00000000ull};
static BOOL copy_in(void *out,const void *in,SIZE_T size)
{
    SIZE_T n;
    return in && ReadProcessMemory(GetCurrentProcess(),in,out,size,&n) && size==n;
}
/* Hold the owner lock until the host write finishes. Ambiguous overlapping title ranges fail. */
HRESULT xs_write_view(IUnknown *device,UINT kind,IUnknown *object,const void *input,SIZE_T dest)
{
    struct xbox_device *d=CONTAINING_RECORD(device,struct xbox_device,iface);
    D3D12_SHADER_RESOURCE_VIEW_DESC srv={0};
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav={0};
    D3D12_CONSTANT_BUFFER_VIEW_DESC cbv={0};
    DWORD words[10]={0};
    SIZE_T size=kind==CBV?16:40;
    if (kind!=CBV && kind!=SRV && kind!=UAV) return E_INVALIDARG;
    if (!copy_in(words,input,size)) return E_POINTER;
    if (!object && kind!=CBV) {
        if (words[0]!=42 || words[2]!=(kind==SRV?0x1688u:0u) ||
            memcmp(words+3,(DWORD[7]){0},28)) return E_NOTIMPL;
        UINT dim=words[1];
        if (kind==SRV) {
            if (dim!=1 && dim!=2 && dim!=4 && dim!=8 && dim!=9) return E_NOTIMPL;
            srv.Format=DXGI_FORMAT_R32_UINT; srv.ViewDimension=dim; srv.Shader4ComponentMapping=words[2];
            if (dim==1) srv.Buffer.NumElements=1;
            else srv.Texture1D.MipLevels=1;
        } else {
            if (dim!=1 && dim!=2 && dim!=4 && dim!=5 && dim!=8) return E_NOTIMPL;
            uav.Format=DXGI_FORMAT_R32_UINT; uav.ViewDimension=dim;
            if (dim==1) uav.Buffer.NumElements=1;
            else if (dim==5) uav.Texture2DArray.ArraySize=1;
            else if (dim==8) uav.Texture3D.WSize=1;
        }
    } else if (!object) {
        if (memcmp(words,(DWORD[4]){0},16)) return E_NOTIMPL;
    } else {
        if (kind!=SRV || words[0]!=DXGI_FORMAT_R32_TYPELESS || words[1]!=1 || words[2]!=0x1688 ||
            !words[6] || words[7] || words[8]!=D3D12_BUFFER_SRV_FLAG_RAW) return E_NOTIMPL;
        srv.Format=DXGI_FORMAT_R32_TYPELESS; srv.ViewDimension=D3D12_SRV_DIMENSION_BUFFER;
        srv.Shader4ComponentMapping=words[2]; memcpy(&srv.Buffer.FirstElement,words+4,8);
        srv.Buffer.NumElements=words[6]; srv.Buffer.Flags=D3D12_BUFFER_SRV_FLAG_RAW;
    }
    HRESULT hr=E_INVALIDARG;
    ID3D12Resource *resource=NULL,*old=NULL;
    AcquireSRWLockExclusive(&d->descriptor_lock);
    if (object) {
        for (struct xs_buffer_binding *b=d->buffers;b;b=b->next) if (b->identity==object) {
            if (srv.Buffer.FirstElement>b->bytes/4 ||
                srv.Buffer.NumElements>b->bytes/4-srv.Buffer.FirstElement) goto done;
            resource=b->host;
            break;
        }
        if (!resource) goto done;
    }
    struct desc_heap *h=NULL;
    D3D12_CPU_DESCRIPTOR_HANDLE cpu;
    for (struct desc_heap *p=d->heaps;p;p=p->next) {
        if (FAILED(xs_descriptor_slot(&p->iface,dest,&cpu,NULL))) continue;
        if (h) goto done;
        h=p;
    }
    if (!h || FAILED(xs_descriptor_slot(&h->iface,dest,&cpu,NULL))) goto done;
    struct descriptor_record *r=h->records+(dest-h->start)/h->stride;
    UINT64 tag[2]={++d->descriptor_serial,0x5853444553430000ull|kind};
    SIZE_T n;
    if (!WriteProcessMemory(GetCurrentProcess(),(void *)dest,kind==CBV?inline_null_cbv:tag,16,&n) || n!=16) {
        hr=E_POINTER; goto done;
    }
    if (kind==SRV) ID3D12Device_CreateShaderResourceView(d->host,resource,&srv,cpu);
    else if (kind==UAV) ID3D12Device_CreateUnorderedAccessView(d->host,NULL,NULL,&uav,cpu);
    else ID3D12Device_CreateConstantBufferView(d->host,&cbv,cpu);
    old=r->resource;
    if (resource) ID3D12Resource_AddRef(resource);
    *r=(struct descriptor_record){tag[0],kind,resource};
    hr=S_OK;
done:
    ReleaseSRWLockExclusive(&d->descriptor_lock);
    if (old) ID3D12Resource_Release(old);
    return hr;
}

/* Consuming adapter holds the heap through retirement; revalidate title writes at use. */
HRESULT xs_resolve_view(IUnknown *heap,SIZE_T slot,UINT kind,ID3D12DescriptorHeap **host,D3D12_GPU_DESCRIPTOR_HANDLE *gpu)
{
    if (!host || !gpu) return E_POINTER;
    *host=NULL; gpu->ptr=0;
    if (!heap || heap->lpVtbl!=&desc_vtbl.unknown) return E_INVALIDARG;
    struct desc_heap *h=CONTAINING_RECORD(heap,struct desc_heap,iface);
    if (!h->records || (kind!=CBV && kind!=SRV && kind!=UAV)) return E_INVALIDARG;
    struct xbox_device *d=h->owner;
    AcquireSRWLockExclusive(&d->descriptor_lock);
    D3D12_CPU_DESCRIPTOR_HANDLE cpu;
    HRESULT hr=xs_descriptor_slot(heap,slot,&cpu,gpu);
    if (SUCCEEDED(hr)) {
        UINT64 bits[2];
        struct descriptor_record *r=h->records+(slot-h->start)/h->stride;
        if (!copy_in(bits,(void *)slot,sizeof bits)) hr=E_POINTER;
        else if (kind==CBV && !memcmp(bits,inline_null_cbv,16)) {
            if (r->kind!=CBV) {
                D3D12_CONSTANT_BUFFER_VIEW_DESC zero={0};
                ID3D12Device_CreateConstantBufferView(d->host,&zero,cpu);
                if (r->resource) ID3D12Resource_Release(r->resource);
                *r=(struct descriptor_record){0,CBV,NULL};
            }
        } else if (!r->serial || r->kind!=kind || bits[0]!=r->serial || bits[1]!=(0x5853444553430000ull|kind))
            hr=E_INVALIDARG;
        if (SUCCEEDED(hr)) *host=h->host; /* borrowed */
    }
    ReleaseSRWLockExclusive(&d->descriptor_lock);
    if (FAILED(hr)) gpu->ptr=0;
    return hr;
}
static void WINAPI write_cbv(IUnknown *d,const void *desc,SIZE_T dest)
{
    if (FAILED(xs_write_view(d,CBV,NULL,desc,dest))) xs_stop();
}
static void WINAPI write_srv(IUnknown *d,IUnknown *r,const void *desc,SIZE_T dest)
{
    if (FAILED(xs_write_view(d,SRV,r,desc,dest))) xs_stop();
}
static void WINAPI write_uav(IUnknown *d,IUnknown *r,IUnknown *counter,const void *desc,SIZE_T dest)
{
    if (counter || FAILED(xs_write_view(d,UAV,r,desc,dest))) xs_stop();
}
