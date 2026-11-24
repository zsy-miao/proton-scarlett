#include "d3d12_device.c"

/* Fixture only: borrow the host object while the successful guest PSO is live. */
ID3D12PipelineState *clear_test_host(IUnknown *i)
{
    return i && i->lpVtbl==&clear_methods.unknown ? CONTAINING_RECORD(i,struct bvh_pipeline,iface)->host : NULL;
}

/* Fixture-only: an ordinary offscreen texture stands in for the factory-owned
 * color texture. Exercise the production writer/tag/heap without a primary. */
HRESULT clear_test_write_rtv(IUnknown *device, ID3D12Resource *texture, SIZE_T slot)
{
    struct xbox_device *d=CONTAINING_RECORD(device,struct xbox_device,iface);
    ID3D12Device *hd=NULL;
    if (FAILED(ID3D12Resource_GetDevice(texture,&IID_ID3D12Device,(void **)&hd))) return E_INVALIDARG;
    BOOL match=hd==d->host;
    ID3D12Device_Release(hd);
    if (!match) return E_INVALIDARG;
    struct color_resource view={.owner=d,.host=texture}; /* Single-threaded, not returned to guest. */
    view.next=d->colors; d->colors=&view;
    HRESULT hr=xs_write_view(device,RTV,&view.iface,(DWORD[4]){28,4,0,0},slot);
    d->colors=view.next;
    return hr;
}

/* Inputs are live, retained adapter-side capabilities. No guest-supplied
 * pointer is dereferenced as COM. Read generation, tag and identity together. */
HRESULT clear_test_resolve(IUnknown *device,IUnknown *pinned_pso,IUnknown *pinned_root,
        IUnknown *heap,SIZE_T slot,UINT64 serial,ID3D12Resource *expected_rt,
        IUnknown *generation,ID3D12Resource *expected_src,ID3D12Device *list_device)
{
    struct xbox_device *d=CONTAINING_RECORD(device,struct xbox_device,iface);
    if (d->host!=list_device || pinned_pso->lpVtbl!=&clear_methods.unknown) return E_INVALIDARG;
    struct bvh_pipeline *p=CONTAINING_RECORD(pinned_pso,struct bvh_pipeline,iface);
    if (p->owner!=d || p->root!=xs_bvh_root_host(device,pinned_root)) return E_INVALIDARG;
    HRESULT hr=E_INVALIDARG;
    AcquireSRWLockShared(&d->descriptor_lock);
    for (struct desc_heap *h=d->rtv_heaps;h;h=h->next) if (&h->iface==heap) {
        D3D12_CPU_DESCRIPTOR_HANDLE cpu; UINT64 tag[2];
        if (FAILED(xs_descriptor_slot(heap,slot,&cpu,NULL)) || !copy_in(tag,(void *)slot,sizeof tag)) break;
        struct descriptor_record *r=h->records+(slot-h->start)/h->stride;
        if (!serial || tag[0]!=serial || r->serial!=serial || tag[1]!=(0x5853444553430000ull|RTV) ||
                r->kind!=RTV || r->resource!=expected_rt) break;
        for (struct xs_buffer_binding *b=d->buffers;b;b=b->next)
            if (b->identity==generation && b->host==expected_src) {hr=S_OK;break;}
        break;
    }
    ReleaseSRWLockShared(&d->descriptor_lock);
    return hr;
}
