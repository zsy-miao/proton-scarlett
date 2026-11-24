/* The captured scatter list is one contiguous VA span backed by one tile
 * section. Validate both complete arrays before changing the address space. */
static BOOL reserved_extent(void *at,SIZE_T bytes)
{
    MEMORY_BASIC_INFORMATION m;
    return VirtualQuery(at,&m,sizeof m)==sizeof m && m.AllocationBase==at &&
        m.BaseAddress==at && m.RegionSize==bytes && m.State==MEM_RESERVE;
}
static void restore_scatter_arena(Arena *a,BOOL split)
{
    if ((split && !VirtualFree(a->base,a->size,MEM_RELEASE|MEM_COALESCE_PLACEHOLDERS)) ||
        !occupy_descriptor(a->base,a->size)) {
        /* Cannot return a normal failure with a half-restored reservation. */
        RaiseFailFastException(NULL,NULL,0);
        __builtin_trap();
    }
}
static DWORD scatter_span(Arena *a,void *at,SIZE_T bytes,ULONG_PTR id,BOOL unmap)
{
    SIZE_T delta=(UINT_PTR)at-(UINT_PTR)a->base;
    if (bytes>a->size-delta) return ERROR_INVALID_ADDRESS;
    BOOL split=bytes!=a->size;
    if (unmap) {
        if (a->scatter_at!=at || a->scatter_bytes!=bytes) return ERROR_INVALID_ADDRESS;
        struct scarlett_section_info i;
        BOOL (WINAPI *same)(HANDLE,HANDLE)=(void *)GetProcAddress(GetModuleHandleW(L"kernelbase.dll"),"CompareObjectHandles");
        if (!same || FAILED(scarlett_query_section(0x5343000c,at,bytes,&i))) return ERROR_INVALID_ADDRESS;
        BOOL live=i.base==at && i.offset==a->scatter_offset && i.view_size==tile_bytes(a->scatter_part) &&
                  same(i.section,a->tile_run->tile[a->scatter_part]);
        CloseHandle(i.section);
        SIZE_T tail=a->size-delta-bytes;
        if (!live || (delta && !reserved_extent(a->base,delta)) ||
            (tail && !reserved_extent((BYTE *)at+bytes,tail))) return ERROR_INVALID_ADDRESS;
        if (!xmem_wc_slot(at,bytes,NULL,0,0)) return GetLastError();
        restore_scatter_arena(a,split);
        --a->tile_run->views; --a->views;
        a->tile_run=NULL; a->scatter_at=NULL;
        return ERROR_SUCCESS;
    }
    if (a->views || a->scatter_at || !reserved_extent(a->base,a->size) || !descriptor_marker(a->base))
        return ERROR_INVALID_ADDRESS;
    PageRun *r=runs;
    while (r && (!r->tile[0] || id<r->first || (id-r->first)/16>=r->count)) r=r->next;
    if (!r || (id-r->first)%16) return ERROR_INVALID_PARAMETER;
    SIZE_T off=(id-r->first)/16*XMEM_PAGE;
    unsigned part=0;
    while (off>=tile_bytes(part)) { off-=tile_bytes(part); ++part; }
    if (bytes>tile_bytes(part)-off) return ERROR_NOT_SUPPORTED;
    if (!VirtualFree(a->base,a->size,MEM_RELEASE|MEM_PRESERVE_PLACEHOLDER)) return GetLastError();
    if (split && !VirtualFree(at,bytes,MEM_RELEASE|MEM_PRESERVE_PLACEHOLDER)) {
        DWORD err=GetLastError(); restore_scatter_arena(a,FALSE); return err;
    }
    if (!xmem_wc_slot(at,bytes,r->tile[part],off,0x2004)) {
        DWORD err=GetLastError(); restore_scatter_arena(a,split); return err;
    }
    a->tile_run=r; ++r->views; ++a->views;
    a->scatter_at=at; a->scatter_bytes=bytes; a->scatter_part=part; a->scatter_offset=off;
    return ERROR_SUCCESS;
}
__declspec(dllexport) BOOL WINAPI XMemMapPhysicalPagesScatter(void **addresses,ULONG_PTR n,PULONG_PTR ids)
{
    DWORD err=ERROR_INVALID_PARAMETER;
    if (!addresses || !n || n>TILE_PAGES) goto fail;
    SIZE_T length=n*sizeof(ULONG_PTR),got;
    ULONG_PTR *list=HeapAlloc(GetProcessHeap(),0,length*(ids?2:1));
    if (!list) { err=ERROR_NOT_ENOUGH_MEMORY; goto fail; }
    if (!ReadProcessMemory(GetCurrentProcess(),addresses,list,length,&got) || got!=length ||
        (ids && (!ReadProcessMemory(GetCurrentProcess(),ids,list+n,length,&got) || got!=length))) {
        err=ERROR_NOACCESS; goto done;
    }
    SIZE_T bytes=n*XMEM_PAGE;
    BOOL unmap=!ids || !list[n];
    if (!list[0] || list[0]%XMEM_PAGE || list[0]>UINTPTR_MAX-bytes ||
        (!unmap && list[n]>UINTPTR_MAX-16*(n-1))) goto done;
    for (ULONG_PTR j=1;j<n;j++)
        if (list[j]!=list[0]+j*XMEM_PAGE || (ids && list[n+j]!=(unmap ? 0 : list[n]+16*j))) {
            err=ERROR_NOT_SUPPORTED; goto done;
        }
    err=ERROR_INVALID_ADDRESS;
    AcquireSRWLockExclusive(&page_lock);
    for (Arena *a=arenas;a;a=a->next) if (a->scatter && list[0]-(UINT_PTR)a->base<a->size) {
        err=scatter_span(a,(void *)list[0],bytes,ids?list[n]:0,unmap); break;
    }
    ReleaseSRWLockExclusive(&page_lock);
done:
    HeapFree(GetProcessHeap(),0,list);
fail:
    if (err) SetLastError(err);
    return !err;
}
