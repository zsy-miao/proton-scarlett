#include "xmem/display_backing.h"

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
    IUnknown iface;
    LONG refs;
    struct xbox_device *owner;
    ID3D12Resource *host;
    BOOL msaa;
    void *slice;
    void (WINAPI *xfree)(void *,UINT64);
    struct color_resource *next;
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
        if (r->xfree) r->xfree(r->slice,XS_DISPLAY_ATTR); /* hook runs now, irrespective of host refs */
        ID3D12Resource_Release(r->host);
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
static const struct {
    IUnknownVtbl unknown;
    xs_stop_fn unsupported[64];
} color_vtbl={{color_query,color_addref,color_release},{[0 ... 63]=xs_stop}};
