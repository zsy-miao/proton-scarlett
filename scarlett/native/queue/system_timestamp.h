/* The title passes a CPU address into the marked system pool, not a GPU VA. */
struct system_timestamp {
    LONG refs;
    BOOL second_bank;
    void *mapping;
    ID3D12Heap *retained;
    ID3D12Resource *dst;
    ID3D12QueryHeap *query;
#ifdef SCARLETT_EXPERIMENTAL_PRIMARY_RELEASE
    HANDLE section;
    UINT64 tail_addr;
    SIZE_T tail_offset;
    BOOL tail_recorded;
#endif
};
static void drop_timestamp(struct system_timestamp *t)
{
    if (!t || InterlockedDecrement(&t->refs)) return;
    if (t->query) ID3D12QueryHeap_Release(t->query);
    if (t->dst) ID3D12Resource_Release(t->dst);
    if (t->retained) ID3D12Heap_Release(t->retained);
    if (t->mapping) UnmapViewOfFile(t->mapping);
#ifdef SCARLETT_EXPERIMENTAL_PRIMARY_RELEASE
    if (t->section) CloseHandle(t->section);
#endif
    HeapFree(GetProcessHeap(),0,t);
}
static HRESULT record_timestamp(ID3D12Device *d,ID3D12GraphicsCommandList7 *list,UINT64 addr,BOOL second_prefix,struct system_timestamp **out)
{
    struct scarlett_section_info i;
    HRESULT hr=scarlett_acquire_section((void *)addr,8,&i);
    BOOL wb=FAILED(hr);
    if (wb && FAILED(hr=scarlett_query_section(0x5343000c,(void*)addr,8,&i))) return hr;
    const UINT window=65536;
    BOOL second=second_prefix && i.offset==0x419aa38;
    if (!i.section || i.view_size!=0x49400000 || i.offset!=addr-(UINT_PTR)i.base || i.offset>i.view_size-8 || (wb && !second && i.offset!=0x4196218 && i.offset!=0x4196698 && i.offset!=0x41964a8)) {
        if (i.section) CloseHandle(i.section);
        return E_INVALIDARG;
    }
    struct system_timestamp *t=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof *t);
    if (!t) {CloseHandle(i.section);return E_OUTOFMEMORY;}
    t->refs=1;t->second_bank=second;
    ID3D12Heap *heap=NULL;
    if (wb) {
        HRESULT (WINAPI *open)(ID3D12Device *,SIZE_T,HANDLE,HANDLE *,ID3D12Heap **)=(void *)GetProcAddress(GetModuleHandleW(L"d3d12core.dll"),"ScarlettWbHeapV1");
        hr=open?open(d,i.view_size,i.section,NULL,&t->retained):E_NOTIMPL;
        heap=t->retained;
    } else {
        t->mapping=MapViewOfFile(i.section,FILE_MAP_READ|FILE_MAP_WRITE,0,(DWORD)i.offset&~(window-1),window);
        hr=t->mapping?S_OK:HRESULT_FROM_WIN32(GetLastError());
        ID3D12Device3 *d3=NULL;
        if (SUCCEEDED(hr)) hr=ID3D12Device_QueryInterface(d,&IID_ID3D12Device3,(void **)&d3);
        if (SUCCEEDED(hr)) hr=ID3D12Device3_OpenExistingHeapFromAddress(d3,t->mapping,&IID_ID3D12Heap,(void **)&heap);
        if (d3) ID3D12Device3_Release(d3);
    }
    D3D12_QUERY_HEAP_DESC qd={D3D12_QUERY_HEAP_TYPE_TIMESTAMP,1,1};
#ifdef SCARLETT_EXPERIMENTAL_PRIMARY_RELEASE
    if (i.offset==0x4196218
#ifdef SCARLETT_EXPERIMENTAL_TAGGED_BATCH
            || i.offset==0x41964a8 || i.offset==0x4196698
#endif
    ) {
        t->section=i.section;
        t->tail_addr=addr-8;
        t->tail_offset=i.offset-8;
        qd.Count=2;
    } else
#endif
        CloseHandle(i.section);
    D3D12_RESOURCE_DESC rd={.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER,.Width=window,.Height=1,
        .DepthOrArraySize=1,.MipLevels=1,.SampleDesc={1,0},.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR,.Flags=D3D12_RESOURCE_FLAG_ALLOW_CROSS_ADAPTER};
    if (wb) rd.Flags=D3D12_RESOURCE_FLAG_NONE;
    if (SUCCEEDED(hr)) hr=ID3D12Device_CreatePlacedResource(d,heap,wb?(i.offset&~(SIZE_T)(window-1)):0,&rd,D3D12_RESOURCE_STATE_COPY_DEST,NULL,&IID_ID3D12Resource,(void **)&t->dst);
    if (!wb && heap) ID3D12Heap_Release(heap);
    if (SUCCEEDED(hr)) hr=ID3D12Device_CreateQueryHeap(d,&qd,&IID_ID3D12QueryHeap,(void **)&t->query);
    if (FAILED(hr)) {drop_timestamp(t);return hr;}
    ID3D12GraphicsCommandList7_EndQuery(list,t->query,D3D12_QUERY_TYPE_TIMESTAMP,0);
    ID3D12GraphicsCommandList7_ResolveQueryData(list,t->query,D3D12_QUERY_TYPE_TIMESTAMP,0,1,t->dst,i.offset%window);
    *out=t;return S_OK;
}
#ifdef SCARLETT_EXPERIMENTAL_PRIMARY_RELEASE
static HRESULT record_tail_timestamp(struct system_timestamp *t,ID3D12GraphicsCommandList7 *list,UINT64 addr)
{
    if (!t->section || t->tail_recorded) return E_NOTIMPL;
    if (addr!=t->tail_addr) return E_INVALIDARG;
    struct scarlett_section_info i;
    HRESULT hr=t->retained ? scarlett_query_section(0x5343000c,(void *)addr,8,&i) :
        scarlett_acquire_section((void *)addr,8,&i);
    if (FAILED(hr)) return hr;
    BOOL (WINAPI *same)(HANDLE,HANDLE)=(void *)GetProcAddress(GetModuleHandleW(L"kernelbase.dll"),"CompareObjectHandles");
    BOOL match=i.section && i.offset==t->tail_offset && i.view_size==0x49400000 &&
        addr==(UINT_PTR)i.base+i.offset && same && same(t->section,i.section);
    if (i.section) CloseHandle(i.section);
    if (!match) return E_INVALIDARG;
    /* Both queries resolve into the original window held through submission retirement. */
    ID3D12GraphicsCommandList7_EndQuery(list,t->query,D3D12_QUERY_TYPE_TIMESTAMP,1);
    ID3D12GraphicsCommandList7_ResolveQueryData(list,t->query,D3D12_QUERY_TYPE_TIMESTAMP,1,1,t->dst,i.offset%65536);
    t->tail_recorded=TRUE;
    return S_OK;
}
#endif
