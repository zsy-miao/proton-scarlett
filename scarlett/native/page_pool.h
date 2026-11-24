/* RegisterPagePoolX: the registration owns the source, independent of the guest VA. */
#include "xmem/tile_sections.h"
struct page_pool {
    struct page_pool *next;
    UINT64 base;
    HANDLE identity; /* Private object identity, not a backing handle. */
    struct tile_sections sections;
    ID3D12Heap *heaps[6];
};

static void pool_destroy(struct page_pool *p)
{
    for (unsigned i=0;i<ARRAYSIZE(p->heaps);++i)
        if (p->heaps[i]) ID3D12Heap_Release(p->heaps[i]);
    tile_sections_close(&p->sections);
    if (p->identity) {
        SetHandleInformation(p->identity,HANDLE_FLAG_PROTECT_FROM_CLOSE,0);
        CloseHandle(p->identity);
    }
    HeapFree(GetProcessHeap(),0,p);
}

static HRESULT WINAPI register_page_pool(IUnknown *iface,UINT64 base,UINT count,HANDLE *out)
{
    struct xbox_device *d=CONTAINING_RECORD(iface,struct xbox_device,iface);
    HANDLE handle=NULL;
    if (!copy_in(out,&handle,sizeof handle)) return E_POINTER;
    if (!base || (base&65535) || count!=TILE_PAGES) return E_INVALIDARG;
    HMODULE xm;
    if (!GetModuleHandleExW(0,L"xmem.dll",&xm)) return E_NOTIMPL;
    capture_tiles_fn capture=(void *)GetProcAddress(xm,"ScarlettCaptureTileSectionsV1");
    HRESULT (WINAPI *open)(ID3D12Device *,SIZE_T,HANDLE,HANDLE *,ID3D12Heap **)=
        (void *)GetProcAddress(GetModuleHandleW(L"d3d12core.dll"),"ScarlettWbHeapV1");
    struct page_pool *p=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof *p);
    HRESULT hr=!p ? E_OUTOFMEMORY : !capture || !open ? E_NOTIMPL : capture((void *)base,count,&p->sections);
    FreeLibrary(xm);
    if (!p) return hr;
    for (unsigned i=0;SUCCEEDED(hr) && i<ARRAYSIZE(p->heaps);++i)
        hr=open(d->host,tile_bytes(i),p->sections.part[i],NULL,&p->heaps[i]);
    if (SUCCEEDED(hr) && !(p->identity=CreateEventW(NULL,FALSE,FALSE,NULL))) hr=HRESULT_FROM_WIN32(GetLastError());
    if (SUCCEEDED(hr) && !SetHandleInformation(p->identity,HANDLE_FLAG_PROTECT_FROM_CLOSE,HANDLE_FLAG_PROTECT_FROM_CLOSE))
        hr=HRESULT_FROM_WIN32(GetLastError());
    if (SUCCEEDED(hr)) {
        AcquireSRWLockExclusive(&d->descriptor_lock);
        for (struct page_pool *q=d->page_pools;q;q=q->next)
            if (q->base==base) { hr=HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS); break; }
        if (SUCCEEDED(hr) && !copy_in(out,&p->identity,sizeof p->identity)) hr=E_POINTER;
        if (SUCCEEDED(hr)) { p->base=base; p->next=d->page_pools; d->page_pools=p; }
        ReleaseSRWLockExclusive(&d->descriptor_lock);
    }
    if (FAILED(hr)) pool_destroy(p);
    return hr;
}

/* Internal ownership operation. The guest UnregisterPagePoolX slot is unverified. */
static inline HRESULT pool_retire(struct xbox_device *d,HANDLE identity)
{
    struct page_pool *p=NULL;
    AcquireSRWLockExclusive(&d->descriptor_lock);
    for (struct page_pool **link=&d->page_pools;*link;link=&(*link)->next) {
        if ((*link)->identity!=identity) continue;
        p=*link; *link=p->next;
        break;
    }
    ReleaseSRWLockExclusive(&d->descriptor_lock);
    if (!p) return E_INVALIDARG;
    pool_destroy(p);
    return S_OK;
}

/* Queue-side lookup uses the registered base/count, never reimports a guest VA.
 * Caller owns the returned heap reference through its last GPU use. */
static inline HRESULT pool_resolve(struct xbox_device *d,UINT64 base,UINT count,UINT page,
        ID3D12Heap **heap,UINT64 *offset)
{
    *heap=NULL; *offset=0;
    if (count!=TILE_PAGES || page>=count) return E_INVALIDARG;
    UINT i=page/(1u<<14);
    if (i>=5) i=5; /* The last chunk is larger than 1 GiB. */
    AcquireSRWLockShared(&d->descriptor_lock);
    for (struct page_pool *p=d->page_pools;p;p=p->next) if (p->base==base) {
        *heap=p->heaps[i]; ID3D12Heap_AddRef(*heap);
        *offset=((UINT64)page<<16)-((UINT64)i<<30);
        break;
    }
    ReleaseSRWLockShared(&d->descriptor_lock);
    return *heap ? S_OK : E_INVALIDARG;
}
