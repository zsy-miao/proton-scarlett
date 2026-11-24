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
