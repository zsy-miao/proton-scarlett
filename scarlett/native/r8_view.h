/* Included by the staged address-SRV writer, under the device's descriptor registry. */
static struct descriptor_record *address_view_slot(struct xbox_device *d,SIZE_T slot,BOOL *visible,
        D3D12_CPU_DESCRIPTOR_HANDLE *cpu)
{
    struct descriptor_record *r=NULL;
    for (struct desc_heap *h=d->heaps;h;h=h->next) {
        D3D12_CPU_DESCRIPTOR_HANDLE candidate;
        if (FAILED(descriptor_slot(&h->iface,slot,&candidate,NULL))) continue;
        if (r) return NULL;
        *cpu=candidate;
        *visible=h->visible;
        r=&h->records[(slot-h->start)/h->stride];
    }
    return r;
}
static BOOL valid_address_record(const struct descriptor_record *r,SIZE_T slot,UINT kind)
{
    UINT64 tag[2];
    return r && r->kind==kind && r->resource && copy_in(tag,(void *)slot,sizeof tag) &&
        tag[0]==r->serial && tag[1]==(0x5853444553430000ull|kind);
}

/* A copy takes a new serial and owns its own reference to the original import. */
static HRESULT store_address_view(struct xbox_device *d,UINT kind,SIZE_T dest,ID3D12Resource *buffer,UINT offset,
        BOOL require_visible,struct descriptor_record *old)
{
    BOOL visible=FALSE;
    D3D12_CPU_DESCRIPTOR_HANDLE cpu={0};
    struct descriptor_record *r=address_view_slot(d,dest,&visible,&cpu);
    if (!r || (require_visible && !visible)) return E_INVALIDARG;
    UINT64 tag[]={++d->descriptor_serial,0x5853444553430000ull|kind};
    if (!store_slot_tag(dest,tag)) return E_POINTER;
    /* Only R8 needs refresh; its bank slot must never expose a stale texture. */
    D3D12_SHADER_RESOURCE_VIEW_DESC null_view={.Format=DXGI_FORMAT_R8_UNORM,.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D,
        .Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,.Texture2D.MipLevels=1};
    if (kind==RAW38_VIEW) {
        D3D12_SHADER_RESOURCE_VIEW_DESC raw={.Format=DXGI_FORMAT_R32_TYPELESS,.ViewDimension=D3D12_SRV_DIMENSION_BUFFER,
            .Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,.Buffer={offset/4,38,0,D3D12_BUFFER_SRV_FLAG_RAW}};
        ID3D12Device_CreateShaderResourceView(d->host,buffer,&raw,cpu);
    } else ID3D12Device_CreateShaderResourceView(d->host,NULL,&null_view,cpu);
    ID3D12Resource_AddRef(buffer);
    *old=*r;
    *r=(struct descriptor_record){.serial=tag[0],.kind=kind,.offset=offset,.resource=buffer};
    return S_OK;
}
static HRESULT write_wc_address_view(IUnknown *device,UINT kind,const DWORD *w,SIZE_T dest)
{
    static const DWORD texture[]={61,4,0x1688,0,0,1,0,0,0,0},raw[]={39,1,0x1688,0,0,0,38,0,1,0};
    BOOL r8=kind==R8_VIEW;
    if (memcmp(w,r8?texture:raw,sizeof raw) || w[12] || w[13] || w[14] || w[15]!=(r8?3u:0u)) return E_NOTIMPL;
    SIZE_T va; memcpy(&va,w+10,sizeof va);
    UINT alignment=r8 ? R8_ALIGNMENT : D3D12_RAW_UAV_SRV_BYTE_ALIGNMENT;
    if (!va || va%alignment) return E_INVALIDARG;
    ID3D12Resource *buffer;
    UINT offset;
    HRESULT hr=retain_wc_buffer(device,(void *)va,r8?R8_BYTES:160,&buffer,&offset);
    if (FAILED(hr)) return hr;
    struct xbox_device *d=CONTAINING_RECORD(device,struct xbox_device,iface);
    struct descriptor_record old={0};
    AcquireSRWLockExclusive(&d->descriptor_lock);
    hr=offset%alignment ? E_INVALIDARG : store_address_view(d,kind,dest,buffer,offset,FALSE,&old);
    ReleaseSRWLockExclusive(&d->descriptor_lock);
    drop_record(old);
    ID3D12Resource_Release(buffer);
    return hr;
}
HRESULT xs_copy_wc_view(IUnknown *device,SIZE_T dest,SIZE_T src)
{
    struct xbox_device *d=CONTAINING_RECORD(device,struct xbox_device,iface);
    struct descriptor_record old={0};
    BOOL visible=TRUE;
    D3D12_CPU_DESCRIPTOR_HANDLE cpu;
    AcquireSRWLockExclusive(&d->descriptor_lock);
    struct descriptor_record *r=address_view_slot(d,src,&visible,&cpu);
    HRESULT hr=!visible && r && (r->kind==R8_VIEW || r->kind==RAW38_VIEW) && valid_address_record(r,src,r->kind) ?
        store_address_view(d,r->kind,dest,r->resource,r->offset,TRUE,&old) : E_INVALIDARG;
    ReleaseSRWLockExclusive(&d->descriptor_lock);
    drop_record(old);
    return hr;
}

struct xs_r8_use {
    ID3D12Resource *source,*rows,*texture;
    ID3D12DescriptorHeap *view;
};
void xs_drop_r8_use(struct xs_r8_use *u)
{
    if (!u) return;
    if (u->view) ID3D12DescriptorHeap_Release(u->view);
    if (u->texture) ID3D12Resource_Release(u->texture);
    if (u->rows) ID3D12Resource_Release(u->rows);
    if (u->source) ID3D12Resource_Release(u->source);
    HeapFree(GetProcessHeap(),0,u);
}
static void r8_transition(ID3D12GraphicsCommandList *l,ID3D12Resource *r,D3D12_RESOURCE_STATES before,
        D3D12_RESOURCE_STATES after)
{
    D3D12_RESOURCE_BARRIER b={.Transition={r,0,before,after}};
    ID3D12GraphicsCommandList_ResourceBarrier(l,1,&b);
}
/* Call immediately before use on an open direct/compute list, source in COMMON.
 * Return a private one-slot heap for this recording only. Drop hold after fence. */
HRESULT xs_refresh_r8(IUnknown *heap,SIZE_T slot,ID3D12GraphicsCommandList *l,
        ID3D12DescriptorHeap **view,struct xs_r8_use **hold)
{
    if (!view || !hold) return E_POINTER;
    *view=NULL; *hold=NULL;
    if (!l || !heap || heap->lpVtbl!=&desc_vtbl.unknown) return E_INVALIDARG;
    struct desc_heap *h=CONTAINING_RECORD(heap,struct desc_heap,iface);
    D3D12_COMMAND_LIST_TYPE type=ID3D12GraphicsCommandList_GetType(l);
    if (!h->visible || !h->records || (type!=D3D12_COMMAND_LIST_TYPE_DIRECT && type!=D3D12_COMMAND_LIST_TYPE_COMPUTE))
        return E_INVALIDARG;
    struct xbox_device *d=h->owner;
    ID3D12Device *ld=NULL;
    HRESULT hr=ID3D12GraphicsCommandList_GetDevice(l,&IID_ID3D12Device,(void **)&ld);
    if (FAILED(hr)) return hr;
    BOOL same=ld==d->host;
    ID3D12Device_Release(ld);
    if (!same) return E_INVALIDARG;
    struct xs_r8_use *u=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof *u);
    if (!u) return E_OUTOFMEMORY;
    UINT offset=0;
    D3D12_CPU_DESCRIPTOR_HANDLE cpu;
    AcquireSRWLockShared(&d->descriptor_lock);
    if (SUCCEEDED(descriptor_slot(heap,slot,&cpu,NULL))) {
        struct descriptor_record *r=&h->records[(slot-h->start)/h->stride];
        if (valid_address_record(r,slot,R8_VIEW)) { u->source=r->resource; ID3D12Resource_AddRef(u->source); offset=r->offset; }
    }
    ReleaseSRWLockShared(&d->descriptor_lock);
    if (!u->source) { hr=E_INVALIDARG; goto fail; }
    D3D12_HEAP_PROPERTIES hp={.Type=D3D12_HEAP_TYPE_DEFAULT,.CreationNodeMask=1,.VisibleNodeMask=1};
    D3D12_RESOURCE_DESC rd={.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER,.Width=R8_BYTES,.Height=1,.MipLevels=1,
        .DepthOrArraySize=1,.SampleDesc={1,0},.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR};
    hr=ID3D12Device_CreateCommittedResource(d->host,&hp,0,&rd,D3D12_RESOURCE_STATE_COPY_DEST,NULL,&IID_ID3D12Resource,(void **)&u->rows);
    if (FAILED(hr)) goto fail;
    rd.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D; rd.Width=256; rd.Height=128;
    rd.Format=DXGI_FORMAT_R8_UNORM; rd.Layout=D3D12_TEXTURE_LAYOUT_UNKNOWN;
    hr=ID3D12Device_CreateCommittedResource(d->host,&hp,0,&rd,D3D12_RESOURCE_STATE_COPY_DEST,NULL,&IID_ID3D12Resource,(void **)&u->texture);
    if (FAILED(hr)) goto fail;
    D3D12_DESCRIPTOR_HEAP_DESC hd={D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,1,D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,1};
    hr=ID3D12Device_CreateDescriptorHeap(d->host,&hd,&IID_ID3D12DescriptorHeap,(void **)&u->view);
    if (FAILED(hr)) goto fail;
    u->view->lpVtbl->GetCPUDescriptorHandleForHeapStart(u->view,&cpu);
    ID3D12Device_CreateShaderResourceView(d->host,u->texture,NULL,cpu);

    /* Each 16-byte row segment is contiguous in both layouts. All reads execute
     * on the consuming queue, including writes ordered after list submission. */
    r8_transition(l,u->source,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_SOURCE);
    for (UINT y=0;y<128;y++) for (UINT x=0;x<256;x+=16) {
        UINT tiled=((y/64)*4+x/64)*4096+(y%32)*16+(x&16)*32+(y&32)*32+(x&32)*64;
        ID3D12GraphicsCommandList_CopyBufferRegion(l,u->rows,y*256+x,u->source,(UINT64)offset+tiled,16);
    }
    r8_transition(l,u->source,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_COMMON);
    r8_transition(l,u->rows,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_TEXTURE_COPY_LOCATION from={.pResource=u->rows,.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT,
        .PlacedFootprint.Footprint={DXGI_FORMAT_R8_UNORM,256,128,1,256}};
    D3D12_TEXTURE_COPY_LOCATION to={.pResource=u->texture,.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
    ID3D12GraphicsCommandList_CopyTextureRegion(l,&to,0,0,0,&from,NULL);
    r8_transition(l,u->texture,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE |
            (type==D3D12_COMMAND_LIST_TYPE_DIRECT ? D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE : 0));
    *view=u->view; *hold=u;
    return S_OK;
fail:
    xs_drop_r8_use(u);
    return hr;
}
