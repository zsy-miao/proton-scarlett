/* The title gets its own identity; only the host resource owns the pool reservation. */
#include "xmem/native_pool.h"
struct row_buffer { IUnknown iface; LONG refs; UINT bytes; void *va,*mapped; HANDLE section; UINT64 generation; UINT_PTR off; SRWLOCK map_lock; ID3D12Resource *host; struct xbox_device *owner; struct row_buffer *next; };
static ID3D12Resource *row_host_locked(struct xbox_device *d,IUnknown *iface,UINT bytes)
{
    for(struct row_buffer *r=d->rows;r;r=r->next) if(&r->iface==iface && r->bytes==bytes)return r->host;
    return NULL;
}
static ULONG WINAPI row_addref(IUnknown *iface)
{
    return InterlockedIncrement(&CONTAINING_RECORD(iface,struct row_buffer,iface)->refs);
}
static void atlas_end_map(struct row_buffer *r)
{
    MemoryBarrier();
    if (r->section) VirtualFree((BYTE*)r->mapped-r->off,0,MEM_RELEASE);
    else if (native_pool_ctl(0x53430107,r->va,&r->generation,sizeof r->generation)<0) xs_stop();
    r->mapped=NULL;
}
static ULONG WINAPI row_release(IUnknown *iface)
{
    struct row_buffer *r=CONTAINING_RECORD(iface,struct row_buffer,iface);
    struct xbox_device *d=r->owner;AcquireSRWLockExclusive(&d->descriptor_lock);
    ULONG refs=InterlockedDecrement(&r->refs);
    if(!refs){struct row_buffer **p=&d->rows;while(*p!=r)p=&(*p)->next;*p=r->next;}
    ReleaseSRWLockExclusive(&d->descriptor_lock);
    if (!refs){ if(r->mapped)atlas_end_map(r); if(r->section)CloseHandle(r->section); ID3D12Resource_Release(r->host); HeapFree(GetProcessHeap(),0,r);IUnknown_Release(&d->iface); }
    return refs;
}
static HRESULT WINAPI row_query(IUnknown *iface,REFIID iid,void **out)
{
    if (!out) return E_POINTER;
    *out=NULL;
    if (!iid || (!IsEqualIID(iid,&IID_IUnknown) && !IsEqualIID(iid,&IID_ID3D12Resource))) return E_NOINTERFACE;
    row_addref(iface); *out=iface; return S_OK;
}
static BOOL seven_row_live(const struct row_buffer *r)
{
    if(r->bytes!=0xd900 || !r->section || !r->generation)return FALSE;
    BOOL (WINAPI *same)(HANDLE,HANDLE)=(void*)GetProcAddress(GetModuleHandleW(L"kernelbase.dll"),"CompareObjectHandles");
    struct scarlett_wc_view_info view;
    if(!same || scarlett_acquire_wc_view(r->va,r->bytes,&view)!=S_OK)return 0;
    struct scarlett_section_info live=view.info;
    BOOL match=view.generation==r->generation && live.section && live.view_size==WC_POOL_BYTES && live.offset==r->off &&
        live.offset<=live.view_size-r->bytes && (UINT_PTR)r->va>=live.offset &&
        (UINT_PTR)live.base==(UINT_PTR)r->va-live.offset && same(live.section,r->section);
    if(live.section)CloseHandle(live.section);
    return match;
}
static UINT64 seven_guest_va(IUnknown *id,UINT_PTR caller)
{
    const struct row_buffer *r=CONTAINING_RECORD(id,struct row_buffer,iface);
    return caller==0x4ecc936 && seven_row_live(r)?(UINT_PTR)r->va:0;
}
static HRESULT write_seven_srv(struct xbox_device *d,UINT_PTR va,SIZE_T dest)
{
    struct descriptor_record old={0};
    HRESULT hr=E_INVALIDARG;
    const struct row_buffer *row=NULL;
    AcquireSRWLockExclusive(&d->descriptor_lock);
    for (struct row_buffer *r=d->rows;r;r=r->next) if ((UINT_PTR)r->va==va && seven_row_live(r)) {
        if (row) goto done;
        row=r;
    }
    if (row) hr=store_address_view(d,SEVEN_R32_VIEW,dest,row->host,0,0,0,TRUE,&old);
done:
    ReleaseSRWLockExclusive(&d->descriptor_lock);drop_record(old);return hr;
}
static UINT64 WINAPI row_va(IUnknown *id)
{
    UINT64 va=seven_guest_va(id,(UINT_PTR)__builtin_return_address(0)-(UINT_PTR)GetModuleHandleW(NULL));
    if(!va)xs_stop();
    return va;
}
static const struct { IUnknownVtbl unknown; xs_stop_fn pre[8]; __typeof__(&row_va) va; xs_stop_fn rest[55]; } row_vtbl={
    {row_query,row_addref,row_release},{[0 ... 7]=xs_stop},row_va,{[0 ... 54]=xs_stop}};
_Static_assert(offsetof(__typeof__(row_vtbl),va)==0x58,"row address ABI");
struct atlas_slots { IUnknownVtbl unknown; xs_stop_fn gap[5];
    HRESULT (WINAPI *map)(IUnknown*,UINT,const D3D12_RANGE*,void**);
    void (WINAPI *unmap)(IUnknown*,UINT,const D3D12_RANGE*); xs_stop_fn rest[57]; };
static const struct atlas_slots atlas_vtbl;
static HRESULT WINAPI atlas_map(IUnknown *iface,UINT sub,const D3D12_RANGE *range,void **out)
{
    struct row_buffer *r=CONTAINING_RECORD(iface,struct row_buffer,iface);
    D3D12_RANGE read; SIZE_T n; void *zero=NULL;
    if (!out || !WriteProcessMemory(GetCurrentProcess(),out,&zero,sizeof zero,&n) || n!=sizeof zero) return E_POINTER;
    if (!iface || iface->lpVtbl!=&atlas_vtbl.unknown || sub || !copy_in(&read,range,sizeof read) ||
        read.Begin || read.End!=r->bytes) return E_INVALIDARG;
    AcquireSRWLockExclusive(&r->map_lock);
    HRESULT hr=DXGI_ERROR_INVALID_CALL;
    if (!r->mapped && r->section){
        struct scarlett_section_info live,alias;
        BOOL (WINAPI *same)(HANDLE,HANDLE)=(void*)GetProcAddress(GetModuleHandleW(L"kernelbase.dll"),"CompareObjectHandles");
        hr=scarlett_query_section(0x53430005,r->va,r->bytes,&live);
        if (SUCCEEDED(hr)){
            hr=(live.offset==r->off && same && same(live.section,r->section))?
                scarlett_query_section_handle(0x53430004,NULL,WC_POOL_BYTES,&alias,r->section):E_INVALIDARG;
            CloseHandle(live.section);
            if (SUCCEEDED(hr))r->mapped=(BYTE*)alias.base+r->off;
        }
    }else if(!r->mapped){
        LONG st=native_pool_ctl(0x53430106,r->va,&r->generation,sizeof r->generation);
        hr=st<0?HRESULT_FROM_NT(st):S_OK;
        if(SUCCEEDED(hr))r->mapped=r->va;
    }
    if(SUCCEEDED(hr) && (!WriteProcessMemory(GetCurrentProcess(),out,&r->mapped,sizeof r->mapped,&n) || n!=sizeof r->mapped)){
        atlas_end_map(r);hr=E_POINTER;
    }
    ReleaseSRWLockExclusive(&r->map_lock);return hr;
}
static void WINAPI atlas_unmap(IUnknown *iface,UINT sub,const D3D12_RANGE *written)
{
    struct row_buffer *r=CONTAINING_RECORD(iface,struct row_buffer,iface);
    if (!iface || iface->lpVtbl!=&atlas_vtbl.unknown || sub || written)xs_stop();
    AcquireSRWLockExclusive(&r->map_lock);
    if (!r->mapped)xs_stop();
    atlas_end_map(r);ReleaseSRWLockExclusive(&r->map_lock);
}
static const struct atlas_slots atlas_vtbl={{row_query,row_addref,row_release},{[0 ... 4]=xs_stop},
    atlas_map,atlas_unmap,{[0 ... 56]=xs_stop}};
_Static_assert(offsetof(struct atlas_slots,map)==0x40 && offsetof(struct atlas_slots,unmap)==0x48,"atlas ABI");
static HRESULT create_row_buffer(struct xbox_device *d,UINT64 va,UINT bytes,UINT state,const void *clear,REFIID iid,void **out)
{
    if (!out) return E_POINTER;
    *out=NULL; IID id;
    if (!copy_in(&id,iid,sizeof id)) return E_POINTER;
    if (!IsEqualIID(&id,&IID_ID3D12Resource)) return E_NOINTERFACE;
    BOOL atlas=bytes==0x400000 || bytes==0x100000;
    BOOL seven=bytes==0xd900;
    if (!va || state!=((atlas || seven)?6u:UINT_MAX) || clear) return E_INVALIDARG;
    struct row_buffer *r=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof *r);
    if (!r) return E_OUTOFMEMORY;
    const char *name=seven?"ScarlettLinearSevenV1":atlas?(bytes==0x400000?"ScarlettAtlas2kV1":"ScarlettAtlas1kV1"):bytes==16?"ScarlettRow16V1":"ScarlettRowBufferV1";
    ID3D12Heap *heap=NULL;
    r->va=(void*)va;
    if(atlas)native_pool_ctl(0x53430103,r->va,&r->generation,sizeof r->generation);
    HRESULT hr=open_wc_buffer(d,va,bytes,name,&r->host,&heap,&r->section,(atlas || seven)?&r->off:NULL,seven?&r->generation:NULL);
    if(heap){
        UINT64 id=0;ID3D12Heap_Release(heap);
        if(!r->generation || native_pool_ctl(0x53430103,r->va,&id,sizeof id)<0 || id!=r->generation){
            ID3D12Resource_Release(r->host);hr=E_INVALIDARG;
        }
    }
    if (FAILED(hr)){HeapFree(GetProcessHeap(),0,r);return hr;}
    r->iface.lpVtbl=atlas?&atlas_vtbl.unknown:&row_vtbl.unknown; r->refs=1;r->bytes=bytes;r->owner=d;IUnknown_AddRef(&d->iface);
    AcquireSRWLockExclusive(&d->descriptor_lock);r->next=d->rows;d->rows=r;ReleaseSRWLockExclusive(&d->descriptor_lock);
    *out=&r->iface; return S_OK;
}

static HRESULT write_atlas_srv(struct xbox_device *d,IUnknown *obj,SIZE_T dest)
{
    struct descriptor_record old={0};HRESULT hr=S_FALSE;
    AcquireSRWLockExclusive(&d->descriptor_lock);
    for(struct row_buffer *r=d->rows;r;r=r->next)if(&r->iface==obj && (r->bytes==0x100000 || r->bytes==0x400000)){
        hr=store_address_view(d,r->bytes==0x100000?ATLAS1K_VIEW:ATLAS2K_VIEW,dest,r->host,0,0,0,FALSE,&old);
        break;
    }
    ReleaseSRWLockExclusive(&d->descriptor_lock);drop_record(old);return hr;
}
