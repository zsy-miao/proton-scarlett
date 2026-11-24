#include "d3d12_device.c"

/* Fixture-only owner/kind lookup; success pins the guest wrapper. */
ID3D12RootSignature *test_retain_im96(IUnknown *d,IUnknown *r)
{
    return retain_graphics_kind(d,r,ROOT_IM96);
}

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

/* Committed stand-ins, as in test-clear-device.c, with the real registry,
 * writer, final release and weak lookup. Not a placed-allocation test. */
IUnknown *test_texture(IUnknown *owner,ID3D12Resource *image,BOOL depth)
{
    struct xbox_device *d=CONTAINING_RECORD(owner,struct xbox_device,iface);
    struct color_resource *c=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof *c);
    if(!c)return NULL;
    c->iface.lpVtbl=&color_vtbl.unknown;c->owner=d;c->refs=1;c->host=image;
    c->depth=c->depth4k=depth;c->small_rt=!depth;
    c->single_format=depth?DXGI_FORMAT_R32G8X24_TYPELESS:DXGI_FORMAT_R8G8B8A8_UNORM;
    IUnknown_AddRef(owner);ID3D12Resource_AddRef(image);
    AcquireSRWLockExclusive(&d->descriptor_lock);
    c->next=d->colors;d->colors=c;
    ReleaseSRWLockExclusive(&d->descriptor_lock);return &c->iface;
}
#ifdef TEST_DISTINCT_PRIMARY
/* A registered stand-in with its own RTV/storage. No display allocator or flip in this fixture. */
IUnknown *test_primary_image(IUnknown *owner,ID3D12Device *hd)
{
    D3D12_RESOURCE_DESC rd={.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D,.Width=16,.Height=16,
        .DepthOrArraySize=1,.MipLevels=1,.Format=DXGI_FORMAT_R8G8B8A8_UNORM,.SampleDesc.Count=1,
        .Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET|D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS};
    D3D12_HEAP_PROPERTIES hp={.Type=D3D12_HEAP_TYPE_DEFAULT,.CreationNodeMask=1,.VisibleNodeMask=1};
    ID3D12Resource *image=NULL;
    if(FAILED(ID3D12Device_CreateCommittedResource(hd,&hp,0,&rd,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,NULL,
        &IID_ID3D12Resource,(void**)&image)))return NULL;
    IUnknown *id=test_texture(owner,image,FALSE);ID3D12Resource_Release(image);
    if(id)CONTAINING_RECORD(id,struct color_resource,iface)->slice=id;
    return id;
}
#endif
