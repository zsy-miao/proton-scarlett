/* The title passes a CPU address into the marked system pool, not a GPU VA. */
struct system_timestamp {
    LONG refs;
    BOOL second_bank;
#ifdef SCARLETT_PAIR_SUBMIT
    HANDLE cpu_section;
    UINT64 cpu_addr;
    SIZE_T cpu_offset;
#endif
    void *mapping;
    ID3D12Heap *retained;
    ID3D12Resource *dst;
    ID3D12QueryHeap *query;
#ifdef SCARLETT_EXPERIMENTAL_PRIMARY_RELEASE
    HANDLE section;
    UINT64 tail_addr;
    SIZE_T tail_offset;
    BOOL tail_recorded;
#ifdef SCARLETT_PAIR_SUBMIT
    BOOL successor_bank;
#endif
#endif
};
static void drop_timestamp(struct system_timestamp *t)
{
    if (!t || InterlockedDecrement(&t->refs)) return;
    if (t->query) ID3D12QueryHeap_Release(t->query);
    if (t->dst) ID3D12Resource_Release(t->dst);
    if (t->retained) ID3D12Heap_Release(t->retained);
    if (t->mapping) UnmapViewOfFile(t->mapping);
#ifdef SCARLETT_PAIR_SUBMIT
    if(t->cpu_section)CloseHandle(t->cpu_section);
#endif
#ifdef SCARLETT_EXPERIMENTAL_PRIMARY_RELEASE
    if (t->section) CloseHandle(t->section);
#endif
    HeapFree(GetProcessHeap(),0,t);
}
/* +a1c309 selects one of four live records; +2482b16 indexes its qword array. */
static BOOL indexed_stamp(UINT64 addr,const struct scarlett_section_info *pool,UINT bank,UINT index,const UINT_PTR *records)
{
    UINT_PTR owner;DWORD w[24];SIZE_T n;
    if(bank>=4 || index!=1 || !ReadProcessMemory(GetCurrentProcess(),records+bank,&owner,sizeof owner,&n) || n!=sizeof owner ||
       pool->view_size!=0x49400000 || owner<(UINT_PTR)pool->base || owner-(UINT_PTR)pool->base>pool->view_size-sizeof w ||
       !ReadProcessMemory(GetCurrentProcess(),(void*)owner,w,sizeof w,&n) || n!=sizeof w) return FALSE;
    UINT64 array,mirror,used;memcpy(&array,w+22,8);memcpy(&mirror,w+20,8);memcpy(&used,w+18,8);
    return w[2]==0x800 && w[3]==8 && (w[14]>>16)==2 && array==mirror && array>=(UINT_PTR)pool->base &&
        array-(UINT_PTR)pool->base<=pool->view_size-0x4010-0x800 && used==array+0x4010 && addr==array+index*8;
}
static BOOL active_stamp(UINT64 addr,const struct scarlett_section_info *pool)
{
    UINT_PTR exe=(UINT_PTR)GetModuleHandleW(NULL);UINT bank,index;SIZE_T n;
    return ReadProcessMemory(GetCurrentProcess(),(void*)(exe+0xa943a0c),&bank,4,&n) && n==4 &&
        ReadProcessMemory(GetCurrentProcess(),(void*)(exe+0x75dedb0),&index,4,&n) && n==4 &&
        indexed_stamp(addr,pool,bank,index,(const UINT_PTR*)(exe+0xa943a18));
}
#ifdef SCARLETT_PAIR_SUBMIT
/* Validate the producer record through the very section used for the GPU import. */
static BOOL third_wb_target(UINT64 addr,const struct scarlett_section_info *dst)
{
    UINT_PTR exe=(UINT_PTR)GetModuleHandleW(NULL),record;DWORD bank,index;SIZE_T n;
    if(!dst->section || dst->offset!=0x419f258 || dst->view_size!=0x49400000 ||
       addr-(UINT_PTR)dst->base!=dst->offset ||
       !ReadProcessMemory(GetCurrentProcess(),(void*)(exe+0xa943a0c),&bank,4,&n) || n!=4 || bank!=3 ||
       !ReadProcessMemory(GetCurrentProcess(),(void*)(exe+0x75dedb0),&index,4,&n) || n!=4 || index!=1 ||
       !ReadProcessMemory(GetCurrentProcess(),(void*)(exe+0xa943a18+3*8),&record,sizeof record,&n) || n!=sizeof record ||
       record-(UINT_PTR)dst->base!=0x3fb30c0)return FALSE;
    struct scarlett_section_info owner;
    if(FAILED(scarlett_query_section(0x5343000c,(void*)record,96,&owner)))return FALSE;
    BOOL (WINAPI *same)(HANDLE,HANDLE)=(void*)GetProcAddress(GetModuleHandleW(L"kernelbase.dll"),"CompareObjectHandles");
    BOOL valid=owner.base==dst->base && owner.view_size==dst->view_size && owner.offset==0x3fb30c0 &&
        owner.section && same && same(owner.section,dst->section);
    if(owner.section)CloseHandle(owner.section);
    if(!valid)return FALSE;
    struct scarlett_section_info alias;
    if(FAILED(scarlett_query_section_handle(0x5343000b,NULL,dst->view_size,&alias,dst->section)))return FALSE;
    DWORD w[24];UINT64 used,mirror,array;
    valid=alias.base && alias.view_size==dst->view_size;
    if(valid)memcpy(w,(BYTE*)alias.base+0x3fb30c0,sizeof w);
    if(!alias.base || !VirtualFree(alias.base,0,MEM_RELEASE) || !valid)return FALSE;
    memcpy(&used,w+18,8);memcpy(&mirror,w+20,8);memcpy(&array,w+22,8);
    return w[2]==2048 && w[3]==8 && (w[14]>>16)==2 && array==addr-8 &&
        mirror==array && used==array+0x4010;
}
static HRESULT prepare_timestamp(ID3D12Device *d,UINT64 addr,UINT prefix,struct system_timestamp **out,SIZE_T *resolve)
#else
static HRESULT record_timestamp(ID3D12Device *d,ID3D12GraphicsCommandList7 *list,UINT64 addr,BOOL second_prefix,struct system_timestamp **out)
#endif
{
    struct scarlett_section_info i;
    HRESULT hr=scarlett_acquire_section((void *)addr,8,&i);
    BOOL wb=FAILED(hr);
    if (wb && FAILED(hr=scarlett_query_section(0x5343000c,(void*)addr,8,&i))) return hr;
    const UINT window=65536;
#ifdef SCARLETT_PAIR_SUBMIT
    BOOL second=prefix==1 && (i.offset==0x419aa38 || (i.section && i.view_size==0x49400000 && active_stamp(addr,&i)));
    if (!i.section || i.view_size!=0x49400000 || i.offset!=addr-(UINT_PTR)i.base || i.offset>i.view_size-8 ||
        (prefix==2 ? !wb || !third_wb_target(addr,&i) :
        wb && !second && i.offset!=0x4196218 && i.offset!=0x4196698 && i.offset!=0x41964a8)) {
#else
    BOOL second=second_prefix && (i.offset==0x419aa38 || (i.section && i.view_size==0x49400000 && active_stamp(addr,&i)));
    if (!i.section || i.view_size!=0x49400000 || i.offset!=addr-(UINT_PTR)i.base || i.offset>i.view_size-8 || (wb && !second && i.offset!=0x4196218 && i.offset!=0x4196698 && i.offset!=0x41964a8)) {
#endif
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
#ifdef SCARLETT_PAIR_SUBMIT
    t->successor_bank=wb && second && i.offset==0x419aa38;
#endif
    if (i.offset==0x4196218
#ifdef SCARLETT_PAIR_SUBMIT
            || t->successor_bank
#endif
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
#ifdef SCARLETT_PAIR_SUBMIT
    if(!wb){
        t->cpu_section=i.section;t->cpu_addr=addr;t->cpu_offset=i.offset;
    }else
#endif
        CloseHandle(i.section);
    D3D12_RESOURCE_DESC rd={.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER,.Width=window,.Height=1,
        .DepthOrArraySize=1,.MipLevels=1,.SampleDesc={1,0},.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR,.Flags=D3D12_RESOURCE_FLAG_ALLOW_CROSS_ADAPTER};
    if (wb) rd.Flags=D3D12_RESOURCE_FLAG_NONE;
    if (SUCCEEDED(hr)) hr=ID3D12Device_CreatePlacedResource(d,heap,wb?(i.offset&~(SIZE_T)(window-1)):0,&rd,D3D12_RESOURCE_STATE_COPY_DEST,NULL,&IID_ID3D12Resource,(void **)&t->dst);
    if (!wb && heap) ID3D12Heap_Release(heap);
    if (SUCCEEDED(hr)) hr=ID3D12Device_CreateQueryHeap(d,&qd,&IID_ID3D12QueryHeap,(void **)&t->query);
    if (FAILED(hr)) {drop_timestamp(t);return hr;}
#ifdef SCARLETT_PAIR_SUBMIT
    *resolve=i.offset%window;
    *out=t;return S_OK;
}
static void emit_q0(struct system_timestamp *t,ID3D12GraphicsCommandList7 *list,SIZE_T offset)
{
    ID3D12GraphicsCommandList7_EndQuery(list,t->query,D3D12_QUERY_TYPE_TIMESTAMP,0);
    ID3D12GraphicsCommandList7_ResolveQueryData(list,t->query,D3D12_QUERY_TYPE_TIMESTAMP,0,1,t->dst,offset);
}
static HRESULT record_timestamp(ID3D12Device *d,ID3D12GraphicsCommandList7 *list,UINT64 addr,UINT prefix,struct system_timestamp **out)
{
    SIZE_T off;HRESULT hr=prepare_timestamp(d,addr,prefix,out,&off);
    if(SUCCEEDED(hr))emit_q0(*out,list,off);
    return hr;
}
#else
    ID3D12GraphicsCommandList7_EndQuery(list,t->query,D3D12_QUERY_TYPE_TIMESTAMP,0);
    ID3D12GraphicsCommandList7_ResolveQueryData(list,t->query,D3D12_QUERY_TYPE_TIMESTAMP,0,1,t->dst,i.offset%window);
    *out=t;return S_OK;
}
#endif
#ifdef SCARLETT_EXPERIMENTAL_PRIMARY_RELEASE
static HRESULT record_tail_timestamp(struct system_timestamp *t,ID3D12GraphicsCommandList7 *list,UINT64 addr)
{
#ifdef SCARLETT_PAIR_SUBMIT
    if (t->successor_bank) return E_NOTIMPL;
#endif
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
#ifdef SCARLETT_PAIR_SUBMIT
static inline BOOL cpu_successor_target(struct system_timestamp *head,struct system_timestamp *tail,
        UINT bank,UINT64 indices,UINT_PTR record)
{
    if(!head || !tail || !head->second_bank || !head->mapping || !tail->mapping ||
       !head->cpu_section || !tail->cpu_section || bank>=4 || indices!=1 ||
       head->cpu_addr!=tail->cpu_addr+8 ||
       !((head->cpu_offset==0x419aeb8 && tail->cpu_offset==0x419aeb0) ||
         (head->cpu_offset==0x419aa38 && tail->cpu_offset==0x419aa30)) ||
       head->mapping==tail->mapping || tail->second_bank || head->retained || tail->retained ||
       head->dst==tail->dst || head->query==tail->query)return FALSE;
    struct scarlett_section_info owner;
    if(FAILED(scarlett_acquire_section((void*)record,96,&owner)))return FALSE;
    BOOL (WINAPI *same)(HANDLE,HANDLE)=(void*)GetProcAddress(GetModuleHandleW(L"kernelbase.dll"),"CompareObjectHandles");
    UINT_PTR records[4]={0};records[bank]=record;
    BOOL valid=owner.section && same && same(owner.section,head->cpu_section) && same(owner.section,tail->cpu_section) &&
        tail->cpu_addr==(UINT_PTR)owner.base+tail->cpu_offset && indexed_stamp(head->cpu_addr,&owner,bank,1,records);
    if(owner.section)CloseHandle(owner.section);
    return valid;
}
/* High index selects the release-side qword. The imported window is already
 * pinned by the producer; never open a new GPU resource for the current VA. */
static HRESULT successor_target(struct system_timestamp *t,UINT64 addr,UINT bank,UINT64 indices,UINT_PTR record)
{
    if(!t || !t->successor_bank || !t->retained || !t->section || !t->dst || !t->query ||
       t->tail_recorded || bank!=2 || indices!=1 || addr!=t->tail_addr || t->tail_offset!=0x419aa30)return E_INVALIDARG;
    struct scarlett_section_info i,owner;
    HRESULT hr=scarlett_query_section(0x5343000c,(void*)addr,8,&i);
    if(FAILED(hr))return hr;
    BOOL (WINAPI *same)(HANDLE,HANDLE)=(void*)GetProcAddress(GetModuleHandleW(L"kernelbase.dll"),"CompareObjectHandles");
    UINT_PTR records[4]={0,0,record,0},base=(UINT_PTR)i.base;
    BOOL valid=base && addr>=base && record>=base && record-base==0x3fb3050 &&
        addr-base==t->tail_offset && i.offset==t->tail_offset && i.view_size==0x49400000 &&
        i.section && same && same(i.section,t->section);
    if(valid){
        hr=scarlett_query_section(0x5343000c,(void*)record,96,&owner);
        valid=SUCCEEDED(hr) && owner.section && same(owner.section,t->section) &&
            owner.base==i.base && owner.view_size==i.view_size && owner.offset==record-base &&
            indexed_stamp(addr+8,&i,bank,1,records);
        if(SUCCEEDED(hr) && owner.section)CloseHandle(owner.section);
    }
    if(i.section)CloseHandle(i.section);
    return valid?S_OK:E_INVALIDARG;
}
static inline void emit_successor_timestamp(struct system_timestamp *t,ID3D12GraphicsCommandList7 *after_release)
{
    ID3D12GraphicsCommandList7_EndQuery(after_release,t->query,D3D12_QUERY_TYPE_TIMESTAMP,1);
    ID3D12GraphicsCommandList7_ResolveQueryData(after_release,t->query,D3D12_QUERY_TYPE_TIMESTAMP,1,1,t->dst,t->tail_offset%65536);
}
#endif
#endif
