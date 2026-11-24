/* Device-owned weak registry. Each wrapper owns one device and one offscreen
 * texture reference; descriptor records independently retain their texture. */
struct color_resource {
    IUnknown iface;
    LONG refs;
    struct xbox_device *owner;
    ID3D12Resource *host;
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
