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

static UINT view_layers(UINT kind)
{
    UINT layers=array_view_layers(kind);
    return layers?layers:kind==CUBE_PAIR_VIEW?CUBE_PAIR_FACES:kind==CUBE1_VIEW?CUBE1_FACES:1;
}
static D3D12_SHADER_RESOURCE_VIEW_DESC address_srv_desc(UINT kind,UINT elements,UINT stride)
{
    D3D12_SHADER_RESOURCE_VIEW_DESC v={.Format=refresh_format(kind),.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING};
    if (kind==STRUCT_VIEW) { v.ViewDimension=D3D12_SRV_DIMENSION_BUFFER; v.Buffer.NumElements=elements; v.Buffer.StructureByteStride=stride; }
    else if (kind==CUBE1_VIEW) { v.ViewDimension=D3D12_SRV_DIMENSION_TEXTURECUBE; v.TextureCube.MipLevels=1; }
    else if (kind==CUBE_PAIR_VIEW) { v.ViewDimension=D3D12_SRV_DIMENSION_TEXTURECUBEARRAY; v.TextureCubeArray.MipLevels=1; v.TextureCubeArray.NumCubes=2; }
    else if (array_view_layers(kind)) { v.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2DARRAY; v.Texture2DArray.MipLevels=1; v.Texture2DArray.ArraySize=array_view_layers(kind); }
    else if (kind==SRGB_ROW_VIEW || kind==A8_LINE_VIEW) { v.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE1D; v.Texture1D.MipLevels=1; }
    else if (kind==VOLUME1_VIEW) { v.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE3D; v.Texture3D.MipLevels=1; }
    else { v.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D; v.Texture2D.MipLevels=1; }
    return v;
}
/* A copy takes a new serial and owns its own reference to the original import. */
static HRESULT store_address_view(struct xbox_device *d,UINT kind,SIZE_T dest,ID3D12Resource *buffer,UINT offset,
        UINT elements,UINT stride,BOOL require_visible,struct descriptor_record *old)
{
    BOOL visible=FALSE;
    D3D12_CPU_DESCRIPTOR_HANDLE cpu={0};
    struct descriptor_record *r=address_view_slot(d,dest,&visible,&cpu);
    if (!r || (require_visible && !visible)) return E_INVALIDARG;
    UINT64 tag[]={++d->descriptor_serial,0x5853444553430000ull|kind};
    if (!store_slot_tag(dest,tag)) return E_POINTER;
    /* A texture requires refresh; its bank slot must never expose a stale texture. */
    D3D12_SHADER_RESOURCE_VIEW_DESC null_view=address_srv_desc(kind,elements,stride);
    if (tile_view(kind)) {
        /* Inert host slot even on reuse; only a separately lowered table may expose the buffer. */
        if (kind==TILE_UINT_UAV || kind==TILE_UINT160_UAV || kind==TILE_R11_UAV || kind==TILE_RG_UAV || kind==TILE_F_UAV) {
            D3D12_UNORDERED_ACCESS_VIEW_DESC u={.Format=kind==TILE_F_UAV?DXGI_FORMAT_R32_FLOAT:kind==TILE_RG_UAV?DXGI_FORMAT_R32G32_FLOAT:kind==TILE_R11_UAV?DXGI_FORMAT_R11G11B10_FLOAT:DXGI_FORMAT_R32_UINT,.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D};
            ID3D12Device_CreateUnorderedAccessView(d->host,NULL,NULL,&u,cpu);
        } else {
            null_view.Format=kind==TILE_F_SRV?DXGI_FORMAT_R32_FLOAT:kind==TILE_RG_SRV?DXGI_FORMAT_R32G32_FLOAT:kind==TILE_R11_SRV?DXGI_FORMAT_R11G11B10_FLOAT:kind==TILE_RGB_SRV?DXGI_FORMAT_R9G9B9E5_SHAREDEXP:DXGI_FORMAT_R32_UINT;
            ID3D12Device_CreateShaderResourceView(d->host,NULL,&null_view,cpu);
        }
    } else if (kind==RAW38_VIEW) {
        D3D12_SHADER_RESOURCE_VIEW_DESC raw={.Format=DXGI_FORMAT_R32_TYPELESS,.ViewDimension=D3D12_SRV_DIMENSION_BUFFER,
            .Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,.Buffer={offset/4,38,0,D3D12_BUFFER_SRV_FLAG_RAW}};
        ID3D12Device_CreateShaderResourceView(d->host,buffer,&raw,cpu);
    } else ID3D12Device_CreateShaderResourceView(d->host,NULL,&null_view,cpu);
    ID3D12Resource_AddRef(buffer);
    *old=*r;
    *r=(struct descriptor_record){.serial=tag[0],.kind=kind,.offset=offset,.resource=buffer,.elements=elements,.stride=stride};
    return S_OK;
}
static HRESULT write_tile_record(struct xbox_device *d,IUnknown *id,UINT kind,UINT_PTR va,SIZE_T dest,UINT mip,UINT levels)
{
    struct descriptor_record old={0};
    ID3D12Resource *buffer=NULL;
    HRESULT hr=E_INVALIDARG;
    AcquireSRWLockExclusive(&d->descriptor_lock);
    for (struct color_resource *c=d->colors;c;c=c->next) {
        if (id==&c->iface && c->single_format==DXGI_FORMAT_R11G11B10_FLOAT && kind==TILE_R11_SRV) {hr=S_FALSE;goto done;}
        if (id==&c->iface && c->small_tile_format==DXGI_FORMAT_R32_UINT && (kind==TILE_UINT_SRV || kind==TILE_UINT_UAV)) {
            if (tile_mapping(c)) hr=store_address_view(d,kind==TILE_UINT_SRV?TILE_UINT160_SRV:TILE_UINT160_UAV,dest,c->host,mip,levels,0,FALSE,&old);
            goto done;
        }
        if (kind==TILE_F_SRV || kind==TILE_F_UAV ? c->small_tile_format!=DXGI_FORMAT_R32_FLOAT :
            kind==TILE_RG_SRV || kind==TILE_RG_UAV ? c->small_tile_format!=DXGI_FORMAT_R32G32_FLOAT : (kind==TILE_R11_SRV || kind==TILE_R11_UAV) ? !c->tiled_r11 : !c->tiled_r32) continue;
        if ((id ? &c->iface!=id : c->tile_va!=va) || !tile_mapping(c)) continue;
        if (buffer) goto done;
        buffer=c->host;
    }
    if (buffer) hr=store_address_view(d,kind,dest,buffer,mip,levels,0,FALSE,&old);
done:
    ReleaseSRWLockExclusive(&d->descriptor_lock);
    drop_record(old);
    return hr;
}
static HRESULT write_wc_address_view(IUnknown *device,UINT kind,const DWORD *w,SIZE_T dest)
{
    static const DWORD raw[]={39,1,0x1688,0,0,0,38,0,1,0};
    UINT elements=kind==STRUCT_VIEW?w[6]:0,stride=kind==STRUCT_VIEW?w[7]:0;
    BOOL six_records=elements==6 && stride==32 && w[9]==0x800100;
    const DWORD structured[]={0,1,0x1688,0,0,0,elements,stride,0,six_records?0x800100u:0u};
    BOOL tex=refresh_format(kind)!=DXGI_FORMAT_UNKNOWN;
    D3D12_SHADER_RESOURCE_VIEW_DESC srv=address_srv_desc(kind,elements,stride);
    DWORD texture[]={srv.Format,srv.ViewDimension,0x1688,0,0,1,0,kind==CUBE_PAIR_VIEW?2:array_view_layers(kind),0,0};
    if (memcmp(w,tex?texture:stride?structured:raw,sizeof texture) || w[12] || w[13] || w[14] || w[15]!=(tex?3u:0u)) return E_NOTIMPL;
    SIZE_T va; memcpy(&va,w+10,sizeof va);
    UINT alignment=kind==A8_LINE_VIEW?D3D12_TEXTURE_DATA_PITCH_ALIGNMENT:tex ? R8_ALIGNMENT : six_records ? 256 : D3D12_RAW_UAV_SRV_BYTE_ALIGNMENT;
    if (!va || va%alignment) return E_INVALIDARG;
    ID3D12Resource *buffer;
    UINT offset;
    HRESULT hr=retain_wc_buffer(device,(void *)va,stride?(UINT64)elements*stride:kind==A8_LINE_VIEW?256:kind==R8_VIEW?R8_BYTES:tex?view_layers(kind)*R8_ALIGNMENT:160,&buffer,&offset);
    if (FAILED(hr)) return hr;
    struct xbox_device *d=CONTAINING_RECORD(device,struct xbox_device,iface);
    struct descriptor_record old={0};
    AcquireSRWLockExclusive(&d->descriptor_lock);
    hr=offset%alignment ? E_INVALIDARG : store_address_view(d,kind,dest,buffer,offset,elements,stride,FALSE,&old);
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
    HRESULT hr=!visible && r && (tile_view(r->kind) || refresh_required(r->kind) || r->kind==RAW38_VIEW) && valid_address_record(r,src,r->kind) ?
        store_address_view(d,r->kind,dest,r->resource,r->offset,r->elements,r->stride,TRUE,&old) : E_INVALIDARG;
    ReleaseSRWLockExclusive(&d->descriptor_lock);
    drop_record(old);
    return hr;
}

struct xs_texture_use {
    ID3D12Resource *source,*rows,*texture;
    ID3D12DescriptorHeap *view;
};
void xs_drop_texture_use(struct xs_texture_use *u)
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
    D3D12_RESOURCE_BARRIER b={.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,before,after}};
    ID3D12GraphicsCommandList_ResourceBarrier(l,1,&b);
}
/* Call immediately before use on an open direct/compute list, source in COMMON.
 * Return a private one-slot heap for this recording only. Drop hold after fence. */
HRESULT xs_refresh_wc_texture(IUnknown *heap,SIZE_T slot,ID3D12GraphicsCommandList *l,
        ID3D12DescriptorHeap **view,struct xs_texture_use **hold)
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
    struct xs_texture_use *u=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof *u);
    if (!u) return E_OUTOFMEMORY;
    UINT offset=0,kind=0,elements=0,stride=0; DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;
    D3D12_CPU_DESCRIPTOR_HANDLE cpu;
    AcquireSRWLockShared(&d->descriptor_lock);
    if (SUCCEEDED(descriptor_slot(heap,slot,&cpu,NULL))) {
        struct descriptor_record *r=&h->records[(slot-h->start)/h->stride];
        if (refresh_required(r->kind) && valid_address_record(r,slot,r->kind)) {
            u->source=r->resource; ID3D12Resource_AddRef(u->source); offset=r->offset; format=refresh_format(r->kind);
            kind=r->kind; elements=r->elements; stride=r->stride;
        }
    }
    ReleaseSRWLockShared(&d->descriptor_lock);
    UINT atlas=kind==ATLAS1K_VIEW?1024:kind==ATLAS2K_VIEW?2048:0;
    BOOL texel32=texel32_format(format),row=kind==SRGB_ROW_VIEW;
    if (!u->source) { hr=E_INVALIDARG; goto fail; }
    D3D12_HEAP_PROPERTIES hp={.Type=D3D12_HEAP_TYPE_DEFAULT,.CreationNodeMask=1,.VisibleNodeMask=1};
    D3D12_RESOURCE_DESC rd={.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER,.Width=R8_BYTES,.Height=1,.MipLevels=1,
        .DepthOrArraySize=1,.SampleDesc={1,0},.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR};
    BOOL rebase=kind==A8_LINE_VIEW && offset%D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT;
    if (kind==R8_VIEW || rebase) {
        if (rebase) rd.Width=D3D12_TEXTURE_DATA_PITCH_ALIGNMENT;
        hr=ID3D12Device_CreateCommittedResource(d->host,&hp,0,&rd,D3D12_RESOURCE_STATE_COPY_DEST,NULL,&IID_ID3D12Resource,(void **)&u->rows);
        if (FAILED(hr)) goto fail;
    }
    if (kind==STRUCT_VIEW) rd.Width=(UINT64)elements*stride;
    else {
        rd.Dimension=(row || kind==A8_LINE_VIEW)?D3D12_RESOURCE_DIMENSION_TEXTURE1D:kind==VOLUME1_VIEW?D3D12_RESOURCE_DIMENSION_TEXTURE3D:D3D12_RESOURCE_DIMENSION_TEXTURE2D; rd.Width=atlas?atlas:texel32?1:kind==A8_LINE_VIEW?128:256; rd.Height=atlas?atlas:(row || texel32 || kind==A8_LINE_VIEW)?1:128;
        rd.DepthOrArraySize=view_layers(kind); rd.Format=format; rd.Layout=D3D12_TEXTURE_LAYOUT_UNKNOWN;
    }
    hr=ID3D12Device_CreateCommittedResource(d->host,&hp,0,&rd,D3D12_RESOURCE_STATE_COPY_DEST,NULL,&IID_ID3D12Resource,(void **)&u->texture);
    if (FAILED(hr)) goto fail;
    D3D12_DESCRIPTOR_HEAP_DESC hd={D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,1,D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,1};
    hr=ID3D12Device_CreateDescriptorHeap(d->host,&hd,&IID_ID3D12DescriptorHeap,(void **)&u->view);
    if (FAILED(hr)) goto fail;
    u->view->lpVtbl->GetCPUDescriptorHandleForHeapStart(u->view,&cpu);
    D3D12_SHADER_RESOURCE_VIEW_DESC srv=address_srv_desc(kind,elements,stride);
    ID3D12Device_CreateShaderResourceView(d->host,u->texture,&srv,cpu);

    /* Each 16-byte row segment is contiguous in both layouts. All reads execute
     * on the consuming queue, including writes ordered after list submission. */
    r8_transition(l,u->source,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_SOURCE);
    if (u->rows) {
        if (rebase) ID3D12GraphicsCommandList_CopyBufferRegion(l,u->rows,0,u->source,offset,rd.Width);
        else for (UINT y=0;y<128;y++) for (UINT x=0;x<256;x+=16) {
            UINT tiled=((y/64)*4+x/64)*4096+(y%32)*16+(x&16)*32+(y&32)*32+(x&32)*64;
            ID3D12GraphicsCommandList_CopyBufferRegion(l,u->rows,y*256+x,u->source,(UINT64)offset+tiled,16);
        }
        r8_transition(l,u->rows,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COPY_SOURCE);
    }
    if (kind==STRUCT_VIEW) {
        /* The original offset is not a multiple of StructureByteStride. Rebase at use, on the queue. */
        ID3D12GraphicsCommandList_CopyBufferRegion(l,u->texture,0,u->source,offset,rd.Width);
    } else {
        D3D12_TEXTURE_COPY_LOCATION from={.pResource=u->rows?u->rows:u->source,.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT,
            .PlacedFootprint={u->rows?0:offset,{rd.Format,(UINT)rd.Width,rd.Height,1,atlas?atlas:row?SRGB_ROW_BYTES:256}}};
        D3D12_TEXTURE_COPY_LOCATION to={.pResource=u->texture,.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
        for (UINT layer=0;layer<rd.DepthOrArraySize;layer++) {
            to.SubresourceIndex=layer;
            ID3D12GraphicsCommandList_CopyTextureRegion(l,&to,0,0,0,&from,NULL);
            from.PlacedFootprint.Offset+=R8_ALIGNMENT;
        }
    }
    r8_transition(l,u->source,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_COMMON);
    r8_transition(l,u->texture,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE |
            (type==D3D12_COMMAND_LIST_TYPE_DIRECT ? D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE : 0));
    *view=u->view; *hold=u;
    return S_OK;
fail:
    xs_drop_texture_use(u);
    return hr;
}
