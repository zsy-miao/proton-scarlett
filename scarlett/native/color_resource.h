#include "xmem/display_backing.h"
#include "xmem/marked_section.h"

#define XS_DISPLAY_ATTR 0x30808000ull
struct color_lease {
    IUnknown iface;
    LONG refs;
    HMODULE xm;
    void *base;
    __typeof__(&ScarlettReleaseDisplayBackingV1) unpin;
};
static ULONG WINAPI lease_addref(IUnknown *i)
{
    return InterlockedIncrement(&CONTAINING_RECORD(i,struct color_lease,iface)->refs);
}
static ULONG WINAPI lease_release(IUnknown *i)
{
    struct color_lease *l=CONTAINING_RECORD(i,struct color_lease,iface);
    ULONG refs=InterlockedDecrement(&l->refs);
    if (!refs) {
        if (l->base) l->unpin(l->base);
        if (l->xm) FreeLibrary(l->xm);
        HeapFree(GetProcessHeap(),0,l);
    }
    return refs;
}
static HRESULT WINAPI lease_query(IUnknown *i,REFIID iid,void **out)
{
    if (!out) return E_POINTER;
    *out=NULL;
    if (!iid || !IsEqualIID(iid,&IID_IUnknown)) return E_NOINTERFACE;
    lease_addref(i); *out=i; return S_OK;
}
static const IUnknownVtbl lease_vtbl={lease_query,lease_addref,lease_release};

/* Device-owned weak registry. Each wrapper owns one device and one offscreen
 * texture reference; descriptor records independently retain their texture. */
struct color_resource {
#if defined(TEST_SAME_BATCH_RELEASE) || defined(SCARLETT_PAIR_SUBMIT)
    UINT64 writer_epoch;
#endif
#ifdef SCARLETT_PAIR_SUBMIT
    UINT64 common_epoch,common_serial;
    SIZE_T common_slot;
#endif
    IUnknown iface;
    LONG refs;
    struct xbox_device *owner;
    ID3D12Resource *host;
#ifdef SCARLETT_EXPERIMENTAL_BRIDGE
    ID3D12CommandQueue *writer;
    BOOL multi_writer,claimed,flipping;
#endif
    BOOL msaa,depth,depth4k,tiled_r32,tiled_r11,small_rt;
    DXGI_FORMAT single_format,small_tile_format;
    void *slice;
    void (WINAPI *xfree)(void *,UINT64);
    struct color_resource *next;
    /* Creation-time generation for the two qualified offscreen 4K targets. */
    ID3D12Heap *pair_heap;
    HANDLE pair_section;
    HANDLE tile_section;
    ID3D12Heap *tile_heap;
    UINT_PTR tile_va,tile_offset;
#ifdef SCARLETT_PAIR_SUBMIT
    ID3D12Fence *sr_end;
    SIZE_T sr_slot;
    UINT64 sr_serial;
#endif
};
static ULONG WINAPI color_addref(IUnknown *i)
{
    return InterlockedIncrement(&CONTAINING_RECORD(i,struct color_resource,iface)->refs);
}
static ULONG WINAPI color_release(IUnknown *i)
{
    struct color_resource *r=CONTAINING_RECORD(i,struct color_resource,iface);
    struct xbox_device *d=r->owner;
    AcquireSRWLockExclusive(&d->descriptor_lock);
    ULONG refs=InterlockedDecrement(&r->refs);
    if (!refs) {
        struct color_resource **p=&d->colors;
        while (*p!=r) p=&(*p)->next;
        *p=r->next;
    }
    ReleaseSRWLockExclusive(&d->descriptor_lock);
    if (!refs) {
#ifdef SCARLETT_EXPERIMENTAL_BRIDGE
#ifdef SCARLETT_PAIR_SUBMIT
        if(r->sr_end)ID3D12Fence_Release(r->sr_end);
#endif
        if(r->writer) ID3D12CommandQueue_Release(r->writer);
#endif
        if (r->xfree) r->xfree(r->slice,XS_DISPLAY_ATTR); /* hook runs now, irrespective of host refs */
        ID3D12Resource_Release(r->host);
        if (r->pair_heap) ID3D12Heap_Release(r->pair_heap);
        if (r->pair_section) CloseHandle(r->pair_section);
        if (r->tile_heap) ID3D12Heap_Release(r->tile_heap);
        if (r->tile_section) CloseHandle(r->tile_section);
        HeapFree(GetProcessHeap(),0,r);
        IUnknown_Release(&d->iface);
    }
    return refs;
}
static HRESULT WINAPI color_query(IUnknown *i,REFIID iid,void **out)
{
    if (!out) return E_POINTER;
    *out=NULL;
    if (!iid) return E_INVALIDARG;
    if (!IsEqualIID(iid,&IID_IUnknown) && !IsEqualIID(iid,&IID_ID3D12Resource)) return E_NOINTERFACE;
    color_addref(i); *out=i;
    return S_OK;
}
/* Validate the current VA generation against the heap or section passed to 0032. */
#ifdef SCARLETT_STAGED_PLACED
static HRESULT native_pool_ref(ID3D12Device*,const void*,SIZE_T,ID3D12Heap**,ID3D12Resource**,UINT*);
#endif
static UINT tile_span(const struct color_resource *r)
{
    if(r->small_tile_format)return r->small_tile_format==DXGI_FORMAT_R32G32_FLOAT?0x1cc00:0xf000;
    return r->tiled_r11?0x4b1400:0x384000;
}
static BOOL tile_mapping(const struct color_resource *r)
{
#ifdef SCARLETT_STAGED_PLACED
    if(r->tile_heap){
        ID3D12Heap *heap=NULL;UINT off=0;
        HRESULT hr=native_pool_ref(r->owner->host,(void*)r->tile_va,tile_span(r),&heap,NULL,&off);
        BOOL same=hr==S_OK && heap==r->tile_heap && off==r->tile_offset;
        if(heap)ID3D12Heap_Release(heap);
        return same;
    }
#endif
    struct scarlett_section_info i;
    BOOL (WINAPI *same)(HANDLE,HANDLE)=(void*)GetProcAddress(GetModuleHandleW(L"kernelbase.dll"),"CompareObjectHandles");
    if (!r->tile_section || !same || FAILED(scarlett_query_section(0x53430005,(void*)r->tile_va,tile_span(r),&i))) return FALSE;
    BOOL ok=i.section && i.view_size==0x89700000 && i.offset==r->tile_offset &&
            r->tile_va==(UINT_PTR)i.base+i.offset && same(i.section,r->tile_section);
    if (i.section) CloseHandle(i.section);
    return ok;
}
static void *WINAPI tile_guest_desc(IUnknown *id,void *out)
{
    const struct color_resource *r=CONTAINING_RECORD(id,struct color_resource,iface);
    const DWORD wire[]={3,0,0,0,1280,0,720,0x10001,42,1,0,0x102,0x300004};
    SIZE_T n;
    if (!r->tiled_r32 || (UINT_PTR)__builtin_return_address(0)-(UINT_PTR)GetModuleHandleW(NULL)!=0x501f80e || !tile_mapping(r) ||
            !WriteProcessMemory(GetCurrentProcess(),out,wire,sizeof wire,&n) || n!=sizeof wire) xs_stop();
    return out;
}
static UINT64 WINAPI tile_guest_va(IUnknown *id)
{
    const struct color_resource *r=CONTAINING_RECORD(id,struct color_resource,iface);
    if (!r->tiled_r32 || (UINT_PTR)__builtin_return_address(0)-(UINT_PTR)GetModuleHandleW(NULL)!=0x4ecc936 || !tile_mapping(r)) xs_stop();
    return r->tile_va;
}
static const struct {
    IUnknownVtbl unknown;
    xs_stop_fn pre[7];
    __typeof__(&tile_guest_desc) desc;
    __typeof__(&tile_guest_va) va;
    xs_stop_fn rest[55];
} color_vtbl={{color_query,color_addref,color_release},{[0 ... 6]=xs_stop},tile_guest_desc,tile_guest_va,{[0 ... 54]=xs_stop}};
