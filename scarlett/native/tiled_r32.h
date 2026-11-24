/* Private 0032 ABI. Resolve the allocation by guest VA each time; callers own the returned buffer. */
static HRESULT open_r32_buffer(struct xbox_device *d,UINT64 va,ID3D12Resource **out)
{
    const UINT span=0x384000;ID3D12Heap *heap=NULL;UINT off=0;HANDLE section=NULL;
    HRESULT (WINAPI *place)(ID3D12Device*,ID3D12Heap*,HANDLE,SIZE_T,SIZE_T,ID3D12Resource**)=(void*)GetProcAddress(GetModuleHandleW(L"d3d12core.dll"),"ScarlettTiledR32BufferV1");
    if(!place)return DXGI_ERROR_UNSUPPORTED;
    HRESULT hr=native_pool_ref(d->host,(void*)va,span,&heap,NULL,&off);
    if(hr==S_FALSE){
        struct scarlett_section_info i;
        if(!xs_is_rtx3070(d->host)) return E_NOTIMPL;
        if(FAILED(hr=scarlett_query_section(0x53430005,(void*)va,span,&i)))return hr;
        if(!i.section || i.view_size!=WC_POOL_BYTES || i.offset>i.view_size-span || va!=(UINT_PTR)i.base+i.offset){if(i.section)CloseHandle(i.section);return E_INVALIDARG;}
        section=i.section;off=i.offset;
    }else if(hr!=S_OK)return hr;
    hr=place(d->host,heap,section,WC_POOL_BYTES,off,out);
    if(heap)ID3D12Heap_Release(heap);else CloseHandle(section);
    return hr;
}
/* Included by d3d12_device.c, only for the observed WC swizzle 0x102 resource. */
static HRESULT create_tiled_r32(struct xbox_device *d,UINT64 va,UINT state,const void *clear,REFIID iid,void **out)
{
    if (!out) return E_POINTER;
    *out=NULL;IID id;
    if (!iid || !copy_in(&id,iid,sizeof id)) return E_POINTER;
    if (!IsEqualIID(&id,&IID_ID3D12Resource)) return E_NOINTERFACE;
    if (!va || state!=3 || clear) return E_INVALIDARG;
    struct color_resource *r=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof *r);
    if (!r)return E_OUTOFMEMORY;
    HRESULT hr=open_r32_buffer(d,va,&r->host);
    if (FAILED(hr)) {HeapFree(GetProcessHeap(),0,r);return hr;}
    r->iface.lpVtbl=&color_vtbl.unknown;r->refs=1;r->owner=d;r->tiled_r32=TRUE;IUnknown_AddRef(&d->iface);
    AcquireSRWLockExclusive(&d->descriptor_lock);r->next=d->colors;d->colors=r;ReleaseSRWLockExclusive(&d->descriptor_lock);
    *out=&r->iface;return S_OK;
}

/* Private only: the shader must lower integer mip-zero coordinates to tiled element indices.
 * Hold this table through the last consuming fence; its private-data reference owns the 0032 lease. */
HRESULT xs_r32_texel_table(IUnknown *owner,IUnknown *identity,ID3D12DescriptorHeap **out)
{
    static const GUID backing={0xc6f673d7,0x7ba3,0x4ad3,{0x99,0x3b,0xdf,0x61,0x80,0xdc,0xe3,0x2b}};
    if (!out) return E_POINTER;
    *out=NULL;
    if (!owner || !identity) return E_INVALIDARG;
    struct xbox_device *d=CONTAINING_RECORD(owner,struct xbox_device,iface);
    if (!xs_is_rtx3070(d->host)) return E_NOTIMPL;
    ID3D12Resource *buffer=NULL;
    AcquireSRWLockShared(&d->descriptor_lock);
    for (struct color_resource *r=d->colors;r;r=r->next) if (&r->iface==identity && r->tiled_r32) {
        buffer=r->host;ID3D12Resource_AddRef(buffer);break;
    }
    ReleaseSRWLockShared(&d->descriptor_lock);
    if (!buffer) return E_INVALIDARG;
    D3D12_FEATURE_DATA_FORMAT_SUPPORT rgb={.Format=DXGI_FORMAT_R9G9B9E5_SHAREDEXP},u={.Format=DXGI_FORMAT_R32_UINT};
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
        srv.Format=DXGI_FORMAT_R9G9B9E5_SHAREDEXP;ID3D12Device_CreateShaderResourceView(d->host,buffer,&srv,cpu);
        *out=table;
    } else if (table) ID3D12DescriptorHeap_Release(table);
    ID3D12Resource_Release(buffer);return hr;
}
