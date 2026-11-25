#include "d3d12_device.c"
#ifdef TEST_CARRIED_REPLACEMENT
static IUnknown *register_again(IUnknown *owner,UINT_PTR base)
{
    D3D12_RESOURCE_DESC rd={.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER,.Width=PLACED_WIDTH,.Height=1,
        .DepthOrArraySize=1,.MipLevels=1,.SampleDesc.Count=1,.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR,.Flags=0x20004};
    IUnknown *p=NULL;
    placed_create(CONTAINING_RECORD(owner,struct xbox_device,iface),base,&rd,UINT_MAX,NULL,&IID_ID3D12Resource,(void**)&p);
    return p;
}
IUnknown *test_duplicate_registration(IUnknown *owner,UINT_PTR base){return register_again(owner,base);}
BOOL test_reused_registration(IUnknown *owner,ID3D12Device *d,ID3D12GraphicsCommandList7 *l,
        struct xs_clear_source *c,struct xs_clear_source *f,struct xs_clear_source *r)
{
    IUnknown *next=register_again(owner,c->span.arena.base);
    if(!next)return FALSE;
    struct placed_backing *p=CONTAINING_RECORD(next,struct placed_identity,iface)->private;
    struct xs_clear_source none={0};
    BOOL ok=next!=c->placed && p->capture!=c->capture && p->matches(p->capture,(void*)p->guest_va) &&
        FAILED(xs_copy_carried_replacement(owner,d,l,r->span.va,c,f,&none)) && !none.buffer;
    IUnknown_Release(next);return ok;
}
#endif
#ifdef TEST_IM96_USED
#include "../tests/im96/used-proof.h"
/* Internal fixture callers hold both heaps, image and allocation identities. */
BOOL test_used_current(const struct used_proof *u)
{
    if(!u->owner || u->owner->lpVtbl!=&bootstrap_vtbl.unknown || !u->cb_allocation ||
       !u->raw_allocation || !u->cb_generation || !u->raw_serial || !u->tex_serial)return FALSE;
    struct xbox_device *d=CONTAINING_RECORD(u->owner,struct xbox_device,iface);
    struct desc_heap *src=NULL,*vis=NULL;
    BOOL ok=FALSE;
    AcquireSRWLockShared(&d->descriptor_lock);
    for(struct desc_heap*h=d->heaps;h;h=h->next){
        if(&h->iface==u->src_heap && !h->visible)src=h;
        if(&h->iface==u->visible && h->visible)vis=h;
    }
    if(!src || !vis || src->stride!=32 || vis->stride!=32 || src->count<=690 || vis->count<=98 ||
       u->raw_slot!=src->start+690*32 || u->cb_slot!=src->start+18*32 || u->tex_slot!=vis->start+98*32 ||
       u->table<vis->start || (u->table-vis->start)%32 || (u->table-vis->start)/32+2>=vis->count)goto done;
    struct descriptor_record *a=src->records+690,*b=vis->records+98,*c=src->records+18;
    if(a->serial!=u->raw_serial || a->kind!=PLACED_RAW32_VIEW || a->placed!=u->raw_allocation ||
       a->offset!=0x3de20 || a->elements!=32 || a->stride || b->serial!=u->tex_serial || b->kind!=SRV ||
       !u->image || b->resource!=u->image || memcmp(&b->captured_srv,&u->view,sizeof u->view) ||
       c->serial || c->kind || c->resource || c->placed)goto done;
    const UINT64 expected[3][4]={{a->serial,0x5853444553430500ULL,0,0},{b->serial,0x5853444553430090ULL,0,0},
            {0x0010000000000000ULL+u->base+0x3dc40,0x0002403c0000001eULL,0,0}};
    const SIZE_T slots[]={u->raw_slot,u->tex_slot,u->cb_slot};
    for(UINT i=0;i<3;++i){
        struct descriptor_record *dst=vis->records+(u->table-vis->start)/32+i;
        UINT64 wire[4],copied[4];
        if(dst->kind || dst->serial || dst->resource || dst->placed || !copy_in(wire,(void*)slots[i],32) ||
           !copy_in(copied,(void*)(u->table+32*i),32) || memcmp(wire,expected[i],32) || memcmp(copied,wire,32))goto done;
    }
    UINT matches=0;
    for(struct placed_backing *p=d->placed;p;p=p->next){
        if(p->guest_va!=u->base || !p->matches(p->capture,(void*)u->base))continue;
        if(++matches!=1 || &p->title->iface!=u->raw_allocation || &p->title->iface!=u->cb_allocation ||
           (UINT_PTR)p->capture!=u->cb_generation)goto done;
    }
    ok=matches==1;
done:ReleaseSRWLockShared(&d->descriptor_lock);return ok;
}
HRESULT test_used_copy(const struct used_proof *u,BOOL camera,ID3D12GraphicsCommandList *l,ID3D12Resource *dst)
{
    if(!test_used_current(u))return E_INVALIDARG;
    IUnknown *id=camera?u->cb_allocation:u->raw_allocation;
    struct placed_backing*p=CONTAINING_RECORD(id,struct placed_identity,iface)->private;
    struct XsPlacedCapture *hold=NULL;
    HRESULT hr=p->copy(p->capture,camera?0x3dc40:0x3de20,camera?480:64,l,dst,0,&hold);
    if(SUCCEEDED(hr))p->drop(hold); /* caller pins the two proved source ranges through its fence */
    return hr;
}
UINT64 test_used_generation(IUnknown *pinned)
{
    struct placed_backing*p=CONTAINING_RECORD(pinned,struct placed_identity,iface)->private;
    return (UINT_PTR)p->capture; /* retained opaque generation capability; matches() checks live XMem mapping */
}
/* Fixture calls this at producer allocation, before encoding a CBV. The slot's
 * unregistered bytes alone are never used as evidence of allocation lifetime. */
IUnknown *test_used_cb_pin(IUnknown *owner,SIZE_T producer,UINT bytes)
{
    if(!owner || owner->lpVtbl!=&bootstrap_vtbl.unknown || bytes!=480)return NULL;
    struct xbox_device*d=CONTAINING_RECORD(owner,struct xbox_device,iface);
    IUnknown *out=NULL;
    AcquireSRWLockShared(&d->descriptor_lock);
    for(struct placed_backing*p=d->placed;p;p=p->next)if(producer-p->guest_va==0x3dc40 && p->matches(p->capture,(void*)p->guest_va)){
        if(out){out=NULL;break;}
        out=&p->title->iface;
    }
    if(out)IUnknown_AddRef(out);
    ReleaseSRWLockShared(&d->descriptor_lock);return out;
}
#endif

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
