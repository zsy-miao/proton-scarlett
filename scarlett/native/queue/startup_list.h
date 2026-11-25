/* Private to queue.c: only the recording and submission shapes used by the startup lists. */
#include "prefix_marker.h"
#include "system_timestamp.h"
#ifdef SCARLETT_EXPERIMENTAL_TAGGED_BATCH
enum batch_phase { B_EMPTY,B_BOUND,B_PREFIX,B_REBOUND,B_HEAD_STAMP,B_POST,B_WAIT,
    B_ACQUIRED,B_CLEARED,B_RELEASED,B_TAIL_STAMP,B_CLOSED,B_INELIGIBLE };
#define batch_step(s,to) ((s)->phase=(s)->phase==(to)-1?(to):B_INELIGIBLE)
#else
#define batch_step(s,to) ((void)0)
#endif
struct list_face { struct startup_list *state; IUnknown iface; };
struct direct_prefix {
    IUnknown iface;
    UINT64 reserved08[2],put,reserved20[2],limit;
};
_Static_assert(offsetof(struct direct_prefix,put)==0x18 && offsetof(struct direct_prefix,limit)==0x30 &&
        sizeof(struct direct_prefix)==0x38,"Direct inline capacity check");
#include "../om-views.h"
struct startup_list {
#ifdef SCARLETT_PAIR_SUBMIT
    struct pair_gen *seal;
    struct om_views first_om;
    ID3D12GraphicsCommandList7 *pre_release;
    ID3D12CommandAllocator *after_alloc;
    UINT64 alloc_generation;
    UINT64 reuse_epoch;
    struct xs_sr_ticket sr;
    ID3D12DescriptorHeap *sr_view;
    D3D12_CPU_DESCRIPTOR_HANDLE sr_cpu;
    BOOL sr_reversed;
    UINT nops,split_at;
    BYTE ops[128]; /* Guest vtable slots; overflow is ineligible. */
#endif
#ifdef XS_TEST_ORDERED_QUEUE
    BOOL single_use,consumed;
    struct startup_list *ordered_next;
    struct adapter *ordered_queue;
    IUnknown *ordered_id,*ordered_lease;
#ifdef TEST_SAME_BATCH_RELEASE
    BOOL exports_rt;
    IUnknown *requires_rt; /* The successor's ordered_lease owns this generation. */
    D3D12_TEXTURE_BARRIER release_wire;
    UINT64 writer_epoch;
#endif
#endif
    struct om_views om;
    struct xs_clear_source previous_vb,vb,source;
    /* Borrow the recording's strong heap and root references. Only root parameter 0 is admitted. */
    struct {IUnknown *heap,*root;UINT slot,cells;} table0;
    UINT prior_b27_slot; /* Same pinned heap/root as table0; history only while cells==3. */
    UINT64 table_epoch; /* Setter generation, never a descriptor-content generation. */
#ifdef SCARLETT_EXPERIMENTAL_TAGGED_BATCH
    enum batch_phase phase;
    IUnknown *sync_owner;
    struct release_receipt *offered,*deferred;
#ifdef SCARLETT_PAIR_SUBMIT
    BOOL successor_query;
#endif
    D3D12_TEXTURE_BARRIER deferred_wire; /* Guest layout/key; no host command emitted. */
#endif
    SIZE_T clear_slot;
    UINT64 clear_serial;
    ID3D12DescriptorHeap *output_heap;
    D3D12_CPU_DESCRIPTOR_HANDLE output_cpu;
    UINT64 output_serial;
    SIZE_T output_slot;
    IUnknown *primary_pin;
    void *reservation;
    LONG refs;
    ID3D12GraphicsCommandList7 *host;
    BYTE *cache_page;
    /* These pins keep the earlier #4 recording alive after its bindings cease to be active. */
    IUnknown *prior_pso,*prior_root;
    IUnknown *graphics_identity,*bound,*required_root;
    ID3D12PipelineState *pipeline; /* borrowed from bound */
    ID3D12RootSignature *graphics_root; /* borrowed from graphics_identity */
    ID3D12Device *device; /* Kept alive by host. */
    IUnknown *allocator,*pool_owner,*heaps[2];
    D3D12_COMMAND_LIST_TYPE type;
    HRESULT error;
    BOOL primary_cleared;
    BOOL primary_released; /* Guest layout 0x1f on the retained primary. */
    struct xs_attachment_pin attachments[2];
    BOOL rt_roundtripped,offscreen_sr;
    ID3D12Resource *primary; /* +490 recording owns the imported texture, not the guest identity. */
    UINT_PTR primary_key; /* Comparison only; primary_pin owns the registration. */
    struct prefix_marker *marker,*post;
    ID3D12GraphicsCommandList7 *head; /* closed segment preceding post */
    ID3D12CommandAllocator *tail_allocator;
    struct system_timestamp *stamp;
    BOOL closed,host_exposed,post_boundary,stencil_zero,blend_ones,full_bounds,full_rect,full_vp;
    SRWLOCK borrow_lock; /* Serialize a native borrow against recording deferred guest state. */
    BOOL depth_zero_pending,pending_470;
    UINT64 clear4_lanes;
    UINT guest_topology;
    BOOL clear4_drew;
    struct list_face local; /* Non-pooled lists need no inline ABI. */
};
const IID xs_sync_list_id = {0xc3827890,0x4024,0x474c,{0x98,0xcf,0x64,0x5b,0x51,0xdc,0x48,0x74}};
const IID xs_pool_list_id = {0x5b160d0f,0xac1b,0x4185,{0x8b,0xa8,0xb3,0xae,0x42,0xa5,0xa4,0x55}};

#ifdef SCARLETT_PAIR_SUBMIT
static void pair_seal(struct startup_list *,IUnknown *);
static void drop_sr(struct startup_list *s){
    if(s->sr_view)ID3D12DescriptorHeap_Release(s->sr_view);
    if(s->sr.end)ID3D12Fence_Release(s->sr.end);
    if(s->sr.rt.identity)IUnknown_Release(s->sr.rt.identity);
    if(s->sr.ds.identity)IUnknown_Release(s->sr.ds.identity);
    s->sr=(struct xs_sr_ticket){0};s->sr_view=NULL;s->sr_reversed=FALSE;
}
static void pair_unseal(struct startup_list *);
static void pair_op(struct startup_list *s,UINT slot){if(s->nops<sizeof s->ops)s->ops[s->nops++]=slot/8;else s->nops=sizeof s->ops+1;}
#else
#define pair_op(s,slot) ((void)0)
#endif
static void sl_free(struct startup_list *s)
{
    if (s->reservation) VirtualFree(s->reservation, 0, MEM_RELEASE);
    else HeapFree(GetProcessHeap(), 0, s);
}
static struct startup_list *sl(IUnknown *i) { return CONTAINING_RECORD(i, struct list_face, iface)->state; }
#ifdef SCARLETT_EXPERIMENTAL_TAGGED_BATCH
#include "deferred-release.h"
#else
#define forget_receipt(s) ((void)0)
#define offer_release(s,i) ((void)0)
#endif
static ULONG WINAPI sl_addref(IUnknown *i) { return InterlockedIncrement(&sl(i)->refs); }
static void drop_heaps(IUnknown *pair[2])
{
    for (unsigned i=0;i<2;i++) if (pair[i]) { IUnknown_Release(pair[i]); pair[i]=NULL; }
}
static void drop_attachments(struct xs_attachment_pin pair[2])
{
    for(UINT k=0;k<2;k++)if(pair[k].identity){IUnknown_Release(pair[k].identity);pair[k]=(struct xs_attachment_pin){0};}
}
static void drop_source(struct xs_clear_source *p)
{
    if(p->buffer)ID3D12Resource_Release(p->buffer);
    if(p->placed)IUnknown_Release(p->placed);
    *p=(struct xs_clear_source){0};
}
static void bind_source(ID3D12GraphicsCommandList7 *host,const struct xs_clear_source *p)
{
    ID3D12GraphicsCommandList7_SetGraphicsRootShaderResourceView(host,0,ID3D12Resource_GetGPUVirtualAddress(p->buffer));
}
static void bind_colour_vb(ID3D12GraphicsCommandList7 *host,const struct xs_clear_source *vb)
{
    D3D12_VERTEX_BUFFER_VIEW view={ID3D12Resource_GetGPUVirtualAddress(vb->buffer),4080,24};
    ID3D12GraphicsCommandList7_IASetVertexBuffers(host,0,1,&view);
}
static void drop_split(struct startup_list *s)
{
    drop_marker(s->post); s->post=NULL;
    s->post_boundary=FALSE;
    if (s->head) { ID3D12GraphicsCommandList7_Release(s->head);s->head=NULL; }
    if (s->tail_allocator) { ID3D12CommandAllocator_Release(s->tail_allocator);s->tail_allocator=NULL; }
}
static ULONG WINAPI sl_release(IUnknown *i)
{
    struct startup_list *s = sl(i);
#ifdef XS_TEST_ORDERED_QUEUE
    struct adapter *a=s->ordered_queue;
    if (a) AcquireSRWLockExclusive(&a->submit_lock);
#endif
    ULONG refs = InterlockedDecrement(&s->refs);
#ifdef XS_TEST_ORDERED_QUEUE
    if (a) {
        if (!refs) {
            struct startup_list **p=&a->ordered;
            while (*p!=s) p=&(*p)->ordered_next;
            *p=s->ordered_next;
        }
        ReleaseSRWLockExclusive(&a->submit_lock);
        if (!refs) {
            IUnknown_Release(s->ordered_lease);
            IUnknown_Release(&a->iface);
        }
    }
#endif
    if (!refs) {
#ifdef SCARLETT_PAIR_SUBMIT
        pair_unseal(s);
        drop_sr(s);
        drop_om(&s->first_om);
        if(s->pre_release)ID3D12GraphicsCommandList7_Release(s->pre_release);
        if(s->after_alloc)ID3D12CommandAllocator_Release(s->after_alloc);
#endif
        ID3D12GraphicsCommandList7_Release(s->host);
        forget_receipt(s);
        IUnknown_Release(s->allocator);
        drop_source(&s->previous_vb);drop_source(&s->vb);drop_source(&s->source);drop_om(&s->om);drop_attachments(s->attachments);
        drop_split(s); drop_timestamp(s->stamp);
        drop_marker(s->marker);
        if (s->primary) ID3D12Resource_Release(s->primary);
        if(s->output_heap)ID3D12DescriptorHeap_Release(s->output_heap);
        if(s->primary_pin)IUnknown_Release(s->primary_pin);
        drop_heaps(s->heaps);
        if (s->pool_owner) IUnknown_Release(s->pool_owner);
#ifdef SCARLETT_EXPERIMENTAL_TAGGED_BATCH
        if (s->sync_owner) IUnknown_Release(s->sync_owner);
#endif
        if(s->graphics_identity)IUnknown_Release(s->graphics_identity);
        if(s->prior_pso)IUnknown_Release(s->prior_pso);
        if(s->prior_root)IUnknown_Release(s->prior_root);
        if(s->bound)IUnknown_Release(s->bound);
        sl_free(s);
    }
    return refs;
}
static HRESULT WINAPI sl_query(IUnknown *i, REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    *out = NULL;
    if (!iid) return E_INVALIDARG;
    if (!IsEqualIID(iid, sl(i)->reservation ? &xs_pool_list_id : &xs_sync_list_id)
            && !IsEqualIID(iid, &IID_IUnknown)) return E_NOINTERFACE;
    sl_addref(i);
    *out = i;
    return S_OK;
}

static void sl_barrier(struct startup_list *s, BOOL valid)
{
    if (s->closed) xs_stop();
    if (!valid) s->error = E_NOTIMPL; /* Report at Close, before anything can be submitted. */
    if (FAILED(s->error)) return;
    s->post_boundary=FALSE;
    D3D12_GLOBAL_BARRIER barrier = {D3D12_BARRIER_SYNC_ALL, D3D12_BARRIER_SYNC_ALL,
            D3D12_BARRIER_ACCESS_COMMON, D3D12_BARRIER_ACCESS_COMMON};
    D3D12_BARRIER_GROUP group = {.Type=D3D12_BARRIER_TYPE_GLOBAL, .NumBarriers=1, .pGlobalBarriers=&barrier};
    ID3D12GraphicsCommandList7_Barrier(s->host, 1, &group);
}
static void WINAPI sl_420(IUnknown *i, UINT mask, UINT control, UINT64 address, UINT64 range)
{
    struct startup_list *s = sl(i);
    sl_barrier(s, mask == (s->type == D3D12_COMMAND_LIST_TYPE_DIRECT ? 0x700000u : 0x100000u)
            && !control && !address && range == 0xffffffffff00ull);
}
static void WINAPI sl_418(IUnknown *i, UINT mask, UINT control, UINT64 address, UINT64 range)
{
    struct startup_list *s = sl(i);
    BOOL direct = s->type == D3D12_COMMAND_LIST_TYPE_DIRECT;
    sl_barrier(s, mask == (direct ? 0xc3c0u : 0xc380u) && control == (direct ? 0x80020000u : 0x20000u)
            && !address && range == 0xffffffffff00ull);
}
/* +540 is pointer-aligned; only +544..563 is guest cache data. Padding is
 * inert, contains no native pointers, and must remain intact at bind/close. */
static const BYTE cache_template[4096]={[0 ... 3]=0xa5,[36 ... 4095]=0xa5};
#ifdef SCARLETT_PAIR_SUBMIT
static HRESULT close_at(IUnknown *i,UINT_PTR caller)
#else
static HRESULT WINAPI sl_close(IUnknown *i)
#endif
{
    struct startup_list *s = sl(i);
    if (s->closed) return E_FAIL;
    if(s->cache_page && memcmp(s->cache_page,cache_template,sizeof cache_template))return E_NOTIMPL;
    HRESULT hr = ID3D12GraphicsCommandList7_Close(s->host);
    s->closed = TRUE;
    batch_step(s,B_CLOSED);
    if (FAILED(hr)) s->error = hr;
    if(SUCCEEDED(s->error))offer_release(s,i);
#ifdef SCARLETT_PAIR_SUBMIT
    if((s->offered || s->deferred) && caller==(UINT_PTR)GetModuleHandleW(NULL)+0x72a1)pair_seal(s,i);
#endif
    return s->error;
}
#ifdef SCARLETT_PAIR_SUBMIT
static HRESULT WINAPI sl_close(IUnknown *i){return close_at(i,(UINT_PTR)__builtin_return_address(0));}
#endif

static BOOL allocator_matches(ID3D12CommandAllocator *a, ID3D12Device *device)
{
    ID3D12Device *owner;
    if (!a || FAILED(ID3D12CommandAllocator_GetDevice(a, &IID_ID3D12Device, (void **)&owner))) return FALSE;
    BOOL match = owner == device;
    ID3D12Device_Release(owner);
    return match;
}
static HRESULT WINAPI sl_reset(IUnknown *i, IUnknown *alloc, IUnknown *pso, UINT flags)
{
    struct startup_list *s = sl(i);
#ifdef XS_TEST_ORDERED_QUEUE
    if (s->single_use) return E_ACCESSDENIED;
#endif
    if (flags || pso) return E_NOTIMPL;
    if (!s->closed) return E_FAIL;
    ID3D12CommandAllocator *a = xs_allocator_host(alloc, s->type);
    if (!allocator_matches(a, s->device)) return E_INVALIDARG;
    HRESULT hr;
#ifdef SCARLETT_PAIR_SUBMIT
    if(s->seal){
        ID3D12GraphicsCommandList7 *next=NULL;
        hr=ID3D12Device_CreateCommandList(s->device,1,s->type,a,NULL,&IID_ID3D12GraphicsCommandList7,(void**)&next);
        if(FAILED(hr))return hr;
        pair_unseal(s);ID3D12GraphicsCommandList7_Release(s->host);s->host=next;
    }else
#endif
    hr = ID3D12GraphicsCommandList7_Reset(s->host, a, NULL);
    if (FAILED(hr)) return hr;
#ifdef SCARLETT_PAIR_SUBMIT
    drop_om(&s->first_om);
    if(s->pre_release){ID3D12GraphicsCommandList7_Release(s->pre_release);s->pre_release=NULL;}
    if(s->after_alloc){ID3D12CommandAllocator_Release(s->after_alloc);s->after_alloc=NULL;}
    s->nops=s->split_at=0;s->reuse_epoch=0;
    drop_sr(s);
    s->alloc_generation=InterlockedCompareExchange64(&CONTAINING_RECORD(alloc,struct command_allocator,a.iface)->generation,0,0);
#endif
    forget_receipt(s);s->output_slot=0;
#ifdef SCARLETT_PAIR_SUBMIT
    s->successor_query=FALSE;
#endif
    IUnknown_AddRef(alloc);
    IUnknown_Release(s->allocator);
    s->allocator = alloc;
    if(s->graphics_identity){IUnknown_Release(s->graphics_identity);s->graphics_identity=NULL;s->graphics_root=NULL;}
    s->primary_cleared=FALSE;
    s->primary_released=FALSE;
    s->primary_key=0;
    if(s->output_heap){ID3D12DescriptorHeap_Release(s->output_heap);s->output_heap=NULL;}
    if(s->primary_pin){IUnknown_Release(s->primary_pin);s->primary_pin=NULL;}
    s->output_cpu.ptr=0;s->output_serial=s->clear_serial=s->clear_slot=0;
    if (s->primary) { ID3D12Resource_Release(s->primary); s->primary=NULL; }
    drop_heaps(s->heaps);
    drop_marker(s->marker); s->marker=NULL;
    drop_split(s); drop_timestamp(s->stamp); s->stamp=NULL;
    s->prior_b27_slot=0;memset(&s->table0,0,sizeof s->table0);drop_source(&s->previous_vb);drop_source(&s->vb);drop_source(&s->source);drop_om(&s->om);drop_attachments(s->attachments);
    AcquireSRWLockExclusive(&s->borrow_lock);
    if(s->prior_pso){IUnknown_Release(s->prior_pso);s->prior_pso=NULL;}
    if(s->prior_root){IUnknown_Release(s->prior_root);s->prior_root=NULL;}
    if(s->bound)IUnknown_Release(s->bound);
    s->bound=NULL;s->required_root=NULL;s->pipeline=NULL;
    if(s->cache_page)memcpy(s->cache_page,cache_template,sizeof cache_template);
    s->stencil_zero=FALSE;
    s->blend_ones=FALSE;
    s->full_bounds=s->full_rect=s->full_vp=FALSE;
    s->pending_470=FALSE;
    s->clear4_lanes=0;s->guest_topology=0;s->clear4_drew=FALSE;
    s->rt_roundtripped=s->offscreen_sr=FALSE;
    s->depth_zero_pending=FALSE; /* host_exposed stays sticky: an earlier borrower may still hold host. */
    ReleaseSRWLockExclusive(&s->borrow_lock);
    s->closed = FALSE;
#ifdef SCARLETT_EXPERIMENTAL_TAGGED_BATCH
    s->phase=B_EMPTY;
#endif
    s->error = S_OK;
    return S_OK;
}
struct sl_table {
    IUnknownVtbl prefix;
    xs_stop_fn gap18[6];
    __typeof__(&sl_close) close;
    xs_stop_fn gap50[121];
    __typeof__(&sl_418) sync418;
    __typeof__(&sl_420) sync420;
    xs_stop_fn gap428[6];
    __typeof__(&sl_reset) reset;
};
_Static_assert(offsetof(struct sl_table, close)==0x48 && offsetof(struct sl_table, sync418)==0x418 &&
        offsetof(struct sl_table, sync420)==0x420 && offsetof(struct sl_table, reset)==0x458, "x64 list ABI");
static const struct sl_table sl_vtbl = {
    {sl_query, sl_addref, sl_release}, {[0 ... 5]=xs_stop}, sl_close,
    {[0 ... 120]=xs_stop}, sl_418, sl_420, {[0 ... 5]=xs_stop}, sl_reset
};

static HRESULT bind_initial_heaps(struct startup_list *s,UINT count,IUnknown *const *input)
{
    IUnknown *pair[2]; SIZE_T n;
    ID3D12DescriptorHeap *banks[2];
    if (!s->pool_owner || s->closed || s->type!=D3D12_COMMAND_LIST_TYPE_DIRECT || count!=2) return E_INVALIDARG;
    if (!ReadProcessMemory(GetCurrentProcess(),input,pair,sizeof pair,&n) || n!=sizeof pair) return E_POINTER;
    /* shortcut: one distinct pair per recording, until an in-list replacement is observed. */
    if (s->heaps[0] && memcmp(s->heaps,pair,sizeof pair)) return E_NOTIMPL;
    HRESULT hr=xs_initial_heap_banks(s->pool_owner,pair,banks);
    if (FAILED(hr)) return hr;
    if(s->table0.heap && s->table0.heap!=pair[0])memset(&s->table0,0,sizeof s->table0);
    if (!s->heaps[0]) for (unsigned i=0;i<2;i++) { IUnknown_AddRef(pair[i]); s->heaps[i]=pair[i]; }
    pair_op(s,0x100);
    ID3D12GraphicsCommandList7_SetDescriptorHeaps(s->host,2,banks);
#ifdef SCARLETT_EXPERIMENTAL_TAGGED_BATCH
    if (s->phase==B_PREFIX) batch_step(s,B_REBOUND);
    else batch_step(s,B_BOUND);
#endif
    s->post_boundary=FALSE;
    return S_OK;
}
static void WINAPI sl_bind(IUnknown *i,UINT count,IUnknown *const *input)
{
    if (FAILED(bind_initial_heaps(sl(i),count,input))) xs_stop();
}
/* The acquire pins the original AMD control section, independent of later VA reuse. */
static HRESULT record_control_prefix(struct startup_list *s,struct prefix_marker *m,const struct scarlett_section_info *id,UINT value)
{
    if (!s->heaps[0] || !s->heaps[1] || !id->section || id->view_size!=65536 ||
        !((id->offset==8 && !value) || (id->offset==0x50 && value==1)
#ifdef SCARLETT_PAIR_SUBMIT
          || (id->offset==0x98 && value==2 && s->phase==B_BOUND && !s->alloc_generation &&
              s->error==S_OK && !s->deferred && s->nops==2 && s->ops[0]==0x100/8 && s->ops[1]==0x238/8)
#endif
        )) return E_NOTIMPL;
    D3D12_FEATURE_DATA_D3D12_OPTIONS3 opts;
    HRESULT hr=ID3D12Device_CheckFeatureSupport(s->device,D3D12_FEATURE_D3D12_OPTIONS3,&opts,sizeof opts);
    if (FAILED(hr) || !(opts.WriteBufferImmediateSupportFlags&D3D12_COMMAND_LIST_SUPPORT_FLAG_DIRECT)) return E_NOTIMPL;
    HRESULT (WINAPI *open)(ID3D12Device3 *,SIZE_T,HANDLE,HANDLE *,ID3D12Heap **)=(void*)GetProcAddress(GetModuleHandleW(L"d3d12core.dll"),"ScarlettControlHeapV1");
    if (!open) return E_NOTIMPL;
    ID3D12Device3 *d=NULL;
    hr=ID3D12Device_QueryInterface(s->device,&IID_ID3D12Device3,(void**)&d);
    if (FAILED(hr)) return hr;
    hr=open(d,id->view_size,id->section,NULL,&m->heap);
    ID3D12Device3_Release(d);
    if (FAILED(hr)) return hr;
    D3D12_RESOURCE_DESC desc={.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER,.Width=id->view_size,.Height=1,.DepthOrArraySize=1,.MipLevels=1,.SampleDesc.Count=1,.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR};
    hr=ID3D12Device_CreatePlacedResource(s->device,m->heap,0,&desc,D3D12_RESOURCE_STATE_COPY_DEST,NULL,&IID_ID3D12Resource,(void**)&m->buffer);
    if (FAILED(hr)) return hr;
    UINT64 gpu=ID3D12Resource_GetGPUVirtualAddress(m->buffer);
    if (!gpu || gpu>UINT64_MAX-id->offset) return E_FAIL;
    D3D12_WRITEBUFFERIMMEDIATE_PARAMETER op={gpu+id->offset,value};
    ID3D12GraphicsCommandList7_WriteBufferImmediate(s->host,1,&op,NULL);
    m->value=value;
    return S_OK;
}
static HRESULT prefix_write(struct startup_list *s,UINT64 addr,UINT value,UINT64 control,UINT flags)
{
    struct scarlett_section_info lease;
    /* shortcut: prefix only, before any host recorder escapes; no mid-list stage inferred. */
    if (!s->reservation || !s->pool_owner || s->closed || s->type!=D3D12_COMMAND_LIST_TYPE_DIRECT ||
        s->primary || s->host_exposed || s->marker || s->stamp || control || flags) return E_NOTIMPL;
    if (!addr || (addr&3)) return E_INVALIDARG;
    struct prefix_marker *m=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof *m);
    if (!m) return E_OUTOFMEMORY;
    m->refs=1;
    HRESULT hr=scarlett_query_section(0x5343000a,(void*)addr,4,&lease);
    if (SUCCEEDED(hr)) {
        hr=record_control_prefix(s,m,&lease,value);
        if (SUCCEEDED(hr)) m->control_section=lease.section;
        else if (lease.section) CloseHandle(lease.section);
        if (FAILED(hr)) drop_marker(m); else s->marker=m;
        return hr;
    }
    struct scarlett_section_info id;
    hr=scarlett_query_section(0x53430015,(void *)addr,4,&id);
    if (FAILED(hr)) {drop_marker(m);return hr;}
    m->generation=id.offset;
    hr=scarlett_query_section(0x53430120,(void *)addr,4,&lease);
    if (FAILED(hr)) {HeapFree(GetProcessHeap(),0,m);return hr;}
    m->alias=lease.base; m->word=(LONG *)((BYTE *)lease.base+lease.offset); m->value=value;
    hr=scarlett_query_section(0x53430015,(void *)addr,4,&id);
    if (FAILED(hr) || id.offset!=m->generation) {drop_marker(m);return E_INVALIDARG;}
    s->marker=m;
    return S_OK;
}
/* The AMD prefix, resolve and publication all belong to the same native recording. */
#ifdef SCARLETT_PAIR_SUBMIT
#include "rtx3070.h"
#endif
static HRESULT record_native_post(struct startup_list *s,UINT64 addr,UINT value,UINT64 control,UINT flags)
{
    struct prefix_marker *m=s->marker;
#ifdef SCARLETT_PAIR_SUBMIT
    if (m && m->value>1 && !(m->value==2 && s->stamp && s->stamp->third_wb &&
        s->phase==B_HEAD_STAMP && !s->alloc_generation && !s->head && !s->post && !s->deferred &&
        s->nops==5 && !memcmp(s->ops,"\x20\x47\x20\x48\x47",5) && xs_is_adapter(s->device,0x1002,0x164e)))
        return E_NOTIMPL;
#endif
    if (!s->stamp || !s->stamp->retained || !m || !m->buffer || !m->control_section || m->native_post || s->closed ||
        s->host_exposed || control || flags || !value) return E_NOTIMPL;
    struct scarlett_section_info id;
    HRESULT hr=scarlett_query_section(0x5343000a,(void *)addr,4,&id);
    if (FAILED(hr)) return hr;
    BOOL (WINAPI *same)(HANDLE,HANDLE)=(void *)GetProcAddress(GetModuleHandleW(L"kernelbase.dll"),"CompareObjectHandles");
#ifdef SCARLETT_PAIR_SUBMIT
    SIZE_T off=m->value==2 ? 0x9c : m->value==1 ? 0x54 : 12;
    BOOL match=id.view_size==65536 && id.offset==off && (!m->value || value==0x29a5ee15) &&
        same && same(id.section,m->control_section);
#else
    BOOL match=id.view_size==65536 && (m->value==1 ? id.offset==0x54 && value==0x29a5ee15 : id.offset==12) &&
        same && same(id.section,m->control_section);
#endif
    if (id.section) CloseHandle(id.section);
    if (!match) return E_INVALIDARG;
    D3D12_GLOBAL_BARRIER b={D3D12_BARRIER_SYNC_ALL,D3D12_BARRIER_SYNC_ALL,D3D12_BARRIER_ACCESS_COMMON,D3D12_BARRIER_ACCESS_COMMON};
    D3D12_BARRIER_GROUP g={.Type=D3D12_BARRIER_TYPE_GLOBAL,.NumBarriers=1,.pGlobalBarriers=&b};
    D3D12_WRITEBUFFERIMMEDIATE_PARAMETER op={ID3D12Resource_GetGPUVirtualAddress(m->buffer)+id.offset,value};
    ID3D12GraphicsCommandList7_Barrier(s->host,1,&g);
    ID3D12GraphicsCommandList7_WriteBufferImmediate(s->host,1,&op,NULL);
    ID3D12GraphicsCommandList7_Barrier(s->host,1,&g);
    m->native_post=value;
    return S_OK;
}
static const D3D12_RECT clear_rect={0,0,3840,2160};
static const D3D12_VIEWPORT clear_viewport={0,0,3840,2160,0,1};
static const FLOAT ones_factor[4]={1,1,1,1};
static HRESULT split_native_list(struct startup_list *s,struct prefix_marker *post)
{
    post->refs=1;post->parent=s->marker;InterlockedIncrement(&post->parent->refs);
    ID3D12GraphicsCommandList7 *tail=NULL;
    ID3D12CommandAllocator *a=NULL;
    HRESULT hr=ID3D12Device_CreateCommandAllocator(s->device,s->type,&IID_ID3D12CommandAllocator,(void **)&a);
    if (SUCCEEDED(hr)) hr=ID3D12Device_CreateCommandList(s->device,1,s->type,a,s->pipeline,&IID_ID3D12GraphicsCommandList7,(void **)&tail);
    if (SUCCEEDED(hr) && s->guest_topology)ID3D12GraphicsCommandList7_IASetPrimitiveTopology(tail,D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    if (SUCCEEDED(hr) && s->stencil_zero) ID3D12GraphicsCommandList7_OMSetStencilRef(tail,0);
    if (SUCCEEDED(hr) && s->blend_ones) ID3D12GraphicsCommandList7_OMSetBlendFactor(tail,ones_factor);
    if (SUCCEEDED(hr) && !s->offscreen_sr)replay_om(&s->om,tail);
    if (SUCCEEDED(hr) && s->output_heap)ID3D12GraphicsCommandList7_OMSetRenderTargets(tail,1,&s->output_cpu,FALSE,NULL);
    if (SUCCEEDED(hr) && s->full_bounds) ID3D12GraphicsCommandList7_OMSetDepthBounds(tail,0.f,1.f);
    if (SUCCEEDED(hr) && s->full_rect) ID3D12GraphicsCommandList7_RSSetScissorRects(tail,1,&clear_rect);
    if (SUCCEEDED(hr) && s->full_vp) ID3D12GraphicsCommandList7_RSSetViewports(tail,1,&clear_viewport);
    if (SUCCEEDED(hr) && s->graphics_root)ID3D12GraphicsCommandList7_SetGraphicsRootSignature(tail,s->graphics_root);
    if (SUCCEEDED(hr) && !s->prior_pso && s->source.buffer)bind_source(tail,&s->source);
    if(SUCCEEDED(hr) && s->vb.buffer)bind_colour_vb(tail,&s->vb);
    if (SUCCEEDED(hr) && s->heaps[0]) {
        ID3D12DescriptorHeap *banks[2]; hr=xs_initial_heap_banks(s->pool_owner,s->heaps,banks);
        if (SUCCEEDED(hr)) ID3D12GraphicsCommandList7_SetDescriptorHeaps(tail,2,banks);
    }
    if (SUCCEEDED(hr)) {
        hr=ID3D12GraphicsCommandList7_Close(s->host);
        if (FAILED(hr)) s->error=hr;
    }
    if (FAILED(hr)) {
        if (tail) ID3D12GraphicsCommandList7_Release(tail);
        if (a) ID3D12CommandAllocator_Release(a);
        drop_marker(post);return hr;
    }
    s->post=post;s->head=s->host;s->host=tail;s->tail_allocator=a;
#ifdef SCARLETT_PAIR_SUBMIT
    s->split_at=s->nops;
#endif
    s->post_boundary=!post->counter;
    return S_OK;
}
#ifdef SCARLETT_PAIR_SUBMIT
#include "rtx3070.h"
#endif
static HRESULT split_after_timestamp(struct startup_list *s,UINT64 addr,UINT value,UINT64 control,UINT flags)
{
    if (!s->marker || !s->marker->alias || !s->stamp || s->closed ||
        s->host_exposed || s->post || control || flags) return E_NOTIMPL;
    SIZE_T prefix=s->marker->value==1 ? 0x50 : 8;
#ifdef SCARLETT_PAIR_SUBMIT
    /* Only the observed third-bank post; its next wait has not been measured. */
    if (s->marker->value==2 && s->phase==B_HEAD_STAMP && !s->alloc_generation &&
        s->error==S_OK && !s->deferred && !s->primary && s->nops==5 &&
        !memcmp(s->ops,"\x20\x47\x20\x48\x47",5) && xs_is_rtx3070(s->device)) prefix=0x98;
    if (s->marker->value && (prefix==8 || value!=0x29a5ee15)) return E_NOTIMPL;
#else
    if (s->marker->value && (s->marker->value!=1 || value!=0x29a5ee15)) return E_NOTIMPL;
#endif
    struct scarlett_section_info id;
    HRESULT hr=scarlett_query_section(0x53430015,(void *)addr,4,&id);
    if (FAILED(hr)) return hr;
    if (id.offset!=s->marker->generation || id.view_size!=65536 || addr!=(UINT_PTR)id.base+prefix+4 ||
        s->marker->word!=(LONG *)((BYTE *)s->marker->alias+prefix)) return E_INVALIDARG;
    struct prefix_marker *post=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof *post);
    if (!post) return E_OUTOFMEMORY;
    post->word=s->marker->word+1;post->value=value;
    return split_native_list(s,post);
}
#ifdef SCARLETT_PAIR_SUBMIT
static HRESULT native_post_at(struct startup_list *s,UINT64 addr,UINT value,UINT64 control,UINT flags,UINT_PTR caller)
{
    if (s->marker && s->marker->value==2 && caller!=0x4ed46aa) return E_NOTIMPL;
    return record_native_post(s,addr,value,control,flags);
}
#else
#define native_post_at(s,a,v,c,f,caller) record_native_post(s,a,v,c,f)
#endif
static void WINAPI sl_marker(IUnknown *i,UINT64 addr,UINT value,UINT64 control,UINT flags)
{
    pair_op(sl(i),0x238);
    struct startup_list *s=sl(i);
    HRESULT hr=!s->stamp ? prefix_write(s,addr,value,control,flags) : s->marker && s->marker->buffer ?
        native_post_at(s,addr,value,control,flags,(UINT_PTR)__builtin_return_address(0)-(UINT_PTR)GetModuleHandleW(NULL)) :
        split_after_timestamp(s,addr,value,control,flags);
    if (FAILED(hr)) xs_stop();
    batch_step(s,(s->stamp?B_POST:B_PREFIX));
}
static HRESULT pool_timestamp(struct startup_list *s,UINT64 addr,UINT64 x,UINT64 y,UINT flags)
{
    /* shortcut: only the observed kind-2 query encoding and primary-release pair. */
    if (!s->reservation || !s->pool_owner || s->closed || s->type!=D3D12_COMMAND_LIST_TYPE_DIRECT ||
        FAILED(s->error) || x || y || flags!=1) return E_NOTIMPL;
    if (!addr || (addr&7)) return E_INVALIDARG;
#ifdef SCARLETT_PAIR_SUBMIT
    if (s->marker && s->marker->buffer && s->marker->value==2) {
        if(s->phase!=B_REBOUND || s->stamp || s->head || s->post || s->deferred ||
           s->primary_released || s->host_exposed)return E_NOTIMPL;
        return record_timestamp(s->device,s->host,addr,2,&s->stamp);
    }
    if(s->deferred){
        struct release_receipt *r=s->deferred;SIZE_T got;UINT bank;UINT64 indices;UINT_PTR record,exe=(UINT_PTR)GetModuleHandleW(NULL);
        if(s->stamp || s->successor_query || !r->claimed || r->owner!=s->pool_owner || r->tid!=GetCurrentThreadId() ||
           s->host_exposed || !r->producer_stamp || s->deferred_wire.pResource!=(ID3D12Resource*)r->key ||
           s->deferred_wire.LayoutAfter!=(D3D12_BARRIER_LAYOUT)0x1f)return E_NOTIMPL;
        if(!s->pre_release || !s->after_alloc || s->host==s->pre_release)return E_NOTIMPL;
        if(!ReadProcessMemory(GetCurrentProcess(),(void*)(exe+0xa943a0c),&bank,4,&got) || got!=4 || bank>=4 ||
           !ReadProcessMemory(GetCurrentProcess(),(void*)(exe+0x75dedb0),&indices,8,&got) || got!=8 ||
           !ReadProcessMemory(GetCurrentProcess(),(void*)(exe+0xa943a18+bank*8),&record,8,&got) || got!=8)return E_INVALIDARG;
        if(r->producer_stamp->cpu_section){
            if(r->producer_stamp->cpu_addr!=addr+8)return E_INVALIDARG;
            struct system_timestamp *tail=NULL;SIZE_T off;
            HRESULT hr=prepare_timestamp(s->device,addr,FALSE,&tail,&off);
            if(FAILED(hr))return hr;
            if(!cpu_successor_target(r->producer_stamp,tail,bank,indices,record)){
                drop_timestamp(tail);return E_INVALIDARG;
            }
            emit_q0(tail,s->host,off);s->stamp=tail;
            s->successor_query=TRUE;return S_OK;
        }
        HRESULT hr=successor_target(r->producer_stamp,addr,bank,indices,record);if(FAILED(hr))return hr;
        s->stamp=r->producer_stamp;InterlockedIncrement(&s->stamp->refs);
        s->successor_query=TRUE;
        emit_successor_timestamp(s->stamp,s->host);
        return S_OK;
    }
#endif
#ifdef SCARLETT_EXPERIMENTAL_PRIMARY_RELEASE
    if (s->primary_released) {
        if (!s->stamp || !s->head || !s->post || !s->post->counter || s->host_exposed) return E_NOTIMPL;
        return record_tail_timestamp(s->stamp,s->host,addr);
    }
#endif
    if (s->stamp) return E_NOTIMPL;
    return record_timestamp(s->device,s->host,addr,s->marker && s->marker->value==1,&s->stamp);
}
static void WINAPI sl_timestamp(IUnknown *i,UINT64 addr,UINT64 x,UINT64 y,UINT flags)
{
    pair_op(sl(i),0x240);
    if (FAILED(pool_timestamp(sl(i),addr,x,y,flags))) xs_stop();
    batch_step(sl(i),(sl(i)->primary_released?B_TAIL_STAMP:B_HEAD_STAMP));
}
#ifdef SCARLETT_EXPERIMENTAL_DIRECT_WAIT
#include "rtx3070.h"
/* shortcut: only an untouched tail of the RTX marker/timestamp/post split.
 * Any new pooled recorder must invalidate post_boundary before appending work. */
static HRESULT record_counter_wait(struct startup_list *s,UINT64 addr,UINT op,UINT64 ref,UINT flags)
{
    struct prefix_marker *m=s->marker,*p=s->post;
    if (!s->reservation || !s->pool_owner || s->type!=D3D12_COMMAND_LIST_TYPE_DIRECT ||
        s->closed || FAILED(s->error) || s->host_exposed || s->primary || !m || !s->stamp ||
        (m->value && !(m->value==1 && s->stamp->second_bank)
#ifdef SCARLETT_PAIR_SUBMIT
        /* Both callers wait after the post; only the alias recording has already split. */
        && !(m->value==2 && s->phase==B_POST && !s->alloc_generation && !s->deferred &&
             s->nops==6 && !memcmp(s->ops,"\x20\x47\x20\x48\x47\x4a",6) &&
             (m->buffer ? s->stamp->third_wb && !m->alias && !s->split_at && !s->post_boundary :
              m->alias && m->word==(LONG *)((BYTE *)m->alias+0x98) && p && p->word==m->word+1 &&
              !p->counter && !s->stamp->retained && s->split_at==5))
#endif
        ) ||
        op!=7 || flags!=1) return E_NOTIMPL;
    if (!addr || (addr&7)) return E_INVALIDARG;
    struct scarlett_section_info id;
    if (m->buffer) {
        if (!s->stamp->retained || !m->control_section || m->native_post!=0x29a5ee15 || p || s->head)
            return E_NOTIMPL;
        HRESULT hr=scarlett_query_section(0x5343000a,(void *)addr,8,&id);
        if (FAILED(hr)) return hr;
        BOOL (WINAPI *same)(HANDLE,HANDLE)=(void *)GetProcAddress(GetModuleHandleW(L"kernelbase.dll"),"CompareObjectHandles");
        BOOL match=id.base==(void *)addr && id.view_size==65536 && !id.offset && id.section &&
            same && same(id.section,m->control_section);
        if (id.section) CloseHandle(id.section);
        if (!match) return E_INVALIDARG;
        p=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof *p);
        if (!p) return E_OUTOFMEMORY;
        D3D12_RANGE read={0,8},none={0,0};
        void *mapped=NULL;
        hr=ID3D12Resource_Map(m->buffer,0,&read,&mapped);
        if (SUCCEEDED(hr) && (!mapped || ((UINT_PTR)mapped&7))) {
            ID3D12Resource_Unmap(m->buffer,0,&none);
            hr=E_FAIL;
        }
        if (FAILED(hr)) { HeapFree(GetProcessHeap(),0,p);return hr; }
        p->counter=mapped;p->threshold=ref;
        return split_native_list(s,p); /* native post and all prior work stay in the head */
    }
    if (!s->post_boundary || !s->head || !m->alias || !s->stamp->mapping ||
        !p || p->parent!=m || p->value!=0x29a5ee15) return E_NOTIMPL;
    if (FAILED(scarlett_query_section(0x53430015,(void *)addr,8,&id)) ||
        id.base!=(void *)addr || id.view_size!=65536 || id.offset!=m->generation) return E_INVALIDARG;
    if (!xs_is_rtx3070(s->device)) return E_NOTIMPL;
    p->threshold=ref; p->counter=m->alias; s->post_boundary=FALSE;
    return S_OK;
}
#ifdef SCARLETT_PAIR_SUBMIT
static HRESULT wait64_at(struct startup_list *s,UINT64 addr,UINT op,UINT64 ref,UINT flags,UINT_PTR caller)
{
    if (s->marker && s->marker->value==2 && caller!=0x2484b12) return E_NOTIMPL;
    pair_op(s,0x250);
    HRESULT hr=record_counter_wait(s,addr,op,ref,flags);
    if (SUCCEEDED(hr)) batch_step(s,B_WAIT);
    return hr;
}
#endif
static void WINAPI sl_wait64(IUnknown *i,UINT64 addr,UINT op,UINT64 ref,UINT flags)
{
#ifdef SCARLETT_PAIR_SUBMIT
    if (FAILED(wait64_at(sl(i),addr,op,ref,flags,(UINT_PTR)__builtin_return_address(0)-(UINT_PTR)GetModuleHandleW(NULL)))) xs_stop();
#else
    pair_op(sl(i),0x250);
    if (SUCCEEDED(record_counter_wait(sl(i),addr,op,ref,flags))) {batch_step(sl(i),B_WAIT);return;}
    xs_stop();
#endif
}
#else
#define sl_wait64 xs_stop
#endif
enum { XS_RECTLIST=17 };
static void recorded_direct_state(struct startup_list *s);
static BOOL pending_colour_inputs(struct startup_list *s);
/* The return half of the round trip swaps all three pairs. Flags/range are checked by the caller. */
static BOOL rt_shader_edge(const D3D12_TEXTURE_BARRIER *b,BOOL returning)
{
    return b->SyncBefore==(returning?D3D12_BARRIER_SYNC_ALL_SHADING:D3D12_BARRIER_SYNC_RENDER_TARGET) &&
        b->SyncAfter==(returning?D3D12_BARRIER_SYNC_RENDER_TARGET:D3D12_BARRIER_SYNC_ALL_SHADING) &&
        b->AccessBefore==(returning?D3D12_BARRIER_ACCESS_SHADER_RESOURCE:D3D12_BARRIER_ACCESS_RENDER_TARGET) &&
        b->AccessAfter==(returning?D3D12_BARRIER_ACCESS_RENDER_TARGET:D3D12_BARRIER_ACCESS_SHADER_RESOURCE) &&
        b->LayoutBefore==(returning?D3D12_BARRIER_LAYOUT_SHADER_RESOURCE:D3D12_BARRIER_LAYOUT_RENDER_TARGET) &&
        b->LayoutAfter==(returning?D3D12_BARRIER_LAYOUT_RENDER_TARGET:D3D12_BARRIER_LAYOUT_SHADER_RESOURCE);
}
#ifdef SCARLETT_PAIR_SUBMIT
static BOOL same_carried_views(struct startup_list *s)
{
    struct xs_sr_ticket now={0};
    if(!s->sr.rt.identity || !s->sr.ds.identity || !s->sr.end ||
       FAILED(xs_sr_snapshot(s->pool_owner,s->sr.rt.identity,&now)))return FALSE;
    BOOL same=now.end==s->sr.end && !memcmp(&now.rt,&s->sr.rt,sizeof now.rt) &&
        !memcmp(&now.ds,&s->sr.ds,sizeof now.ds);
    if(now.ds.identity)IUnknown_Release(now.ds.identity);
    IUnknown_Release(now.rt.identity);ID3D12Fence_Release(now.end);return same;
}
#endif
static HRESULT record_attachment_pair(struct startup_list *s,const D3D12_BARRIER_GROUP *g)
{
    if(!s->pending_470 || !s->depth_zero_pending || s->host_exposed)return E_NOTIMPL;
    D3D12_TEXTURE_BARRIER b[2]={0};SIZE_T got,bytes=sizeof(b[0])+offsetof(D3D12_TEXTURE_BARRIER,Flags)+sizeof(b[0].Flags);
    if(!ReadProcessMemory(GetCurrentProcess(),g->pTextureBarriers,b,bytes,&got) || got!=bytes)return E_POINTER;
    const D3D12_BARRIER_SUBRESOURCE_RANGE all={.IndexOrFirstMipLevel=UINT_MAX};
    /* Same pinned image twice, after its #4 draw. Frozen OM can outlive both
     * guest descriptor slots; do not resolve either slot again here. */
    const struct xs_attachment_pin *rt=&s->attachments[0];
#ifdef SCARLETT_PAIR_SUBMIT
    if(s->sr.end){
        const struct xs_clear_source *v=&s->vb,*c=&s->source;
        if(rt->identity || s->attachments[1].identity || s->sr_reversed || s->reuse_epoch!=1 ||
           !c->needs_publication || !v->needs_publication || !c->capture || c->capture!=v->capture ||
           !c->placed || v->placed!=c->placed || !v->span.gen || v->span.gen!=c->span.gen ||
           v->span.node!=c->span.node || v->span.bytes!=4080 || c->span.bytes!=100 ||
           s->split_at!=5 || s->alloc_generation || s->table_epoch || s->nops!=28 ||
           memcmp(s->ops,"\x20\x47\x20\x48\x47\x4a\x92\x34\x14\x7e\x19\x22\x4b\x2f\x18\x8e\x17\x92\x32\x46\x16\x15\x2c\x0c\x14\x22\x30\x92",28) ||
           !same_carried_views(s))return E_NOTIMPL;
        rt=&s->sr.rt;
    }
#endif
    if(rt->identity){
        if(!pending_colour_inputs(s) || !s->vb.buffer || s->rt_roundtripped ||
           !s->om.heap[0] || !s->om.heap[1] ||
           !rt->host || rt->host==s->primary)return E_NOTIMPL;
        for(UINT k=0;k<2;k++)if(b[k].pResource!=(ID3D12Resource*)rt->identity ||
            !rt_shader_edge(&b[k],k!=0) ||
            b[k].Flags || memcmp(&b[k].Subresources,&all,sizeof all))return E_NOTIMPL;
        b[0].pResource=b[1].pResource=rt->host;
        D3D12_BARRIER_GROUP host={.Type=D3D12_BARRIER_TYPE_TEXTURE,.NumBarriers=2,.pTextureBarriers=b};
        ID3D12GraphicsCommandList7_Barrier(s->host,1,&host);
        s->rt_roundtripped=TRUE;recorded_direct_state(s);return S_OK;
    }
    if(!s->blend_ones || !s->stencil_zero || !s->bound || !s->required_root ||
        s->graphics_identity!=s->required_root)return E_NOTIMPL;
    for(UINT k=0;k<2;k++)if(b[k].SyncBefore!=D3D12_BARRIER_SYNC_ALL_SHADING ||
        b[k].SyncAfter!=(k?D3D12_BARRIER_SYNC_DEPTH_STENCIL:D3D12_BARRIER_SYNC_RENDER_TARGET) ||
        b[k].AccessBefore!=D3D12_BARRIER_ACCESS_SHADER_RESOURCE ||
        b[k].AccessAfter!=(k?D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE:D3D12_BARRIER_ACCESS_RENDER_TARGET) ||
        b[k].LayoutBefore!=D3D12_BARRIER_LAYOUT_SHADER_RESOURCE ||
        b[k].LayoutAfter!=(k?D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE:D3D12_BARRIER_LAYOUT_RENDER_TARGET) ||
        memcmp(&b[k].Subresources,&all,sizeof all) || b[k].Flags)return E_NOTIMPL;
    HRESULT hr=xs_pin_attachment_pair(s->pool_owner,s->device,b,s->attachments);
    if(FAILED(hr))return hr;
    for(UINT k=0;k<2;k++)b[k].pResource=s->attachments[k].host;
    D3D12_BARRIER_GROUP host={.Type=D3D12_BARRIER_TYPE_TEXTURE,.NumBarriers=2,.pTextureBarriers=b};
    ID3D12GraphicsCommandList7_Barrier(s->host,1,&host);return S_OK;
}
/* shortcut: one observed whole-primary UAV-to-RT transition per recording. */
#ifdef SCARLETT_PAIR_SUBMIT
static BOOL returned_primary_wait(const struct startup_list *s)
{
    const struct prefix_marker *m=s->marker,*p=s->post;
    if (!m || m->value!=2 || !p || p->parent!=m || !p->counter || s->post_boundary ||
        !s->head || s->head==s->host || !s->stamp) return FALSE;
    if (!m->buffer) return s->split_at==5 && m->alias && p->counter==m->alias;
    /* record_counter_wait mapped this parent's buffer and split after its native +9c write. */
    return s->split_at==6 && m->heap && m->control_section && !m->alias && !m->word &&
        m->native_post==0x29a5ee15 && !p->word && s->stamp->third_wb && s->stamp->retained;
}
static HRESULT record_primary_barrier_at(struct startup_list *s,UINT count,const D3D12_BARRIER_GROUP *groups,UINT_PTR issuer)
#else
static HRESULT record_primary_barrier(struct startup_list *s,UINT count,const D3D12_BARRIER_GROUP *groups)
#endif
{
    if (!s->reservation || !s->pool_owner || s->closed || FAILED(s->error) || s->primary_released ||
        s->type!=D3D12_COMMAND_LIST_TYPE_DIRECT || count!=1) return E_NOTIMPL;
    D3D12_BARRIER_GROUP g;
    D3D12_TEXTURE_BARRIER b;
    SIZE_T n;
    if (!ReadProcessMemory(GetCurrentProcess(),groups,&g,sizeof g,&n) || n!=sizeof g) return E_POINTER;
    if(g.Type==D3D12_BARRIER_TYPE_TEXTURE && g.NumBarriers==2){
#ifdef SCARLETT_PAIR_SUBMIT
        if(s->sr.end && issuer!=0x32113)return E_NOTIMPL;
#endif
        return record_attachment_pair(s,&g);
    }
    if (g.Type!=D3D12_BARRIER_TYPE_TEXTURE || g.NumBarriers!=1) return E_NOTIMPL;
    /* The guest need not supply readable tail padding after Flags. */
    SIZE_T bytes=offsetof(D3D12_TEXTURE_BARRIER,Flags)+sizeof b.Flags;
    if (!ReadProcessMemory(GetCurrentProcess(),g.pTextureBarriers,&b,bytes,&n) || n!=bytes) return E_POINTER;
    const D3D12_BARRIER_SUBRESOURCE_RANGE all={.IndexOrFirstMipLevel=UINT_MAX};
    if (b.Flags || memcmp(&b.Subresources,&all,sizeof all)) return E_NOTIMPL;
#ifdef SCARLETT_PAIR_SUBMIT
    if(s->reuse_epoch && issuer==0x32113 && s->primary_cleared && !s->host_exposed &&
       !s->attachments[0].identity && !s->deferred && s->pending_470 && s->depth_zero_pending){
        if(!s->sr.end && s->nops==18 && !memcmp(s->ops,"\x20\x47\x20\x48\x47\x4a\x92\x34\x14\x7e\x19\x22\x4b\x2f\x18\x8e\x17\x92",18) &&
           returned_primary_wait(s) && rt_shader_edge(&b,TRUE)){
            if((UINT_PTR)b.pResource==s->primary_key)return E_INVALIDARG;
            HRESULT hr=xs_sr_snapshot(s->pool_owner,b.pResource,&s->sr);
            if(FAILED(hr))return hr;
            if(s->sr.rt.host==s->primary || (s->marker->buffer && !s->sr.ds.identity)){
                drop_sr(s);return E_INVALIDARG;
            }
        }else if(s->sr.end && b.pResource==(ID3D12Resource*)s->sr.rt.identity){
            if((!s->sr_view && !s->om.heap[0]) || s->sr_reversed || !rt_shader_edge(&b,FALSE) ||
               b.pResource!=(ID3D12Resource*)s->sr.rt.identity)return E_NOTIMPL;
            s->sr_reversed=TRUE;
        }else goto original_edge;
        b.pResource=s->sr.rt.host;g.pTextureBarriers=&b;ID3D12GraphicsCommandList7_Barrier(s->host,1,&g);
        recorded_direct_state(s);return S_OK;
    }
original_edge:
#endif
#ifdef SCARLETT_EXPERIMENTAL_TAGGED_BATCH
    if(s->deferred)return E_ACCESSDENIED;
    if(!s->primary && b.LayoutAfter==(D3D12_BARRIER_LAYOUT)0x1f)return defer_release(s,&b);
#endif
    if (s->previous_vb.buffer) {
        /* The earlier pair pinned this exact guest identity under the recording owner's lock. */
        const struct xs_attachment_pin *rt=&s->attachments[0];
        if(!s->rt_roundtripped || s->offscreen_sr || s->host_exposed ||
            (s->primary && (!s->primary_key || s->primary_key==(UINT_PTR)rt->identity || s->primary==rt->host)) ||
            !s->pending_470 || s->bound || s->pipeline || !s->om.heap[0] || !rt->identity || !rt->host ||
            b.pResource!=(ID3D12Resource*)rt->identity || !rt_shader_edge(&b,FALSE))return E_NOTIMPL;
        b.pResource=rt->host;g.pTextureBarriers=&b;
        ID3D12GraphicsCommandList7_Barrier(s->host,1,&g);
        s->offscreen_sr=TRUE;recorded_direct_state(s);return S_OK;
    }
    if (s->primary) {
#ifdef SCARLETT_EXPERIMENTAL_PRIMARY_RELEASE
        /* shortcut: host COMMON for this cleared primary only; qualify its retail acquire before enabling. */
        if (!s->primary_cleared || s->host_exposed ||
            b.SyncBefore!=D3D12_BARRIER_SYNC_RENDER_TARGET || b.SyncAfter!=D3D12_BARRIER_SYNC_NONE ||
            b.AccessBefore!=D3D12_BARRIER_ACCESS_RENDER_TARGET || b.AccessAfter!=D3D12_BARRIER_ACCESS_NO_ACCESS ||
            b.LayoutBefore!=D3D12_BARRIER_LAYOUT_RENDER_TARGET || b.LayoutAfter!=(D3D12_BARRIER_LAYOUT)0x1f) return E_NOTIMPL;
        ID3D12Resource *named=xs_retain_primary(s->pool_owner,b.pResource);
        if (!named) return E_INVALIDARG;
        BOOL match=named==s->primary;
        ID3D12Resource_Release(named);
        if (!match) return E_INVALIDARG;
        b.LayoutAfter=D3D12_BARRIER_LAYOUT_COMMON;
        s->primary_released=TRUE;
#else
        return E_NOTIMPL;
#endif
    }
#ifdef SCARLETT_PAIR_SUBMIT
    else if(b.LayoutBefore==(D3D12_BARRIER_LAYOUT)0x1f){
        if(issuer!=0x32113 || b.LayoutAfter!=D3D12_BARRIER_LAYOUT_RENDER_TARGET || b.SyncBefore!=D3D12_BARRIER_SYNC_NONE ||
           b.SyncAfter!=D3D12_BARRIER_SYNC_RENDER_TARGET || b.AccessBefore!=D3D12_BARRIER_ACCESS_NO_ACCESS ||
           b.AccessAfter!=D3D12_BARRIER_ACCESS_RENDER_TARGET || s->phase!=B_WAIT || s->alloc_generation ||
           !returned_primary_wait(s) || s->offered || s->host_exposed || s->nops!=7 ||
           memcmp(s->ops,"\x20\x47\x20\x48\x47\x4a\x92",7))return E_NOTIMPL;
        IUnknown *pin=NULL;
        ID3D12Resource *src=xs_acquire_primary(s->pool_owner,b.pResource,&pin);
        if(!src)return E_INVALIDARG;
        HRESULT hr=xs_reuse_common(s->pool_owner,pin,src,NULL,&s->reuse_epoch);
        if(FAILED(hr)){IUnknown_Release(pin);ID3D12Resource_Release(src);return hr;}
        s->primary=src;s->primary_pin=pin;s->primary_key=(UINT_PTR)b.pResource;
        b.LayoutBefore=D3D12_BARRIER_LAYOUT_COMMON;
    }
#endif
    else {
        if (b.SyncBefore!=D3D12_BARRIER_SYNC_ALL_SHADING || b.SyncAfter!=D3D12_BARRIER_SYNC_RENDER_TARGET ||
            b.AccessBefore!=D3D12_BARRIER_ACCESS_UNORDERED_ACCESS || b.AccessAfter!=D3D12_BARRIER_ACCESS_RENDER_TARGET ||
            b.LayoutBefore!=D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS || b.LayoutAfter!=D3D12_BARRIER_LAYOUT_RENDER_TARGET) return E_NOTIMPL;
        if (!(s->primary=xs_acquire_primary(s->pool_owner,b.pResource,&s->primary_pin))) return E_INVALIDARG;
        s->primary_key=(UINT_PTR)b.pResource;
    }
    b.pResource=s->primary;
    g.pTextureBarriers=&b;
    ID3D12GraphicsCommandList7_Barrier(s->host,1,&g);
    return S_OK;
}
#ifdef SCARLETT_PAIR_SUBMIT
#define record_primary_barrier(s,n,g) record_primary_barrier_at(s,n,g,0)
#endif
static void WINAPI sl_primary_barrier(IUnknown *i,UINT count,const D3D12_BARRIER_GROUP *groups)
{
    pair_op(sl(i),0x490);
#ifdef SCARLETT_PAIR_SUBMIT
    if (FAILED(record_primary_barrier_at(sl(i),count,groups,(UINT_PTR)__builtin_return_address(0)-(UINT_PTR)GetModuleHandleW(NULL)))) xs_stop();
#else
    if (FAILED(record_primary_barrier(sl(i),count,groups))) xs_stop();
#endif
    if(!sl(i)->offscreen_sr)batch_step(sl(i),(sl(i)->primary_released?B_RELEASED:B_ACQUIRED));
}
/* shortcut: first full transparent-black clear after this recording's primary transition. */
static HRESULT record_primary_clear(struct startup_list *s,SIZE_T slot,const FLOAT *color,UINT count,const D3D12_RECT *rects)
{
    if (s->table0.heap || s->prior_pso || s->pending_470) return E_ACCESSDENIED;
    if (!s->primary || s->closed || FAILED(s->error) || s->host_exposed || s->primary_cleared || count || rects) return E_NOTIMPL;
    UINT bits[4]; SIZE_T n;
    if (!ReadProcessMemory(GetCurrentProcess(),color,bits,sizeof bits,&n) || n!=sizeof bits) return E_POINTER;
    if (bits[0] || bits[1] || bits[2] || bits[3]) return E_NOTIMPL;
    UINT64 serial=0;
    HRESULT hr=xs_clear_primary_rtv(s->pool_owner,s->host,slot,s->primary,&serial);
    if(SUCCEEDED(hr)){s->clear_slot=slot;s->clear_serial=serial;}
    if (SUCCEEDED(hr)) s->primary_cleared=TRUE;
    return hr;
}
static void WINAPI sl_primary_clear(IUnknown *i,SIZE_T slot,const FLOAT *color,UINT count,const D3D12_RECT *rects)
{
    pair_op(sl(i),0x1a0);
    if (FAILED(record_primary_clear(sl(i),slot,color,count,rects))) xs_stop();
    batch_step(sl(i),B_CLEARED);
}
/* The atlas orders the tail of the second-bank recording, after its original-address wait. */
static BOOL atlas_sync_valid(struct startup_list *s,UINT mask,UINT control,UINT64 address,UINT64 range)
{
    return s->reservation && s->pool_owner && s->type==D3D12_COMMAND_LIST_TYPE_DIRECT && !s->closed &&
        SUCCEEDED(s->error) && !s->host_exposed && !s->primary && s->stamp && s->stamp->second_bank &&
        s->head && s->post && s->post->counter && !s->post_boundary &&
        mask==0xc280 && control==0x20000 && !address && range==0xffffffffff00ull;
}
static void WINAPI pool_sync418(IUnknown *i,UINT mask,UINT control,UINT64 address,UINT64 range)
{
    pair_op(sl(i),0x418);
    struct startup_list *s=sl(i);
    sl_barrier(s,atlas_sync_valid(s,mask,control,address,range));
#ifdef SCARLETT_EXPERIMENTAL_TAGGED_BATCH
    s->phase=B_INELIGIBLE; /* The primary-clear singleton contract has no atlas barrier. */
#endif
}
static BOOL open_direct_state(struct startup_list *s)
{
    return s->pool_owner && s->reservation && s->type==D3D12_COMMAND_LIST_TYPE_DIRECT &&
        !s->closed && SUCCEEDED(s->error) && !s->host_exposed && !s->primary_released;
}
static BOOL can_set_topology(struct startup_list *s,UINT topology)
{
    return open_direct_state(s) && (topology==D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST ||
        (topology==XS_RECTLIST && !s->prior_pso && s->clear4_drew && s->pending_470 && s->clear4_lanes==UINT64_MAX &&
         s->bound && s->required_root && s->graphics_identity==s->required_root));
}
static void recorded_direct_state(struct startup_list *s)
{
    s->post_boundary=FALSE;
#ifdef SCARLETT_EXPERIMENTAL_TAGGED_BATCH
    s->phase=B_INELIGIBLE;
#endif
}
static void WINAPI pool_topology(IUnknown *i,UINT topology)
{
    pair_op(sl(i),0xa0);
    struct startup_list *s=sl(i);
    if (!can_set_topology(s,topology)) xs_stop();
    ID3D12GraphicsCommandList7_IASetPrimitiveTopology(s->host,D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    /* RECTLIST feeds vertex triples to a rectangle GS. #4 has no GS and must reject it. */
    s->guest_topology=topology;
    recorded_direct_state(s);
}
/* shortcut: retain zero without emitting a host mode. Draw/indirect/bundle slots must
 * stay fail-closed until a consumer translates this state for its actual PSO. */
static HRESULT record_depth_order(struct startup_list *s,UINT value)
{
    if (value) return E_NOTIMPL;
    HRESULT hr=E_NOTIMPL;
    AcquireSRWLockExclusive(&s->borrow_lock);
    if (open_direct_state(s)) {
        s->depth_zero_pending=TRUE;
        recorded_direct_state(s);
        hr=S_OK;
    }
    ReleaseSRWLockExclusive(&s->borrow_lock);
    return hr;
}
static void WINAPI pool_depth_order(IUnknown *i,UINT value)
{
    pair_op(sl(i),0x258);
    if (FAILED(record_depth_order(sl(i),value))) xs_stop();
}
static HRESULT record_pool_rate(struct startup_list *s,UINT rate,const D3D12_SHADING_RATE_COMBINER *input)
{
    D3D12_SHADING_RATE_COMBINER pair[2];SIZE_T n;
    if (!open_direct_state(s) || rate!=D3D12_SHADING_RATE_1X1) return E_INVALIDARG;
    if (!input || !ReadProcessMemory(GetCurrentProcess(),input,pair,sizeof pair,&n) || n!=sizeof pair) return E_POINTER;
    if (pair[0]!=D3D12_SHADING_RATE_COMBINER_PASSTHROUGH || pair[1]!=D3D12_SHADING_RATE_COMBINER_PASSTHROUGH)
        return E_NOTIMPL;
    ID3D12GraphicsCommandList7_RSSetShadingRate(s->host,D3D12_SHADING_RATE_1X1,pair);
    recorded_direct_state(s);
    return S_OK;
}
static void WINAPI pool_rate(IUnknown *i,UINT rate,const D3D12_SHADING_RATE_COMBINER *pair)
{
    pair_op(sl(i),0x3f0);
    if (FAILED(record_pool_rate(sl(i),rate,pair))) xs_stop();
}
static HRESULT record_graphics_root(struct startup_list *s,IUnknown *identity)
{
    if(!open_direct_state(s) || !identity)return E_INVALIDARG;
    /* No +c8 is issued when request+38 is null. +110 is still a real root bind. */
    if(s->required_root && identity!=s->required_root && !s->prior_pso && s->bound &&
       s->graphics_identity==s->required_root && s->source.buffer && s->om.heap[1] && s->full_vp &&
       s->pending_470 && s->depth_zero_pending && s->clear4_drew){
        ID3D12RootSignature *root=xs_retain_colour_root(s->pool_owner,identity);
        if(!root)return E_NOTIMPL;
        s->prior_pso=s->bound;s->prior_root=s->graphics_identity;
        s->bound=NULL;s->pipeline=NULL;s->required_root=s->graphics_identity=identity;s->graphics_root=root;
        /* Keep the old logical mask for its recording; the colour root has
         * neither a clear-source argument nor an installed parity PSO. */
        if(s->guest_topology!=XS_RECTLIST)s->guest_topology=0;
    }
    if((s->required_root && s->required_root!=identity) ||
        (s->graphics_identity && s->graphics_identity!=identity))return E_NOTIMPL;
    if(!s->graphics_identity){
        s->graphics_root=xs_retain_graphics_root(s->pool_owner,identity);
        if(!s->graphics_root)return E_INVALIDARG;
        s->graphics_identity=identity;
    }
    if(s->table0.heap && s->table0.root!=identity)memset(&s->table0,0,sizeof s->table0);
    ID3D12GraphicsCommandList7_SetGraphicsRootSignature(s->host,s->graphics_root);
    if(!s->prior_pso && s->source.buffer)bind_source(s->host,&s->source);
    recorded_direct_state(s);return S_OK;
}
#ifdef SCARLETT_PAIR_SUBMIT
#include "carried-rect42.h"
#endif
static void WINAPI pool_root(IUnknown *i,IUnknown *root)
{
    pair_op(sl(i),0x110);
#ifdef SCARLETT_PAIR_SUBMIT
    struct startup_list *s=sl(i);
    if(s->prior_pso && s->bound && s->graphics_identity!=s->required_root){
        if(FAILED(carried_rect42(s,root,(UINT_PTR)__builtin_return_address(0)-(UINT_PTR)GetModuleHandleW(NULL))))xs_stop();
        return;
    }
#endif
    if(FAILED(record_graphics_root(sl(i),root)))xs_stop();
}
static HRESULT record_blend(struct startup_list *s,const FLOAT *input)
{
    FLOAT factor[4]; SIZE_T got;
    if (!open_direct_state(s)) return E_NOTIMPL;
    if (!ReadProcessMemory(GetCurrentProcess(),input,factor,sizeof factor,&got) || got!=sizeof factor) return E_POINTER;
    if (memcmp(factor,ones_factor,sizeof factor)) return E_NOTIMPL;
    ID3D12GraphicsCommandList7_OMSetBlendFactor(s->host,factor);
    s->blend_ones=TRUE;recorded_direct_state(s);return S_OK;
}
static void WINAPI pool_blend(IUnknown *i,const FLOAT *input)
{
    pair_op(sl(i),0xb8);
    if (FAILED(record_blend(sl(i),input))) xs_stop();
}
static HRESULT record_stencil(struct startup_list *s,UINT reference)
{
    if(reference || !open_direct_state(s))return E_NOTIMPL;
    ID3D12GraphicsCommandList7_OMSetStencilRef(s->host,0);
    s->stencil_zero=TRUE;recorded_direct_state(s);return S_OK;
}
static void WINAPI pool_stencil(IUnknown *i,UINT reference)
{
    pair_op(sl(i),0xc0);
    if(FAILED(record_stencil(sl(i),reference)))xs_stop();
}
static HRESULT record_clear4(struct startup_list *s,IUnknown *identity)
{
    HRESULT hr=E_NOTIMPL;
    AcquireSRWLockExclusive(&s->borrow_lock);
#ifdef XS_IM96_FACTORY
    /* This helper is reached through the explicit +c8 setter. Publication alone never changes the list.
     * Keep the guest alias pinned; private host root and PSO are borrowed from the pinned wrapper. */
    if(open_direct_state(s) && s->cache_page && !memcmp(s->cache_page,cache_template,sizeof cache_template) &&
       s->prior_pso && s->guest_topology==XS_RECTLIST && s->required_root &&
       s->graphics_identity==s->required_root && (!s->bound || s->bound==identity)){
        ID3D12RootSignature *private_root=NULL;
        ID3D12PipelineState *p=xs_pin_im96_pso(s->pool_owner,s->device,identity,s->graphics_identity,&private_root);
        if(p){
            if(!s->bound)s->bound=identity;else IUnknown_Release(identity);
            s->pipeline=p;s->graphics_root=private_root;
            ID3D12GraphicsCommandList7_SetGraphicsRootSignature(s->host,private_root);
            ID3D12GraphicsCommandList7_SetPipelineState(s->host,p);recorded_direct_state(s);
            fprintf(stderr,"F96 installed-c8 tid=%lu owner=%p recorder=%p segment=%p alias=%p wrapper=%p privateRoot=%p hostPSO=%p hr=00000000\n",
                    GetCurrentThreadId(),s->pool_owner,s,s->host,s->graphics_identity,s->bound,private_root,p);
            ReleaseSRWLockExclusive(&s->borrow_lock);return S_OK;
        }
    }
#endif
    if(open_direct_state(s) && !s->previous_vb.buffer && s->cache_page && !memcmp(s->cache_page,cache_template,sizeof cache_template) &&
        (!s->bound || s->bound==identity)){
        IUnknown *root=NULL;
        ID3D12PipelineState *p;
        if(s->prior_pso){root=s->graphics_identity;p=xs_pin_colour_pso(s->pool_owner,s->device,identity,root);}
        else p=xs_pin_clear4(s->pool_owner,s->device,identity,&root);
        if(p){
            if(!s->graphics_identity || s->graphics_identity==root){
                if(!s->bound){s->bound=identity;s->pipeline=p;s->required_root=root;}
                else IUnknown_Release(identity);
                ID3D12GraphicsCommandList7_SetPipelineState(s->host,s->pipeline);recorded_direct_state(s);hr=S_OK;
            }else IUnknown_Release(identity);
        }
    }
    ReleaseSRWLockExclusive(&s->borrow_lock);return hr;
}
static void WINAPI pool_pipeline(IUnknown *i,IUnknown *pso)
{
    pair_op(sl(i),0xc8);
    HRESULT hr;
#ifdef SCARLETT_PAIR_SUBMIT
    struct startup_list *s=sl(i);
    if(s->source.needs_publication && s->bound && s->bound!=pso && !s->prior_pso)
        hr=carried_rect42(s,pso,(UINT_PTR)__builtin_return_address(0)-(UINT_PTR)GetModuleHandleW(NULL));
    else
#endif
        hr=record_clear4(sl(i),pso);
    if(FAILED(hr)){
#ifdef XS_IM96_FACTORY
        fprintf(stderr,"PSO refused-c8 tid=%lu owner=%p recorder=%p segment=%p alias=%p input=%p hr=%08lx\n",
                GetCurrentThreadId(),sl(i)->pool_owner,sl(i),sl(i)->host,sl(i)->graphics_identity,pso,(DWORD)hr);
#endif
        xs_stop();
    }
}
static HRESULT unbind_index(struct startup_list *s,const D3D12_INDEX_BUFFER_VIEW *view)
{
    if (!open_direct_state(s) || view) return E_NOTIMPL;
    ID3D12GraphicsCommandList7_IASetIndexBuffer(s->host,NULL);
    recorded_direct_state(s);
    return S_OK;
}
static void WINAPI pool_index(IUnknown *i,const D3D12_INDEX_BUFFER_VIEW *view)
{
    pair_op(sl(i),0x178);
    if (FAILED(unbind_index(sl(i),view))) xs_stop();
}
static HRESULT record_om(struct startup_list *s,UINT count,const SIZE_T *rt,BOOL range,const SIZE_T *ds)
{
#ifdef SCARLETT_PAIR_SUBMIT
    if(s->sr.end){
        SIZE_T at,n,depth=0;
        if(!open_direct_state(s) || !s->pending_470 || s->sr_view || count!=1 || range)return E_NOTIMPL;
        if(s->sr_reversed){
            const struct xs_clear_source *first=&s->previous_vb,*vb=&s->vb;
            if(ds || !s->primary_cleared || !s->primary_pin || s->primary_key!=(UINT_PTR)s->primary_pin || s->output_heap ||
               !s->rt_roundtripped || s->offscreen_sr || s->reuse_epoch!=1 || s->split_at!=5 || s->nops!=35 ||
               s->ops[30]!=0x120/8 || s->ops[32]!=0x180/8 || s->ops[34]!=0x190/8 || !xs_is_rtx3070(s->device) ||
               !s->heaps[0] || s->table_epoch!=1 || s->table0.cells!=1 || s->table0.slot!=372600 ||
               s->table0.heap!=s->heaps[0] || s->table0.root!=s->graphics_identity || s->bound || s->pipeline ||
               !s->clear4_drew || !vb->needs_publication || !vb->buffer || vb->buffer==first->buffer ||
               vb->placed!=first->placed || vb->capture!=first->capture || vb->span.gen!=first->span.gen ||
               vb->span.node!=first->span.node || vb->span.data!=first->span.data || vb->span.bytes!=4080 ||
               vb->span.va-vb->span.data!=0x1df10 || !xs_held_table_inputs(&s->source,first))return E_NOTIMPL;
            for(UINT j=0;j<2;j++)if(!s->om.heap[j] || !s->first_om.heap[j] || s->om.heap[j]==s->first_om.heap[j])return E_NOTIMPL;
            if(!ReadProcessMemory(GetCurrentProcess(),rt,&at,sizeof at,&n) || n!=sizeof at)return E_POINTER;
            if(at!=s->clear_slot)return E_INVALIDARG;
            HRESULT hr=xs_freeze_returned_output(s->pool_owner,s->primary_pin,s->primary,at,s->clear_serial,s->reuse_epoch,
                &s->sr,&s->output_heap,&s->output_cpu);
            if(SUCCEEDED(hr)){
                s->output_slot=at;s->output_serial=s->clear_serial;
                ID3D12GraphicsCommandList7_OMSetRenderTargets(s->host,1,&s->output_cpu,FALSE,NULL);recorded_direct_state(s);
                fprintf(stderr,"scarlett: returned +190 recorder=%p key=%p slot=%zx serial=%llu epoch=%llu originalEnd=%p\n",
                    s->host,s->primary_pin,at,s->output_serial,s->reuse_epoch,s->sr.end);
            }
            return hr;
        }
        BOOL fresh=s->rt_roundtripped;
        if(fresh){
            if(s->split_at!=5 || s->nops!=29 || s->ops[27]!=0x490/8 || s->ops[28]!=0x190/8 ||
               !s->source.needs_publication || !s->vb.needs_publication || !s->om.heap[0] || !s->om.heap[1] ||
               s->first_om.heap[0] || s->first_om.heap[1] || !ds || !same_carried_views(s))return E_NOTIMPL;
        }else if(s->om.heap[0] || s->nops!=19 || s->ops[18]!=0x190/8)return E_NOTIMPL;
        if(!ReadProcessMemory(GetCurrentProcess(),rt,&at,sizeof at,&n) || n!=sizeof at ||
           (ds && (!ReadProcessMemory(GetCurrentProcess(),ds,&depth,sizeof depth,&n) || n!=sizeof depth)))return E_POINTER;
        if(at!=s->sr.rt.slot || depth!=s->sr.ds.slot)return E_INVALIDARG;
        if(depth){
            struct xs_attachment_pin pins[]={s->sr.rt,s->sr.ds};
            struct om_views next={0};
            HRESULT hr=xs_freeze_om(s->pool_owner,pins,(SIZE_T[]){at,depth},&next);
            if(SUCCEEDED(hr)){
                if(fresh)s->first_om=s->om; /* Earlier commands still reference these private heaps. */
                s->om=next;replay_om(&s->om,s->host);recorded_direct_state(s);
            }
            return hr;
        }
        HRESULT hr=xs_sr_view(s->pool_owner,&s->sr,&s->sr_view,&s->sr_cpu);
        if(SUCCEEDED(hr)){ID3D12GraphicsCommandList7_OMSetRenderTargets(s->host,1,&s->sr_cpu,FALSE,NULL);recorded_direct_state(s);}
        return hr;
    }
#endif
    if(!open_direct_state(s) || !s->pending_470 || !s->attachments[0].identity ||
        !s->attachments[1].identity || count!=1 || range)return E_NOTIMPL;
    SIZE_T slots[2],got;
    if(s->offscreen_sr){
        if(ds || s->output_heap || !s->previous_vb.buffer || !s->vb.buffer || !s->rt_roundtripped ||
           !s->clear4_drew || !s->depth_zero_pending || !s->om.heap[0] || !s->om.heap[1] ||
           s->guest_topology!=XS_RECTLIST || !s->prior_pso || s->table0.cells!=1 || s->table0.heap!=s->heaps[0] ||
           s->table0.root!=s->graphics_identity || s->bound || s->pipeline ||
           !s->primary_pin || (UINT_PTR)s->primary_pin!=s->primary_key ||
           s->primary_pin==s->attachments[0].identity || s->primary==s->attachments[0].host ||
           s->primary==s->attachments[1].host)return E_NOTIMPL;
        if(!ReadProcessMemory(GetCurrentProcess(),rt,slots,sizeof *slots,&got) || got!=sizeof *slots)return E_POINTER;
        HRESULT hr=xs_freeze_primary_output(s->pool_owner,s->primary_pin,s->primary,slots[0],
                s->primary_cleared && slots[0]==s->clear_slot?s->clear_serial:0,
                &s->output_heap,&s->output_cpu,&s->output_serial);
        if(SUCCEEDED(hr)){
            s->output_slot=slots[0];
            ID3D12GraphicsCommandList7_OMSetRenderTargets(s->host,1,&s->output_cpu,FALSE,NULL);
            recorded_direct_state(s);
        }
        return hr;
    }
    if(!ReadProcessMemory(GetCurrentProcess(),rt,slots,sizeof *slots,&got) || got!=sizeof *slots ||
       !ReadProcessMemory(GetCurrentProcess(),ds,slots+1,sizeof *slots,&got) || got!=sizeof *slots)return E_POINTER;
    HRESULT hr=xs_freeze_om(s->pool_owner,s->attachments,slots,&s->om);
#ifdef SCARLETT_PAIR_SUBMIT
    if(SUCCEEDED(hr) && !s->first_om.heap[0]){
        s->first_om=s->om;
        for(UINT j=0;j<2;j++)ID3D12DescriptorHeap_AddRef(s->first_om.heap[j]);
    }
#endif
    if(SUCCEEDED(hr)){replay_om(&s->om,s->host);recorded_direct_state(s);}return hr;
}
static void WINAPI pool_om(IUnknown *i,UINT count,const SIZE_T *rt,BOOL range,const SIZE_T *ds)
{
    pair_op(sl(i),0x190);
#ifdef SCARLETT_PAIR_SUBMIT
    BOOL wrong_issuer=sl(i)->sr.ds.identity && (UINT_PTR)__builtin_return_address(0)!=(UINT_PTR)GetModuleHandleW(NULL)+0x33317;
    HRESULT hr=wrong_issuer?E_NOTIMPL:record_om(sl(i),count,rt,range,ds);
#else
    HRESULT hr=record_om(sl(i),count,rt,range,ds);
#endif
    if(FAILED(hr)){
#ifdef SCARLETT_PAIR_SUBMIT
        if(sl(i)->sr.end){
            SIZE_T r=0,d=0,n=0;
            BOOL ok=count && ReadProcessMemory(GetCurrentProcess(),rt,&r,sizeof r,&n) && n==sizeof r;
            BOOL dok=!ds || (ReadProcessMemory(GetCurrentProcess(),ds,&d,sizeof d,&n) && n==sizeof d);
            if(sl(i)->rt_roundtripped && sl(i)->source.needs_publication)
                fprintf(stderr,"TRIP190 refusal end=%p nops=%u live=%d expectRT=%p/%p/%zx/%llu expectDS=%p/%p/%zx/%llu handlesMatch=%d\n",
                    sl(i)->sr.end,sl(i)->nops,same_carried_views(sl(i)),sl(i)->sr.rt.identity,sl(i)->sr.rt.host,sl(i)->sr.rt.slot,sl(i)->sr.rt.serial,
                    sl(i)->sr.ds.identity,sl(i)->sr.ds.host,sl(i)->sr.ds.slot,sl(i)->sr.ds.serial,count==1 && !range && ok && dok && r==sl(i)->sr.rt.slot && d==sl(i)->sr.ds.slot);
            fprintf(stderr,"SR190 refused issuer=%zx list=%p count=%u range=%u rt=%d:%zx ds=%p/%d:%zx expected=%zx/%llu hr=%lx\n",
                (UINT_PTR)__builtin_return_address(0)-(UINT_PTR)GetModuleHandleW(NULL),i,count,range,ok,r,ds,dok,d,sl(i)->sr.rt.slot,sl(i)->sr.rt.serial,hr);
        }
#endif
        xs_stop();
    }
}
static HRESULT record_bounds(struct startup_list *s,FLOAT lo,FLOAT hi)
{
    const FLOAT full[]={0.f,1.f};
    if(!open_direct_state(s) || !s->pending_470 || !s->om.heap[1] ||
       memcmp(&lo,full,sizeof lo) || memcmp(&hi,full+1,sizeof hi))return E_NOTIMPL;
    D3D12_FEATURE_DATA_D3D12_OPTIONS2 opts;
    HRESULT hr=ID3D12Device_CheckFeatureSupport(s->device,D3D12_FEATURE_D3D12_OPTIONS2,&opts,sizeof opts);
    if(FAILED(hr))return hr;
    if(!opts.DepthBoundsTestSupported)return DXGI_ERROR_UNSUPPORTED;
    ID3D12GraphicsCommandList7_OMSetDepthBounds(s->host,lo,hi);
    s->full_bounds=TRUE;recorded_direct_state(s);return S_OK;
}
static void WINAPI pool_bounds(IUnknown *i,FLOAT lo,FLOAT hi)
{
    pair_op(sl(i),0x230);
    if(FAILED(record_bounds(sl(i),lo,hi)))xs_stop();
}
/* shortcut: only the observed single full-target rectangle and viewport on the #4 path. */
static HRESULT record_clear_raster(struct startup_list *s,UINT count,const void *input,BOOL viewport)
{
    if(!open_direct_state(s) || !s->pending_470 || !s->full_bounds || count!=1 || (viewport && !s->full_rect))return E_NOTIMPL;
    BYTE data[sizeof clear_viewport];SIZE_T n=viewport?sizeof clear_viewport:sizeof clear_rect,got;
    if(!ReadProcessMemory(GetCurrentProcess(),input,data,n,&got) || got!=n)return E_POINTER;
    if(memcmp(data,viewport?(const void*)&clear_viewport:(const void*)&clear_rect,n))return E_NOTIMPL;
    if(viewport){ID3D12GraphicsCommandList7_RSSetViewports(s->host,1,&clear_viewport);s->full_vp=TRUE;}
    else {ID3D12GraphicsCommandList7_RSSetScissorRects(s->host,1,&clear_rect);s->full_rect=TRUE;}
    recorded_direct_state(s);return S_OK;
}
static void WINAPI pool_rect(IUnknown *i,UINT n,const D3D12_RECT *r){
    pair_op(sl(i),0xb0);if(FAILED(record_clear_raster(sl(i),n,r,FALSE)))xs_stop();}
static void WINAPI pool_vp(IUnknown *i,UINT n,const D3D12_VIEWPORT *v){
    pair_op(sl(i),0xa8);if(FAILED(record_clear_raster(sl(i),n,v,TRUE)))xs_stop();}
#ifdef SCARLETT_PAIR_SUBMIT
static BOOL carried_clear_split(const struct startup_list *s)
{
    if(s->marker && s->marker->buffer)
        return s->split_at==6 && returned_primary_wait(s) && s->post->threshold==1;
    return s->split_at==5;
}
#endif
static HRESULT record_source(struct startup_list *s,UINT index,UINT64 va)
{
    if(!open_direct_state(s) || s->prior_pso || index || !s->full_vp || !s->pending_470 || !s->depth_zero_pending ||
        !s->om.heap[1] || !s->bound || !s->required_root || s->graphics_identity!=s->required_root || s->source.buffer)return E_NOTIMPL;
    HRESULT hr;
#ifdef SCARLETT_PAIR_SUBMIT
    if(s->sr.end){
        if(!s->reuse_epoch || !s->sr.ds.identity || s->sr_reversed || !s->om.heap[0] ||
           !carried_clear_split(s) ||
           s->nops!=23 || memcmp(s->ops+18,"\x32\x46\x16\x15\x2c",5))return E_NOTIMPL;
        hr=xs_copy_restored_clear(s->pool_owner,s->device,s->host,va,&s->source);
        if(SUCCEEDED(hr))fprintf(stderr,"scarlett: restored +160 recorded host=%p placed=%p capture=%p node=%zx gen=%u bytes=%u pending publication\n",
            s->host,s->source.placed,s->source.capture,s->source.span.node,s->source.span.gen,s->source.span.bytes);
    }else
#endif
    hr=xs_copy_clear_source(s->pool_owner,s->device,s->host,va,&s->source);
    if(SUCCEEDED(hr)){bind_source(s->host,&s->source);recorded_direct_state(s);}return hr;
}
static void WINAPI pool_source(IUnknown *i,UINT index,UINT64 va)
{
    pair_op(sl(i),0x160);
    if(FAILED(record_source(sl(i),index,va)))xs_stop();
}
/* +258(0) remains logical state. Only this digest-qualified pair has disabled
 * depth/bounds/stencil, no PS memory effects, and no depth or stencil output. */
static HRESULT record_clear4_draw(struct startup_list *s,UINT n,UINT instances,UINT first,UINT base)
{
    const struct xs_attachment_pin *rt=&s->attachments[0],*ds=&s->attachments[1];
#ifdef SCARLETT_PAIR_SUBMIT
    if(s->source.needs_publication){
        const struct lease_span *v=&s->source.span;
        if(!s->sr.end || s->sr_reversed || !s->reuse_epoch || rt->identity || ds->identity ||
           !s->source.capture || v->bytes!=100 || !v->gen || v->va-v->arena.base!=0x5ff80 ||
           v->va-v->data!=0x1ff80 || v->holds!=2 || v->stamp!=~1ull ||
           s->clear4_lanes!=UINT64_MAX || s->clear4_drew || !carried_clear_split(s) || s->nops!=24 ||
           memcmp(s->ops+18,"\x32\x46\x16\x15\x2c\x0c",6))return E_NOTIMPL;
        rt=&s->sr.rt;ds=&s->sr.ds;
        if(!rt->host || !ds->host || rt->host==ds->host || !rt->serial || !ds->serial ||
           !s->om.cpu[0].ptr || !s->om.cpu[1].ptr)return E_INVALIDARG;
    }
#endif
    if(n!=3 || instances!=1 || first || base || s->prior_pso || !open_direct_state(s) || !s->depth_zero_pending ||
       !s->pending_470 || s->guest_topology!=D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST || !s->source.buffer || !s->source.placed ||
       !s->om.heap[0] || !s->om.heap[1] || !rt->identity || !ds->identity ||
       !s->full_bounds || !s->full_rect || !s->full_vp || !s->stencil_zero || !s->blend_ones ||
       !s->graphics_root || !s->required_root || s->graphics_identity!=s->required_root)return E_NOTIMPL;
    IUnknown *root=NULL;
    if(!xs_pin_clear4(s->pool_owner,s->device,s->bound,&root))return E_NOTIMPL;
    BOOL match=root==s->required_root && s->pipeline && s->pipeline==xs_clear4_effective(s->bound,s->clear4_lanes);
    IUnknown_Release(s->bound);
    if(!match)return E_NOTIMPL;
    ID3D12GraphicsCommandList7_DrawInstanced(s->host,n,instances,first,base);
    s->clear4_drew=TRUE;
    recorded_direct_state(s);
    fprintf(stderr,"scarlett: recorded #4 DrawInstanced(3,1,0,0) recorder=%p host=%p lanes=%016llx RTV/DSV=%llu/%llu\n",
            s,s->host,s->clear4_lanes,rt->serial,ds->serial);
    return S_OK;
}
static void WINAPI pool_clear4_draw(IUnknown *i,UINT n,UINT instances,UINT first,UINT base)
{
    pair_op(sl(i),0x60);
    struct startup_list *s=sl(i);
#ifdef SCARLETT_PAIR_SUBMIT
    if(s->source.needs_publication && (UINT_PTR)__builtin_return_address(0)!=(UINT_PTR)GetModuleHandleW(NULL)+0xbff06)xs_stop();
#endif
    if(FAILED(record_clear4_draw(s,n,instances,first,base)))xs_stop();
}
/* Initial mode-15 inputs are flushed with the previous draw pinned, before explicit +c8. */
static BOOL pending_colour_inputs(struct startup_list *s)
{
    return open_direct_state(s) && !s->previous_vb.buffer && s->clear4_drew && s->prior_pso && s->prior_root && s->pending_470 &&
        s->guest_topology==XS_RECTLIST && s->graphics_root && s->graphics_identity==s->required_root &&
        s->source.placed && !s->bound && !s->pipeline;
}
static HRESULT record_colour_vb(struct startup_list *s,UINT slot,UINT count,const D3D12_VERTEX_BUFFER_VIEW *input)
{
    BOOL replacement=s->vb.buffer!=NULL;
    UINT table_slot=368504;
#ifdef SCARLETT_PAIR_SUBMIT
    if(s->source.needs_publication){
        if(!s->sr.end || s->sr_reversed || !s->sr.ds.identity || s->reuse_epoch!=1 || !carried_clear_split(s))return E_NOTIMPL;
        if(s->split_at==6 && (replacement || s->table0.heap || s->table0.cells || !s->sr.rt.identity ||
           !s->om.heap[0] || !s->om.heap[1] || !s->source.buffer))return E_NOTIMPL;
        if(replacement){
            table_slot=372600;
            if(s->nops!=33 || s->ops[30]!=0x120/8 || s->ops[32]!=0x180/8 || s->table_epoch!=1 ||
               !s->heaps[0] || !s->om.heap[0] || !s->om.heap[1] || !s->first_om.heap[0] || !s->first_om.heap[1] ||
               s->first_om.heap[0]==s->om.heap[0] || s->first_om.heap[1]==s->om.heap[1] ||
               !same_carried_views(s))return E_NOTIMPL;
        }else if(s->table_epoch || s->rt_roundtripped || s->nops!=27 || memcmp(s->ops+24,"\x14\x22\x30",3))return E_NOTIMPL;
    }
#endif
    if(!pending_colour_inputs(s)||slot||count!=1)return E_NOTIMPL;
    if(replacement && (!s->rt_roundtripped || s->table0.cells!=1 || s->table0.heap!=s->heaps[0] ||
        s->table0.slot!=table_slot || s->table0.root!=s->graphics_identity || s->vb.placed!=s->source.placed))return E_NOTIMPL;
    D3D12_VERTEX_BUFFER_VIEW v;SIZE_T got;if(!ReadProcessMemory(GetCurrentProcess(),input,&v,sizeof v,&got)||got!=sizeof v)return E_POINTER;
    if(v.SizeInBytes!=4080 || v.StrideInBytes!=24)return E_NOTIMPL;
    struct xs_clear_source next={0};
    HRESULT hr;
#ifdef SCARLETT_PAIR_SUBMIT
    if(s->source.needs_publication)
        hr=replacement?xs_copy_carried_replacement(s->pool_owner,s->device,s->host,v.BufferLocation,&s->source,&s->vb,&next):
                xs_copy_restored_first_vb(s->pool_owner,s->device,s->host,v.BufferLocation,&s->source,&next);
    else
#endif
        hr=xs_copy_colour_vb(s->pool_owner,s->device,s->host,v.BufferLocation,replacement,s->source.placed,&next);
    if(FAILED(hr))return hr;
    s->previous_vb=s->vb;s->vb=next;
    bind_colour_vb(s->host,&s->vb);recorded_direct_state(s);
#ifdef SCARLETT_PAIR_SUBMIT
    if(s->source.needs_publication && s->split_at==6)
        fprintf(stderr,"scarlett: native first +180 recorded segment=%p placed=%p capture=%p node=%zx gen=%u bytes=%u pending publication\n",
                s->host,next.placed,next.capture,next.span.node,next.span.gen,next.span.bytes);
#endif
    return S_OK;
}
static void WINAPI pool_colour_vb(IUnknown *i,UINT slot,UINT count,const D3D12_VERTEX_BUFFER_VIEW *v)
{
    pair_op(sl(i),0x180);
#ifdef SCARLETT_PAIR_SUBMIT
    if(sl(i)->source.needs_publication && (UINT_PTR)__builtin_return_address(0)!=(UINT_PTR)GetModuleHandleW(NULL)+0x32966)xs_stop();
#endif
    if(FAILED(record_colour_vb(sl(i),slot,count,v)))xs_stop();
}
static HRESULT record_table0(struct startup_list *s,UINT index,UINT64 handle)
{
#ifdef SCARLETT_PAIR_SUBMIT
    if(s->source.needs_publication && (!s->sr.end || s->sr_reversed || s->reuse_epoch!=1 || s->split_at!=5 ||
       s->nops!=31 || s->ops[30]!=0x120/8 || s->previous_vb.buffer || s->table_epoch || s->table0.heap ||
       !s->om.heap[0] || !s->om.heap[1] || !s->first_om.heap[0] || !s->first_om.heap[1] ||
       s->first_om.heap[0]==s->om.heap[0] || s->first_om.heap[1]==s->om.heap[1] ||
       !xs_held_table_inputs(&s->source,&s->vb)))return E_NOTIMPL;
#endif
    if(index || !s->rt_roundtripped || !s->vb.buffer || s->vb.placed!=s->source.placed ||
       !s->heaps[0] || s->table_epoch==UINT64_MAX)return E_NOTIMPL;
    UINT cells=1;
    if(s->previous_vb.buffer){
        /* shortcut: the observed pending three-cell flush after the primary-only OM bind. */
        if(!open_direct_state(s) || !s->output_heap || !s->offscreen_sr ||
           s->guest_topology!=XS_RECTLIST || s->pipeline || s->bound || !s->prior_pso ||
           s->table0.heap!=s->heaps[0] || s->table0.root!=s->graphics_identity ||
           s->graphics_identity!=s->required_root)return E_NOTIMPL;
        cells=3;
    }else if(!pending_colour_inputs(s))return E_NOTIMPL;
    UINT slot;HRESULT hr=xs_active_table_slot(s->pool_owner,s->heaps[0],handle,cells,&slot);
    if(SUCCEEDED(hr)){
        if(cells==3 && s->table0.cells==1)s->prior_b27_slot=s->table0.slot;
        s->table0=(__typeof__(s->table0)){s->heaps[0],s->graphics_identity,slot,cells};
        ++s->table_epoch;
        recorded_direct_state(s);
    }
    return hr;
}
static void WINAPI pool_b27(IUnknown *i,UINT index,UINT64 handle)
{
    pair_op(sl(i),0x120);
#ifdef SCARLETT_PAIR_SUBMIT
    if(sl(i)->source.needs_publication && (UINT_PTR)__builtin_return_address(0)!=(UINT_PTR)GetModuleHandleW(NULL)+0x32206)xs_stop();
#endif
    if(FAILED(record_table0(sl(i),index,handle)))xs_stop();
}
/* Only these slots have been traced on the pool's returned pointer. */
static void WINAPI pool_470(IUnknown *,UINT64);
static const struct {
    IUnknownVtbl prefix;
    xs_stop_fn gap18[6];
    __typeof__(&sl_close) close;
    xs_stop_fn gap50[2]; __typeof__(&pool_clear4_draw) draw; xs_stop_fn gap68[7];
    __typeof__(&pool_topology) topology;
    __typeof__(&pool_vp) vp;__typeof__(&pool_rect) rect; __typeof__(&pool_blend) blend; __typeof__(&pool_stencil) stencil; __typeof__(&pool_pipeline) pipeline; xs_stop_fn gapd0[6];
    __typeof__(&sl_bind) bind;
    xs_stop_fn gap108; __typeof__(&pool_root) root; xs_stop_fn gap118; __typeof__(&pool_b27) b27; xs_stop_fn gap128[7]; __typeof__(&pool_source) source; xs_stop_fn gap168[2];
    __typeof__(&pool_index) index; __typeof__(&pool_colour_vb) vb;xs_stop_fn gap188;__typeof__(&pool_om) om;xs_stop_fn gap198;
    __typeof__(&sl_primary_clear) clear;
    xs_stop_fn gap1a8[17];__typeof__(&pool_bounds) bounds;
    __typeof__(&sl_marker) marker;
    __typeof__(&sl_timestamp) timestamp;
    xs_stop_fn gap248;
    __typeof__(&sl_wait64) wait64;
    __typeof__(&pool_depth_order) depth_order;
    xs_stop_fn gap260[50];
    __typeof__(&pool_rate) rate;
    xs_stop_fn gap3f8[4];
    __typeof__(&pool_sync418) sync418;
    xs_stop_fn gap420[7];
    __typeof__(&sl_reset) reset;
    xs_stop_fn gap460[2]; __typeof__(&pool_470) state470; xs_stop_fn gap478[3];
    __typeof__(&sl_primary_barrier) barrier;
} pool_vtbl = {
    {sl_query,sl_addref,sl_release}, {[0 ... 5]=xs_stop}, sl_close, {xs_stop,xs_stop},pool_clear4_draw,{[0 ... 6]=xs_stop}, pool_topology,pool_vp,pool_rect,pool_blend,pool_stencil,pool_pipeline,{[0 ... 5]=xs_stop}, sl_bind, xs_stop, pool_root,xs_stop,pool_b27,{[0 ... 6]=xs_stop},pool_source,{xs_stop,xs_stop},pool_index,pool_colour_vb,xs_stop,pool_om,xs_stop, sl_primary_clear, {[0 ... 16]=xs_stop},pool_bounds, sl_marker, sl_timestamp, xs_stop, sl_wait64, pool_depth_order, {[0 ... 49]=xs_stop}, pool_rate, {[0 ... 3]=xs_stop}, pool_sync418, {[0 ... 6]=xs_stop}, sl_reset, {xs_stop,xs_stop}, pool_470, {[0 ... 2]=xs_stop}, sl_primary_barrier
};
_Static_assert(offsetof(__typeof__(pool_vtbl),b27)==0x120 && offsetof(__typeof__(pool_vtbl),vb)==0x180,"colour inputs ABI");
_Static_assert(offsetof(__typeof__(pool_vtbl),draw)==0x60,"#4 draw ABI");
_Static_assert(offsetof(__typeof__(pool_vtbl),blend)==0xb8,"blend factor ABI");
_Static_assert(offsetof(__typeof__(pool_vtbl),om)==0x190,"OM ABI");
_Static_assert(offsetof(__typeof__(pool_vtbl),state470)==0x470,"pooled opaque state ABI");
_Static_assert(offsetof(__typeof__(pool_vtbl),vp)==0xa8 && offsetof(__typeof__(pool_vtbl),rect)==0xb0,"raster ABI");
_Static_assert(offsetof(__typeof__(pool_vtbl),source)==0x160,"clear vertex source ABI");
_Static_assert(offsetof(__typeof__(pool_vtbl),bounds)==0x230,"depth bounds ABI");
_Static_assert(offsetof(__typeof__(pool_vtbl),stencil)==0xc0,"stencil reference ABI");
_Static_assert(offsetof(__typeof__(pool_vtbl),pipeline)==0xc8,"pooled PSO ABI");
_Static_assert(offsetof(__typeof__(pool_vtbl),root)==0x110,"pooled graphics root ABI");
_Static_assert(offsetof(__typeof__(pool_vtbl),index)==0x178,"index buffer ABI");
_Static_assert(offsetof(__typeof__(pool_vtbl),depth_order)==0x258,"pooled depth-order ABI");
_Static_assert(offsetof(__typeof__(pool_vtbl),rate)==0x3f0,"pooled shading rate ABI");
_Static_assert(offsetof(__typeof__(pool_vtbl),topology)==0xa0,"pooled topology ABI");
_Static_assert(offsetof(__typeof__(pool_vtbl),sync418)==0x418,"pooled sync ABI");
_Static_assert(offsetof(__typeof__(pool_vtbl),wait64)==0x250,"pooled wait ABI");
_Static_assert(offsetof(__typeof__(pool_vtbl),clear)==0x1a0,"pooled clear ABI");
_Static_assert(offsetof(__typeof__(pool_vtbl),barrier)==0x490, "pooled barrier ABI");
_Static_assert(offsetof(__typeof__(pool_vtbl),close)==0x48 && offsetof(__typeof__(pool_vtbl),bind)==0x100 && offsetof(__typeof__(pool_vtbl),marker)==0x238 && offsetof(__typeof__(pool_vtbl),timestamp)==0x240 && offsetof(__typeof__(pool_vtbl),reset)==0x458,
        "pooled list ABI");

/* shortcut: exactly full coverage and the diagonal discriminator on #4.
 * The wrapper pins both PSOs; no root change or invalidated guest binding. */
static HRESULT record_470(IUnknown *i,UINT64 value)
{
    if (!i || i->lpVtbl!=&pool_vtbl.prefix) return E_NOTIMPL;
    struct startup_list *s=sl(i);
    HRESULT hr=E_NOTIMPL;
    AcquireSRWLockExclusive(&s->borrow_lock);
    /* record_clear4 already pins bound for this exact pool owner and host device. */
    if (open_direct_state(s) && !s->prior_pso && s->bound && s->required_root && s->graphics_identity==s->required_root) {
        ID3D12PipelineState *effective=xs_clear4_effective(s->bound,value);
        if(effective){
            ID3D12GraphicsCommandList7_SetPipelineState(s->host,effective);
            s->pipeline=effective;s->clear4_lanes=value;s->pending_470=TRUE;
            recorded_direct_state(s);hr=S_OK;
        }
    }
    ReleaseSRWLockExclusive(&s->borrow_lock);
    return hr;
}
static void WINAPI pool_470(IUnknown *i,UINT64 value)
{
    pair_op(sl(i),0x470);
    if (FAILED(record_470(i,value))) xs_stop();
}

/* Checked counterpart of the void guest slot. */
HRESULT xs_bind_pool_heaps(IUnknown *i,UINT count,IUnknown *const *heaps)
{
    if (!i || i->lpVtbl!=&pool_vtbl.prefix) return E_INVALIDARG;
    return bind_initial_heaps(sl(i),count,heaps);
}

HRESULT xs_device_list(ID3D12Device *device, D3D12_COMMAND_LIST_TYPE type, IUnknown *alloc, BOOL pooled,IUnknown *pool_owner, IUnknown **out)
{
    if (!out) return E_POINTER;
    *out = NULL;
    ID3D12CommandAllocator *a = xs_allocator_host(alloc, type);
    if (!device || !allocator_matches(a, device)) return E_INVALIDARG;
    D3D12_FEATURE_DATA_D3D12_OPTIONS12 options;
    HRESULT hr = ID3D12Device_CheckFeatureSupport(device, D3D12_FEATURE_D3D12_OPTIONS12, &options, sizeof(options));
    if (FAILED(hr)) return hr;
    if (!options.EnhancedBarriersSupported) return DXGI_ERROR_UNSUPPORTED;
    struct startup_list *s;
    struct list_face *face;
    if (pooled) {
        /* A read-only zero-capacity prefix selects typed methods. Guard everything past it. */
        SYSTEM_INFO info;
        GetSystemInfo(&info);
        SIZE_T page=info.dwPageSize;
        BOOL cache=type==D3D12_COMMAND_LIST_TYPE_DIRECT && pool_owner;
        if(cache && page!=sizeof cache_template)return E_NOTIMPL;
        BYTE *base = VirtualAlloc(NULL, (cache?4:3) * page, MEM_RESERVE, PAGE_NOACCESS);
        if (!base) return E_OUTOFMEMORY;
        if (!VirtualAlloc(base, (cache?3:2) * page, MEM_COMMIT, PAGE_READWRITE)) {
            VirtualFree(base,0,MEM_RELEASE);
            return E_OUTOFMEMORY;
        }
        s = (struct startup_list *)base;
        s->reservation = base;
        SIZE_T prefix=type==D3D12_COMMAND_LIST_TYPE_DIRECT?sizeof(struct direct_prefix):sizeof(IUnknown);
        if(cache){s->cache_page=base+2*page;memcpy(s->cache_page,cache_template,page);prefix=0x540;}
        face=(struct list_face *)(base+2*page-prefix - offsetof(struct list_face,iface));
        face->state=s;
        face->iface.lpVtbl=&pool_vtbl.prefix;
        DWORD old;
        if (!VirtualProtect(base+page,page,PAGE_READONLY,&old)) {
            hr=HRESULT_FROM_WIN32(GetLastError());sl_free(s);return hr;
        }
    } else {
        s = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*s));
        if (!s) return E_OUTOFMEMORY;
        face=&s->local;
        face->state=s;
        face->iface.lpVtbl=&sl_vtbl.prefix;
    }
    hr = ID3D12Device_CreateCommandList(device, 1, type, a, NULL, &IID_ID3D12GraphicsCommandList7, (void **)&s->host);
    if (FAILED(hr)) { sl_free(s); return hr; }
    if (pooled && (s->pool_owner=pool_owner)) IUnknown_AddRef(pool_owner);
#ifdef SCARLETT_EXPERIMENTAL_TAGGED_BATCH
    if (!pooled && (s->sync_owner=pool_owner)) IUnknown_AddRef(pool_owner);
#endif
#ifdef SCARLETT_PAIR_SUBMIT
    s->alloc_generation=InterlockedCompareExchange64(&CONTAINING_RECORD(alloc,struct command_allocator,a.iface)->generation,0,0);
#endif
    s->refs = 1;
    s->device = device;
    s->type = type;
    IUnknown_AddRef(alloc);
    s->allocator = alloc;
    *out = &face->iface;
    return S_OK;
}
HRESULT xs_startup_list(ID3D12Device *device,D3D12_COMMAND_LIST_TYPE type,IUnknown *alloc,BOOL pooled,IUnknown **out)
{
    return xs_device_list(device,type,alloc,pooled,NULL,out);
}
ID3D12GraphicsCommandList7 *xs_list_host(IUnknown *i)
{
    if (!i || (i->lpVtbl!=&sl_vtbl.prefix && i->lpVtbl!=&pool_vtbl.prefix)) return NULL;
    struct startup_list *s=sl(i);
    ID3D12GraphicsCommandList7 *host=NULL;
    AcquireSRWLockExclusive(&s->borrow_lock);
    if (!s->prior_pso && !s->primary_released && !s->depth_zero_pending && !s->pending_470 && !s->bound
#ifdef SCARLETT_PAIR_SUBMIT
        && !s->source.needs_publication
#endif
#ifdef SCARLETT_EXPERIMENTAL_TAGGED_BATCH
        && !s->deferred
#endif
#ifdef XS_TEST_ORDERED_QUEUE
        && !(s->single_use && s->closed)
#endif
#ifdef SCARLETT_EXPERIMENTAL_BRIDGE
        && escape_native()
#endif
        ) {
        s->host_exposed=TRUE;
        host=s->host;
    }
    ReleaseSRWLockExclusive(&s->borrow_lock);
    return host;
}

/* A list may be reset or released as soon as it is submitted. Pin this
 * recording's guest heaps independently until this queue passes the list. */
struct heap_retirement { struct xs_clear_source previous_vb,vb,source; IUnknown *bound; ID3D12RootSignature *root; ID3D12Resource *primary; ID3D12CommandAllocator *tail_alloc; IUnknown *head_alloc; struct prefix_marker *native; struct system_timestamp *stamp; IUnknown *pair[2]; ID3D12Fence *fence,*gate; HANDLE event;
    ID3D12DescriptorHeap *output_heap;IUnknown *output_key;
#ifdef SCARLETT_PAIR_SUBMIT
    BOOL reuse_guard;
    struct om_views sr_om,sr_first_om;
    struct xs_sr_ticket sr;
    ID3D12DescriptorHeap *sr_view;
#endif
#ifdef SCARLETT_EXPERIMENTAL_TAGGED_BATCH
    ID3D12GraphicsCommandList7 *tag_head,*tag_tail;
    ID3D12Fence *prefix_gate;
    struct prefix_marker *tag_post;
    IUnknown *tag_queue;
#endif
};
static void CALLBACK retire_heaps(PTP_CALLBACK_INSTANCE instance,void *ctx,PTP_WAIT wait,TP_WAIT_RESULT result)
{
    struct heap_retirement *r=ctx;
    (void)instance;(void)result;
    CloseThreadpoolWait(wait);
#ifdef SCARLETT_PAIR_SUBMIT
    if(r->reuse_guard)InterlockedDecrement(&pair_inflight);
    if(r->sr.end)ID3D12Fence_Release(r->sr.end);
    if(r->sr.rt.identity)IUnknown_Release(r->sr.rt.identity);
    if(r->sr.ds.identity)IUnknown_Release(r->sr.ds.identity);
    drop_om(&r->sr_om);
    drop_om(&r->sr_first_om);
    if(r->sr_view)ID3D12DescriptorHeap_Release(r->sr_view);
#endif
#ifdef SCARLETT_EXPERIMENTAL_TAGGED_BATCH
    if (r->tag_tail) {
        fprintf(stderr,"scarlett: tagged singleton %s end=%p completed=%llu\n",r->tag_head?"batch":"sync",
                r->fence,ID3D12Fence_GetCompletedValue(r->fence));
        struct command_allocator *a=CONTAINING_RECORD(r->head_alloc,struct command_allocator,a.iface);
        AcquireSRWLockExclusive(&a->lock);a->submission_pins--;ReleaseSRWLockExclusive(&a->lock);
        if (r->tag_head) ID3D12GraphicsCommandList7_Release(r->tag_head);
        ID3D12GraphicsCommandList7_Release(r->tag_tail);
        if (r->prefix_gate) ID3D12Fence_Release(r->prefix_gate);
        drop_marker(r->tag_post);
        IUnknown_Release(r->tag_queue);
    }
#endif
    drop_source(&r->previous_vb);drop_source(&r->vb);drop_source(&r->source);
    if(r->bound)IUnknown_Release(r->bound);
    if(r->root)ID3D12RootSignature_Release(r->root);
    if (r->primary) ID3D12Resource_Release(r->primary);
    if(r->output_heap)ID3D12DescriptorHeap_Release(r->output_heap);
    if(r->output_key)IUnknown_Release(r->output_key);
    if (r->head_alloc) IUnknown_Release(r->head_alloc);
    if (r->tail_alloc) ID3D12CommandAllocator_Release(r->tail_alloc);
    if (r->gate) ID3D12Fence_Release(r->gate);
    drop_timestamp(r->stamp); drop_marker(r->native); drop_heaps(r->pair); ID3D12Fence_Release(r->fence); CloseHandle(r->event); HeapFree(GetProcessHeap(),0,r);
}
static struct xs_clear_source retain_source(struct xs_clear_source p)
{
    if(p.placed)IUnknown_AddRef(p.placed);
    if(p.buffer)ID3D12Resource_AddRef(p.buffer);
    return p;
}
static void retain_recording(struct heap_retirement *r,struct startup_list *s)
{
#ifdef SCARLETT_PAIR_SUBMIT
    if((r->sr=s->sr).end){ID3D12Fence_AddRef(r->sr.end);IUnknown_AddRef(r->sr.rt.identity);}
    if(r->sr.ds.identity){
        IUnknown_AddRef(r->sr.ds.identity);r->sr_om=s->om;
        r->sr_first_om=s->first_om;
        for(UINT i=0;i<2;i++){
            if(r->sr_om.heap[i])ID3D12DescriptorHeap_AddRef(r->sr_om.heap[i]);
            if(r->sr_first_om.heap[i])ID3D12DescriptorHeap_AddRef(r->sr_first_om.heap[i]);
        }
    }
    if((r->sr_view=s->sr_view))ID3D12DescriptorHeap_AddRef(r->sr_view);
#endif
    if (s->marker && s->marker->buffer) {r->native=s->marker;InterlockedIncrement(&r->native->refs);}
    r->source=retain_source(s->source);r->vb=retain_source(s->vb);r->previous_vb=retain_source(s->previous_vb);
    if ((r->bound=s->bound))IUnknown_AddRef(r->bound);
    if ((r->root=s->graphics_root))ID3D12RootSignature_AddRef(r->root);
    if ((r->primary=s->primary)) ID3D12Resource_AddRef(r->primary);
    if((r->output_heap=s->output_heap)){ID3D12DescriptorHeap_AddRef(r->output_heap);r->output_key=s->primary_pin;IUnknown_AddRef(r->output_key);}
    if (s->post || s->primary) {r->head_alloc=s->allocator;IUnknown_AddRef(r->head_alloc);}
    if (s->post) {r->tail_alloc=s->tail_allocator;ID3D12CommandAllocator_AddRef(r->tail_alloc);}
    if ((r->stamp=s->stamp)) InterlockedIncrement(&r->stamp->refs);
    for (unsigned i=0;i<2;i++) if ((r->pair[i]=s->heaps[i])) IUnknown_AddRef(r->pair[i]);
}
static void execute_recording(ID3D12CommandQueue *q,struct startup_list *s,struct heap_retirement *r)
{
    ID3D12Fence **prefix_gate=NULL;
#ifdef SCARLETT_EXPERIMENTAL_TAGGED_BATCH
    if (r && r->tag_tail) prefix_gate=&r->prefix_gate;
#endif
    if (s->marker && s->marker->alias) enqueue_marker(q,s->device,s->marker,prefix_gate);
    if (s->post) {
        ID3D12CommandList *head=(ID3D12CommandList *)s->head;
        ID3D12CommandQueue_ExecuteCommandLists(q,1,&head);
        enqueue_marker(q,s->device,s->post,s->post->counter?&r->gate:NULL);
    }
    ID3D12CommandList *tail=(ID3D12CommandList *)s->host;
    ID3D12CommandQueue_ExecuteCommandLists(q,1,&tail);
#if defined(TEST_PAIR_SPLIT) || defined(SCARLETT_PAIR_SUBMIT)
    /* The ordered collector signals once, after its successor. */
    if (r && !r->fence) return;
#endif
    if (r && FAILED(ID3D12CommandQueue_Signal(q,r->fence,1))) xs_stop();
}
#ifdef SCARLETT_EXPERIMENTAL_TAGGED_BATCH
#ifdef SCARLETT_EXPERIMENTAL_BRIDGE
/* Under submit_lock. A later command list invalidates the captured final layout. */
static void forget_release(struct adapter *a)
{
    if (a->display.released) ID3D12Resource_Release(a->display.released);
    if (a->display.released_end) ID3D12Fence_Release(a->display.released_end);
    a->display.released=NULL;a->display.released_end=NULL;a->display.released_token=0;
}
#endif
#ifdef SCARLETT_PAIR_SUBMIT
#include "pair-submit.h"
#endif
#include "tagged_batch.h"
#endif
static HRESULT checked_startup_submit(IUnknown *iface, UINT count, IUnknown *const *lists, UINT64 token, UINT flags)
{
#ifdef SCARLETT_EXPERIMENTAL_TAGGED_BATCH
    if (token) return checked_tagged_submit(iface,count,lists,token,flags);
#endif
    ID3D12CommandQueue *q = as_queue(iface);
    if (!q || count!=1 || token || flags) return E_NOTIMPL;
    IUnknown *list; SIZE_T n; const void *vt;
    if (!ReadProcessMemory(GetCurrentProcess(),lists,&list,sizeof list,&n) || n!=sizeof list ||
        !ReadProcessMemory(GetCurrentProcess(),list,&vt,sizeof vt,&n) || n!=sizeof vt ||
        (vt!=&sl_vtbl.prefix && vt!=&pool_vtbl.prefix)) return E_INVALIDARG;
    struct startup_list *s = sl(list);
#ifdef SCARLETT_PAIR_SUBMIT
    if(s->source.needs_publication)return E_ACCESSDENIED;
#endif
#ifdef XS_TEST_ORDERED_QUEUE
    if (s->single_use) return E_ACCESSDENIED;
#endif
    if (!s->closed || FAILED(s->error)) return E_INVALIDARG;
#ifdef SCARLETT_EXPERIMENTAL_TAGGED_BATCH
    if(s->deferred)return E_ACCESSDENIED;
#endif
    if (s->table0.heap || s->prior_pso || s->pending_470) return E_ACCESSDENIED;
    D3D12_COMMAND_QUEUE_DESC desc;
    q->lpVtbl->GetDesc(q, &desc);
    ID3D12Device *device;
    if (FAILED(ID3D12CommandQueue_GetDevice(q, &IID_ID3D12Device, (void **)&device))) xs_stop();
    BOOL match = device == s->device;
    ID3D12Device_Release(device);
    if (!match || desc.Type != s->type) return E_INVALIDARG;
    struct heap_retirement *r=NULL;
    PTP_WAIT wait=NULL;
    if (s->source.buffer || s->heaps[0] || s->stamp || s->primary || s->graphics_root || s->bound) {
        r=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof *r);
        if (!r || FAILED(ID3D12Device_CreateFence(s->device,0,D3D12_FENCE_FLAG_NONE,&IID_ID3D12Fence,(void **)&r->fence)) ||
            !(r->event=CreateEventW(NULL,FALSE,FALSE,NULL)) || !(wait=CreateThreadpoolWait(retire_heaps,r,NULL)) ||
            FAILED(ID3D12Fence_SetEventOnCompletion(r->fence,1,r->event))) xs_stop();
        retain_recording(r,s);
    }
    SRWLOCK *lock=&CONTAINING_RECORD(iface,struct adapter,iface)->submit_lock;
    AcquireSRWLockExclusive(lock);
#ifdef SCARLETT_EXPERIMENTAL_DIRECT_WAIT
    if (s->post && s->post->counter && CONTAINING_RECORD(iface,struct adapter,iface)->external_submissions)
        xs_stop(); /* cannot atomically submit the split around an untracked producer */
#endif
#ifdef SCARLETT_EXPERIMENTAL_BRIDGE
#ifdef SCARLETT_PAIR_SUBMIT
    if(s->reuse_epoch){
        HRESULT hr=E_ACCESSDENIED;
        if(!s->host_exposed && CONTAINING_RECORD(iface,struct adapter,iface)->display.owner==s->pool_owner &&
           TryAcquireSRWLockExclusive(&escape_lock)){
            if(!bridge_escaped)hr=xs_reuse_with_sr(s->pool_owner,s->primary_pin,s->primary,q,&s->reuse_epoch,s->sr.end?&s->sr:NULL);
            if(SUCCEEDED(hr)){InterlockedIncrement(&pair_inflight);r->reuse_guard=TRUE;}
            ReleaseSRWLockExclusive(&escape_lock);
        }
        if(FAILED(hr)){ReleaseSRWLockExclusive(lock);retire_heaps(NULL,r,wait,WAIT_OBJECT_0);return hr;}
    }else
#endif
    if(s->primary && FAILED(xs_primary_writer(s->pool_owner,s->primary,q)))xs_stop();
    forget_release(CONTAINING_RECORD(iface,struct adapter,iface));
#ifdef SCARLETT_PAIR_SUBMIT
    if(!xs_sr_submitted(s->pool_owner,s->attachments,s->attachments+1,q,NULL)){
        ReleaseSRWLockExclusive(lock);retire_heaps(NULL,r,wait,WAIT_OBJECT_0);return E_ACCESSDENIED;
    }
#endif
#endif
    execute_recording(q,s,r);
    if (r) SetThreadpoolWait(wait,r->event,NULL);
    ReleaseSRWLockExclusive(lock);
    return S_OK;
}
static void WINAPI startup_submit(IUnknown *iface, UINT count, IUnknown *const *lists, UINT64 token, UINT flags)
{
#ifdef SCARLETT_PAIR_SUBMIT
    if(count==2 && token==2 && flags==3 && (UINT_PTR)__builtin_return_address(0)!=(UINT_PTR)GetModuleHandleW(NULL)+0x24821e3)xs_stop();
#endif
    if (checked_startup_submit(iface,count,lists,token,flags)!=S_OK) xs_stop();
}
