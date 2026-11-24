/* The title gets its own identity; only the host resource owns the pool reservation. */
struct row_buffer { IUnknown iface; LONG refs; UINT bytes; ID3D12Resource *host; struct xbox_device *owner; struct row_buffer *next; };
static ID3D12Resource *row_host_locked(struct xbox_device *d,IUnknown *iface,UINT bytes)
{
    for(struct row_buffer *r=d->rows;r;r=r->next) if(&r->iface==iface && r->bytes==bytes)return r->host;
    return NULL;
}
static ULONG WINAPI row_addref(IUnknown *iface)
{
    return InterlockedIncrement(&CONTAINING_RECORD(iface,struct row_buffer,iface)->refs);
}
static ULONG WINAPI row_release(IUnknown *iface)
{
    struct row_buffer *r=CONTAINING_RECORD(iface,struct row_buffer,iface);
    struct xbox_device *d=r->owner;AcquireSRWLockExclusive(&d->descriptor_lock);
    ULONG refs=InterlockedDecrement(&r->refs);
    if(!refs){struct row_buffer **p=&d->rows;while(*p!=r)p=&(*p)->next;*p=r->next;}
    ReleaseSRWLockExclusive(&d->descriptor_lock);
    if (!refs){ ID3D12Resource_Release(r->host); HeapFree(GetProcessHeap(),0,r);IUnknown_Release(&d->iface); }
    return refs;
}
static HRESULT WINAPI row_query(IUnknown *iface,REFIID iid,void **out)
{
    if (!out) return E_POINTER;
    *out=NULL;
    if (!iid || (!IsEqualIID(iid,&IID_IUnknown) && !IsEqualIID(iid,&IID_ID3D12Resource))) return E_NOINTERFACE;
    row_addref(iface); *out=iface; return S_OK;
}
static const struct { IUnknownVtbl unknown; xs_stop_fn rest[64]; } row_vtbl={
    {row_query,row_addref,row_release},{[0 ... 63]=xs_stop}};
static HRESULT create_row_buffer(struct xbox_device *d,UINT64 va,UINT bytes,UINT state,const void *clear,REFIID iid,void **out)
{
    if (!out) return E_POINTER;
    *out=NULL; IID id;
    if (!copy_in(&id,iid,sizeof id)) return E_POINTER;
    if (!IsEqualIID(&id,&IID_ID3D12Resource)) return E_NOINTERFACE;
    BOOL atlas=bytes==0x400000 || bytes==0x100000;
    if (!va || state!=(atlas?6u:UINT_MAX) || clear) return E_INVALIDARG;
    struct row_buffer *r=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof *r);
    if (!r) return E_OUTOFMEMORY;
    const char *name=atlas?(bytes==0x400000?"ScarlettAtlas2kV1":"ScarlettAtlas1kV1"):bytes==16?"ScarlettRow16V1":"ScarlettRowBufferV1";
    HRESULT hr=open_wc_buffer(d,va,bytes,name,&r->host,NULL,NULL,NULL);
    if (FAILED(hr)){HeapFree(GetProcessHeap(),0,r);return hr;}
    r->iface.lpVtbl=&row_vtbl.unknown; r->refs=1;r->bytes=bytes;r->owner=d;IUnknown_AddRef(&d->iface);
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
