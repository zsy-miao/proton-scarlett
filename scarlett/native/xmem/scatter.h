/* The captured scatter list is one contiguous VA span backed by one tile
 * section. Validate both complete arrays before changing the address space. */
static BOOL reserved_extent(void *at,SIZE_T bytes)
{
    MEMORY_BASIC_INFORMATION m;
    return VirtualQuery(at,&m,sizeof m)==sizeof m && m.AllocationBase==at &&
        m.BaseAddress==at && m.RegionSize==bytes && m.State==MEM_RESERVE;
}
static BOOL marked_gap(BYTE *at,SIZE_T bytes)
{
    return !bytes || (reserved_extent(at,bytes) && descriptor_marker(at));
}
static BOOL release_gap(BYTE *at,SIZE_T bytes)
{
    return !bytes || VirtualFree(at,bytes,MEM_RELEASE|MEM_PRESERVE_PLACEHOLDER);
}
static void restore_gap(BYTE *at,SIZE_T bytes,BOOL split)
{
    if (bytes && ((split && !VirtualFree(at,bytes,MEM_RELEASE|MEM_COALESCE_PLACEHOLDERS)) ||
        !occupy_descriptor(at,bytes))) { RaiseFailFastException(NULL,NULL,0); __builtin_trap(); }
}
static BOOL live_extent(Arena *a,ScatterExtent v)
{
    struct scarlett_section_info i;
    BOOL (WINAPI *same)(HANDLE,HANDLE)=(void *)GetProcAddress(GetModuleHandleW(L"kernelbase.dll"),"CompareObjectHandles");
    if (!same || FAILED(scarlett_query_section(0x5343000c,v.at,v.bytes,&i))) return FALSE;
    BOOL ok=i.base==v.at && i.offset==v.offset && i.view_size==tile_bytes(a->scatter_part) &&
            same(i.section,a->tile_run->tile[a->scatter_part]);
    CloseHandle(i.section);
    return ok;
}
static DWORD scatter_span(Arena *a,BYTE *at,SIZE_T bytes,ULONG_PTR id,BOOL unmap)
{
    SIZE_T delta=(UINT_PTR)at-(UINT_PTR)a->base;
    if (bytes>a->size-delta) return ERROR_INVALID_ADDRESS;
    /* shortcut: three disjoint spans from one tile section. All vacant gaps keep
     * the reservation marker, including the gap restored by a partial teardown. */
    unsigned n=(unsigned)a->views,pos=0;
    if (n>ARRAYSIZE(a->extent) || (n && !a->tile_run)) return ERROR_INVALID_ADDRESS;
    BYTE *begin=a->base,*end=begin+a->size;
    for (unsigned j=0;j<n;j++) {
        ScatterExtent v=a->extent[j];
        if (!marked_gap(begin,v.at-begin) || !live_extent(a,v)) return ERROR_INVALID_ADDRESS;
        begin=v.at+v.bytes;
        if (v.at<at) ++pos;
    }
    if (!marked_gap(begin,end-begin)) return ERROR_INVALID_ADDRESS;
    begin=pos?a->extent[pos-1].at+a->extent[pos-1].bytes:(BYTE *)a->base;
    if (unmap) {
        if (pos==n || a->extent[pos].at!=at || a->extent[pos].bytes!=bytes) return ERROR_INVALID_ADDRESS;
        if (pos+1<n) end=a->extent[pos+1].at;
        SIZE_T left=at-begin,right=end-(at+bytes);
        if (!release_gap(begin,left)) return GetLastError();
        if (!release_gap(at+bytes,right)) {
            DWORD err=GetLastError(); restore_gap(begin,left,FALSE); return err;
        }
        if (!xmem_wc_slot(at,bytes,NULL,0,0)) {
            DWORD err=GetLastError(); restore_gap(begin,left,FALSE); restore_gap(at+bytes,right,FALSE); return err;
        }
        restore_gap(begin,end-begin,left || right);
        for (unsigned j=pos+1;j<n;j++) a->extent[j-1]=a->extent[j];
        a->extent[n-1]=(ScatterExtent){0};
        --a->tile_run->views; --a->views;
        if (!a->views) a->tile_run=NULL;
        return ERROR_SUCCESS;
    }
    if (pos<n) end=a->extent[pos].at;
    if (n==ARRAYSIZE(a->extent) || at<begin || at+bytes>end)
        return ERROR_INVALID_ADDRESS;
    PageRun *r=runs;
    while (r && (!r->tile[0] || id<r->first || (id-r->first)/16>=r->count)) r=r->next;
    if (!r || (id-r->first)%16) return ERROR_INVALID_PARAMETER;
    SIZE_T off=(id-r->first)/16*XMEM_PAGE;
    unsigned part=0;
    while (off>=tile_bytes(part)) { off-=tile_bytes(part); ++part; }
    if (bytes>tile_bytes(part)-off) return ERROR_NOT_SUPPORTED;
    if (n && (a->tile_run!=r || a->scatter_part!=part)) return ERROR_NOT_SUPPORTED;
    for (unsigned j=0;j<n;j++) {
        ScatterExtent v=a->extent[j];
        if (off<v.offset+v.bytes && v.offset<off+bytes) return ERROR_NOT_SUPPORTED;
    }
    SIZE_T left=at-begin,right=end-(at+bytes);
    BOOL split=left || right;
    if (!release_gap(begin,end-begin)) return GetLastError();
    if (split && !VirtualFree(at,bytes,MEM_RELEASE|MEM_PRESERVE_PLACEHOLDER)) {
        DWORD err=GetLastError(); restore_gap(begin,end-begin,FALSE); return err;
    }
    if (left && !occupy_descriptor(begin,left)) {
        DWORD err=GetLastError(); restore_gap(begin,end-begin,split); return err;
    }
    if (right && !occupy_descriptor(at+bytes,right)) {
        DWORD err=GetLastError(); if (!release_gap(begin,left)) RaiseFailFastException(NULL,NULL,0);
        restore_gap(begin,end-begin,split); return err;
    }
    if (!xmem_wc_slot(at,bytes,r->tile[part],off,0x2004)) {
        DWORD err=GetLastError();
        if (!release_gap(begin,left) || !release_gap(at+bytes,right)) RaiseFailFastException(NULL,NULL,0);
        restore_gap(begin,end-begin,split); return err;
    }
    a->tile_run=r; ++r->views; ++a->views;
    for (unsigned j=n;j>pos;j--) a->extent[j]=a->extent[j-1];
    a->extent[pos]=(ScatterExtent){at,bytes,off}; a->scatter_part=part;
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
