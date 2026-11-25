/* Private 0032 ABI. Resolve the allocation by guest VA each time; callers own the returned buffer. */
static HRESULT open_wc_buffer(struct xbox_device *d,UINT64 va,UINT span,const char *name,ID3D12Resource **out,
        ID3D12Heap **ret_heap,HANDLE *ret_section,UINT_PTR *ret_off,UINT64 *ret_generation)
{
    ID3D12Heap *heap=NULL;UINT off=0;HANDLE section=NULL;
    HRESULT (WINAPI *place)(ID3D12Device*,ID3D12Heap*,HANDLE,SIZE_T,SIZE_T,ID3D12Resource**)=(void*)GetProcAddress(GetModuleHandleW(L"d3d12core.dll"),name);
    if(!place)return DXGI_ERROR_UNSUPPORTED;
    HRESULT hr=native_pool_ref(d->host,(void*)va,span,&heap,NULL,&off);
    if(hr==S_FALSE){
        struct scarlett_section_info i;
        if(!xs_is_rtx3070(d->host)) return E_NOTIMPL;
        if(ret_generation){
            struct scarlett_wc_view_info view;
            hr=scarlett_acquire_wc_view((void*)va,span,&view);
            if(hr==S_OK){i=view.info;*ret_generation=view.generation;}
            /* Older Wine can still place the buffer, but cannot authorize an address query. */
            else if(hr==HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED)) hr=scarlett_query_section(0x53430005,(void*)va,span,&i);
        }else hr=scarlett_query_section(0x53430005,(void*)va,span,&i);
        if(FAILED(hr))return hr;
        if(!i.section || i.view_size!=WC_POOL_BYTES || i.offset>i.view_size-span || va!=(UINT_PTR)i.base+i.offset){if(i.section)CloseHandle(i.section);return E_INVALIDARG;}
        section=i.section;off=i.offset;
    }else if(hr!=S_OK)return hr;
    hr=place(d->host,heap,section,WC_POOL_BYTES,off,out);
    if(SUCCEEDED(hr) && ret_off){*ret_heap=heap;*ret_section=section;*ret_off=off;}
    else if(heap) ID3D12Heap_Release(heap);
    else CloseHandle(section);
    return hr;
}
/* Included by d3d12_device.c, only for the observed WC swizzle 0x102 resource. */
static HRESULT create_tiled_buffer(struct xbox_device *d,DXGI_FORMAT fmt,BOOL small,UINT64 va,UINT state,const void *clear,REFIID iid,void **out)
{
    if (!out) return E_POINTER;
    *out=NULL;IID id;
    if (!iid || !copy_in(&id,iid,sizeof id)) return E_POINTER;
    if (!IsEqualIID(&id,&IID_ID3D12Resource)) return E_NOINTERFACE;
    if (!va || state!=3 || clear) return E_INVALIDARG;
    struct color_resource *r=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof *r);
    if (!r)return E_OUTOFMEMORY;
    BOOL r11=fmt==DXGI_FORMAT_R11G11B10_FLOAT, rg=fmt==DXGI_FORMAT_R32G32_FLOAT;
    r->tiled_r11=r11;r->tiled_r32=!small && fmt==DXGI_FORMAT_R32_UINT;
    if(small)r->small_tile_format=fmt;
    r->tile_va=va;
    const char *name=rg?"ScarlettTiledRG160V1":fmt==DXGI_FORMAT_R32_FLOAT?"ScarlettTiledFloat160V1":
            small?"ScarlettTiledUint160V1":r11?"ScarlettTiledR11FiveV1":"ScarlettTiledR32BufferV1";
    HRESULT hr=open_wc_buffer(d,va,tile_span(r),name,
             &r->host,&r->tile_heap,&r->tile_section,&r->tile_offset,NULL);
    if (FAILED(hr)) {HeapFree(GetProcessHeap(),0,r);return hr;}
    r->iface.lpVtbl=&color_vtbl.unknown;r->refs=1;r->owner=d;IUnknown_AddRef(&d->iface);
    AcquireSRWLockExclusive(&d->descriptor_lock);r->next=d->colors;d->colors=r;ReleaseSRWLockExclusive(&d->descriptor_lock);
    *out=&r->iface;return S_OK;
}

/* Private only: the shader must lower integer mip-zero coordinates to tiled element indices.
 * Hold this table through the last consuming fence; its private-data reference owns the 0032 lease. */
static HRESULT r32_texel_table(IUnknown *owner,IUnknown *heap,SIZE_T slot,DXGI_FORMAT packed_read,ID3D12DescriptorHeap **out)
{
    static const GUID backing={0xc6f673d7,0x7ba3,0x4ad3,{0x99,0x3b,0xdf,0x61,0x80,0xdc,0xe3,0x2b}};
    if (!out) return E_POINTER;
    *out=NULL;
    if (!owner || !heap) return E_INVALIDARG;
    struct xbox_device *d=CONTAINING_RECORD(owner,struct xbox_device,iface);
    BOOL decode=packed_read==DXGI_FORMAT_R32_UINT;
    if (decode ? !xs_is_adapter(d->host,0x1002,0x164e) : packed_read!=DXGI_FORMAT_R9G9B9E5_SHAREDEXP || !xs_is_rtx3070(d->host)) return E_NOTIMPL;
    ID3D12Resource *buffer=NULL;
    AcquireSRWLockShared(&d->descriptor_lock);
    for (struct desc_heap *h=d->heaps;h;h=h->next) if (&h->iface==heap && h->visible) {
        D3D12_GPU_DESCRIPTOR_HANDLE unused;
        if (FAILED(descriptor_range(heap,slot,3,NULL,NULL,&unused))) break;
        const struct descriptor_record *r=h->records+(slot-h->start)/h->stride;
        if (!valid_address_record(r,slot,TILE_UINT_SRV) || !valid_address_record(r+1,slot+h->stride,TILE_UINT_UAV) ||
                !valid_address_record(r+2,slot+2*h->stride,TILE_RGB_SRV) || r->resource!=r[1].resource || r->resource!=r[2].resource) break;
        buffer=r->resource; ID3D12Resource_AddRef(buffer); break;
    }
    ReleaseSRWLockShared(&d->descriptor_lock);
    if (!buffer) return E_INVALIDARG;
    D3D12_FEATURE_DATA_FORMAT_SUPPORT rgb={.Format=packed_read},u={.Format=DXGI_FORMAT_R32_UINT};
    HRESULT hr=ID3D12Device_CheckFeatureSupport(d->host,D3D12_FEATURE_FORMAT_SUPPORT,&rgb,sizeof rgb);
    if (SUCCEEDED(hr)) hr=ID3D12Device_CheckFeatureSupport(d->host,D3D12_FEATURE_FORMAT_SUPPORT,&u,sizeof u);
    UINT loads=D3D12_FORMAT_SUPPORT1_BUFFER|D3D12_FORMAT_SUPPORT1_SHADER_LOAD;
    UINT writes=D3D12_FORMAT_SUPPORT2_UAV_TYPED_LOAD|D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE;
    if (SUCCEEDED(hr) && ((rgb.Support1&loads)!=loads || (u.Support1&(loads|D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW))!=(loads|D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW) || (u.Support2&writes)!=writes)) hr=DXGI_ERROR_UNSUPPORTED;
    ID3D12DescriptorHeap *table=NULL;
    D3D12_DESCRIPTOR_HEAP_DESC hd={D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,3,D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,1};
    if (SUCCEEDED(hr)) hr=ID3D12Device_CreateDescriptorHeap(d->host,&hd,&IID_ID3D12DescriptorHeap,(void **)&table);
    if (SUCCEEDED(hr)) hr=ID3D12DescriptorHeap_SetPrivateDataInterface(table,&backing,(IUnknown*)buffer);
    if (SUCCEEDED(hr)) {
        D3D12_SHADER_RESOURCE_VIEW_DESC srv={.Format=DXGI_FORMAT_R32_UINT,.ViewDimension=D3D12_SRV_DIMENSION_BUFFER,
            .Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,.Buffer.NumElements=1280*720};
        D3D12_UNORDERED_ACCESS_VIEW_DESC uav={.Format=DXGI_FORMAT_R32_UINT,.ViewDimension=D3D12_UAV_DIMENSION_BUFFER,.Buffer.NumElements=1280*720};
        D3D12_CPU_DESCRIPTOR_HANDLE cpu;table->lpVtbl->GetCPUDescriptorHandleForHeapStart(table,&cpu);
        UINT stride=ID3D12Device_GetDescriptorHandleIncrementSize(d->host,hd.Type);
        ID3D12Device_CreateShaderResourceView(d->host,buffer,&srv,cpu);cpu.ptr+=stride;
        ID3D12Device_CreateUnorderedAccessView(d->host,buffer,NULL,&uav,cpu);cpu.ptr+=stride;
        srv.Format=packed_read;ID3D12Device_CreateShaderResourceView(d->host,buffer,&srv,cpu);
        *out=table;
    } else if (table) ID3D12DescriptorHeap_Release(table);
    ID3D12Resource_Release(buffer);return hr;
}

HRESULT xs_r32_texel_table(IUnknown *owner,IUnknown *heap,SIZE_T slot,ID3D12DescriptorHeap **out)
{return r32_texel_table(owner,heap,slot,DXGI_FORMAT_R9G9B9E5_SHAREDEXP,out);}
/* Slot 2 is uint; only a shader that explicitly decodes RGB9E5 may use this table. */
HRESULT xs_r32_packed_uint_table(IUnknown *owner,IUnknown *heap,SIZE_T slot,ID3D12DescriptorHeap **out)
{return r32_texel_table(owner,heap,slot,DXGI_FORMAT_R32_UINT,out);}

/* Private explicit-tile ABI: full SRV, five mip SRVs, five packed-word UAVs.
 * As with the R32 table, the recorder retains the snapshot until the fence. */
HRESULT xs_r11_mip_table(IUnknown *owner,IUnknown *heap,SIZE_T slot,ID3D12DescriptorHeap **out)
{
    static const GUID backing={0xb50d8703,0xb544,0x4b5a,{0x8c,0x84,0x9f,0x61,0x66,0xab,0x93,0x1f}};
    if (!out) return E_POINTER;
    *out=NULL;
    if (!owner || !heap) return E_INVALIDARG;
    struct xbox_device *d=CONTAINING_RECORD(owner,struct xbox_device,iface);
    ID3D12Resource *buffer=NULL;
    AcquireSRWLockShared(&d->descriptor_lock);
    for (struct desc_heap *h=d->heaps;h;h=h->next) if (&h->iface==heap && h->visible) {
        D3D12_GPU_DESCRIPTOR_HANDLE unused;
        if (FAILED(descriptor_range(heap,slot,11,NULL,NULL,&unused))) break;
        const struct descriptor_record *r=h->records+(slot-h->start)/h->stride;
        UINT i;
        for (i=0;i<11;i++) if (!valid_address_record(r+i,slot+i*h->stride,i<6?TILE_R11_SRV:TILE_R11_UAV) ||
                r[i].resource!=r->resource || r[i].offset!=(i?(i-1)%5:0) || r[i].elements!=(i?1u:5u)) break;
        if (i==11) {buffer=r->resource; ID3D12Resource_AddRef(buffer);} break;
    }
    ReleaseSRWLockShared(&d->descriptor_lock);
    if (!buffer) return E_INVALIDARG;
    D3D12_FEATURE_DATA_FORMAT_SUPPORT f={.Format=DXGI_FORMAT_R11G11B10_FLOAT},u={.Format=DXGI_FORMAT_R32_UINT};
    HRESULT hr=ID3D12Device_CheckFeatureSupport(d->host,D3D12_FEATURE_FORMAT_SUPPORT,&f,sizeof f);
    if (SUCCEEDED(hr)) hr=ID3D12Device_CheckFeatureSupport(d->host,D3D12_FEATURE_FORMAT_SUPPORT,&u,sizeof u);
    UINT loads=D3D12_FORMAT_SUPPORT1_BUFFER|D3D12_FORMAT_SUPPORT1_SHADER_LOAD;
    UINT writes=D3D12_FORMAT_SUPPORT2_UAV_TYPED_LOAD|D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE;
    if (SUCCEEDED(hr) && ((f.Support1&loads)!=loads || !(u.Support1&D3D12_FORMAT_SUPPORT1_BUFFER) ||
            !(u.Support1&D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW) || (u.Support2&writes)!=writes)) hr=DXGI_ERROR_UNSUPPORTED;
    ID3D12DescriptorHeap *table=NULL;
    D3D12_DESCRIPTOR_HEAP_DESC hd={D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,11,D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,1};
    if (SUCCEEDED(hr)) hr=ID3D12Device_CreateDescriptorHeap(d->host,&hd,&IID_ID3D12DescriptorHeap,(void**)&table);
    if (SUCCEEDED(hr)) hr=ID3D12DescriptorHeap_SetPrivateDataInterface(table,&backing,(IUnknown*)buffer);
    if (SUCCEEDED(hr)) {
        UINT stride=ID3D12Device_GetDescriptorHandleIncrementSize(d->host,hd.Type);
        D3D12_CPU_DESCRIPTOR_HANDLE cpu;table->lpVtbl->GetCPUDescriptorHandleForHeapStart(table,&cpu);
        for (UINT i=0;i<11;i++,cpu.ptr+=stride) {
            UINT level=i?(i-1)%5:0, first=i?r11_end[4-level]/4:0, count=r11_end[5-level]/4-first;
            if (i<6) {
                D3D12_SHADER_RESOURCE_VIEW_DESC srv={.Format=f.Format,.ViewDimension=D3D12_SRV_DIMENSION_BUFFER,
                    .Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,.Buffer={first,count}};
                ID3D12Device_CreateShaderResourceView(d->host,buffer,&srv,cpu);
            } else {
                D3D12_UNORDERED_ACCESS_VIEW_DESC uv={.Format=u.Format,.ViewDimension=D3D12_UAV_DIMENSION_BUFFER,.Buffer={first,count}};
                ID3D12Device_CreateUnorderedAccessView(d->host,buffer,NULL,&uv,cpu);
            }
        }
        *out=table;
    } else if (table) ID3D12DescriptorHeap_Release(table);
    ID3D12Resource_Release(buffer);return hr;
}
