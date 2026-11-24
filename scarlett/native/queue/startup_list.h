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
struct startup_list {
#ifdef SCARLETT_EXPERIMENTAL_TAGGED_BATCH
    enum batch_phase phase;
    IUnknown *sync_owner;
#endif
#ifdef SCARLETT_EXPERIMENTAL_BRIDGE
    SIZE_T clear_slot;
    UINT64 clear_serial;
#endif
    void *reservation;
    LONG refs;
    ID3D12GraphicsCommandList7 *host;
    BYTE *cache_page;
    IUnknown *graphics_identity,*bound,*required_root;
    ID3D12PipelineState *pipeline; /* borrowed from bound */
    ID3D12RootSignature *graphics_root; /* borrowed from graphics_identity */
    ID3D12Device *device; /* Kept alive by host. */
    IUnknown *allocator,*pool_owner,*heaps[2];
    D3D12_COMMAND_LIST_TYPE type;
    HRESULT error;
    BOOL primary_cleared;
    BOOL primary_released; /* Guest layout 0x1f on the retained primary. */
    ID3D12Resource *primary; /* +490 recording owns the imported texture, not the guest identity. */
    struct prefix_marker *marker,*post;
    ID3D12GraphicsCommandList7 *head; /* closed segment preceding post */
    ID3D12CommandAllocator *tail_allocator;
    struct system_timestamp *stamp;
    BOOL closed,host_exposed,post_boundary,stencil_zero,blend_ones;
    SRWLOCK borrow_lock; /* Serialize a native borrow against recording deferred guest state. */
    BOOL depth_zero_pending,pending_470;
    struct list_face local; /* Non-pooled lists need no inline ABI. */
};
const IID xs_sync_list_id = {0xc3827890,0x4024,0x474c,{0x98,0xcf,0x64,0x5b,0x51,0xdc,0x48,0x74}};
const IID xs_pool_list_id = {0x5b160d0f,0xac1b,0x4185,{0x8b,0xa8,0xb3,0xae,0x42,0xa5,0xa4,0x55}};

static void sl_free(struct startup_list *s)
{
    if (s->reservation) VirtualFree(s->reservation, 0, MEM_RELEASE);
    else HeapFree(GetProcessHeap(), 0, s);
}
static struct startup_list *sl(IUnknown *i) { return CONTAINING_RECORD(i, struct list_face, iface)->state; }
static ULONG WINAPI sl_addref(IUnknown *i) { return InterlockedIncrement(&sl(i)->refs); }
static void drop_heaps(IUnknown *pair[2])
{
    for (unsigned i=0;i<2;i++) if (pair[i]) { IUnknown_Release(pair[i]); pair[i]=NULL; }
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
    ULONG refs = InterlockedDecrement(&s->refs);
    if (!refs) {
        ID3D12GraphicsCommandList7_Release(s->host);
        IUnknown_Release(s->allocator);
        drop_split(s); drop_timestamp(s->stamp);
        drop_marker(s->marker);
        if (s->primary) ID3D12Resource_Release(s->primary);
        drop_heaps(s->heaps);
        if (s->pool_owner) IUnknown_Release(s->pool_owner);
#ifdef SCARLETT_EXPERIMENTAL_TAGGED_BATCH
        if (s->sync_owner) IUnknown_Release(s->sync_owner);
#endif
        if(s->graphics_identity)IUnknown_Release(s->graphics_identity);
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
static HRESULT WINAPI sl_close(IUnknown *i)
{
    struct startup_list *s = sl(i);
    if (s->closed) return E_FAIL;
    if(s->cache_page && memcmp(s->cache_page,cache_template,sizeof cache_template))return E_NOTIMPL;
    HRESULT hr = ID3D12GraphicsCommandList7_Close(s->host);
    s->closed = TRUE;
    batch_step(s,B_CLOSED);
    if (FAILED(hr)) s->error = hr;
    return s->error;
}

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
    if (flags || pso) return E_NOTIMPL;
    if (!s->closed) return E_FAIL;
    ID3D12CommandAllocator *a = xs_allocator_host(alloc, s->type);
    if (!allocator_matches(a, s->device)) return E_INVALIDARG;
    HRESULT hr = ID3D12GraphicsCommandList7_Reset(s->host, a, NULL);
    if (FAILED(hr)) return hr;
    IUnknown_AddRef(alloc);
    IUnknown_Release(s->allocator);
    s->allocator = alloc;
    if(s->graphics_identity){IUnknown_Release(s->graphics_identity);s->graphics_identity=NULL;s->graphics_root=NULL;}
    s->primary_cleared=FALSE;
    s->primary_released=FALSE;
    if (s->primary) { ID3D12Resource_Release(s->primary); s->primary=NULL; }
    drop_heaps(s->heaps);
    drop_marker(s->marker); s->marker=NULL;
    drop_split(s); drop_timestamp(s->stamp); s->stamp=NULL;
    AcquireSRWLockExclusive(&s->borrow_lock);
    if(s->bound){IUnknown_Release(s->bound);s->bound=NULL;s->required_root=NULL;s->pipeline=NULL;}
    if(s->cache_page)memcpy(s->cache_page,cache_template,sizeof cache_template);
    s->stencil_zero=FALSE;
    s->blend_ones=FALSE;
    s->pending_470=FALSE;
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
    if (!s->heaps[0]) for (unsigned i=0;i<2;i++) { IUnknown_AddRef(pair[i]); s->heaps[i]=pair[i]; }
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
        !((id->offset==8 && !value) || (id->offset==0x50 && value==1))) return E_NOTIMPL;
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
static HRESULT record_native_post(struct startup_list *s,UINT64 addr,UINT value,UINT64 control,UINT flags)
{
    struct prefix_marker *m=s->marker;
    if (!s->stamp || !s->stamp->retained || !m || !m->buffer || !m->control_section || m->native_post || s->closed ||
        s->host_exposed || control || flags || !value) return E_NOTIMPL;
    struct scarlett_section_info id;
    HRESULT hr=scarlett_query_section(0x5343000a,(void *)addr,4,&id);
    if (FAILED(hr)) return hr;
    BOOL (WINAPI *same)(HANDLE,HANDLE)=(void *)GetProcAddress(GetModuleHandleW(L"kernelbase.dll"),"CompareObjectHandles");
    BOOL match=id.view_size==65536 && (m->value==1 ? id.offset==0x54 && value==0x29a5ee15 : id.offset==12) &&
        same && same(id.section,m->control_section);
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
static const FLOAT ones_factor[4]={1,1,1,1};
static HRESULT split_native_list(struct startup_list *s,struct prefix_marker *post)
{
    post->refs=1;post->parent=s->marker;InterlockedIncrement(&post->parent->refs);
    ID3D12GraphicsCommandList7 *tail=NULL;
    ID3D12CommandAllocator *a=NULL;
    HRESULT hr=ID3D12Device_CreateCommandAllocator(s->device,s->type,&IID_ID3D12CommandAllocator,(void **)&a);
    if (SUCCEEDED(hr)) hr=ID3D12Device_CreateCommandList(s->device,1,s->type,a,s->pipeline,&IID_ID3D12GraphicsCommandList7,(void **)&tail);
    if (SUCCEEDED(hr) && s->stencil_zero) ID3D12GraphicsCommandList7_OMSetStencilRef(tail,0);
    if (SUCCEEDED(hr) && s->blend_ones) ID3D12GraphicsCommandList7_OMSetBlendFactor(tail,ones_factor);
    if (SUCCEEDED(hr) && s->graphics_root)ID3D12GraphicsCommandList7_SetGraphicsRootSignature(tail,s->graphics_root);
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
    s->post_boundary=!post->counter;
    return S_OK;
}
static HRESULT split_after_timestamp(struct startup_list *s,UINT64 addr,UINT value,UINT64 control,UINT flags)
{
    if (!s->marker || !s->marker->alias || !s->stamp || s->closed ||
        s->host_exposed || s->post || control || flags) return E_NOTIMPL;
    SIZE_T prefix=s->marker->value==1 ? 0x50 : 8;
    if (s->marker->value && (s->marker->value!=1 || value!=0x29a5ee15)) return E_NOTIMPL;
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
static void WINAPI sl_marker(IUnknown *i,UINT64 addr,UINT value,UINT64 control,UINT flags)
{
    struct startup_list *s=sl(i);
    HRESULT hr=!s->stamp ? prefix_write(s,addr,value,control,flags) : s->marker && s->marker->buffer ?
        record_native_post(s,addr,value,control,flags) : split_after_timestamp(s,addr,value,control,flags);
    if (FAILED(hr)) xs_stop();
    batch_step(s,(s->stamp?B_POST:B_PREFIX));
}
static HRESULT pool_timestamp(struct startup_list *s,UINT64 addr,UINT64 x,UINT64 y,UINT flags)
{
    /* shortcut: only the observed kind-2 query encoding and primary-release pair. */
    if (!s->reservation || !s->pool_owner || s->closed || s->type!=D3D12_COMMAND_LIST_TYPE_DIRECT ||
        FAILED(s->error) || x || y || flags!=1) return E_NOTIMPL;
    if (!addr || (addr&7)) return E_INVALIDARG;
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
        (m->value && !(m->value==1 && s->stamp->second_bank)) ||
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
static void WINAPI sl_wait64(IUnknown *i,UINT64 addr,UINT op,UINT64 ref,UINT flags)
{
    if (SUCCEEDED(record_counter_wait(sl(i),addr,op,ref,flags))) {batch_step(sl(i),B_WAIT);return;}
    xs_stop();
}
#else
#define sl_wait64 xs_stop
#endif
/* shortcut: one observed whole-primary UAV-to-RT transition per recording. */
static HRESULT record_primary_barrier(struct startup_list *s,UINT count,const D3D12_BARRIER_GROUP *groups)
{
    if (!s->reservation || !s->pool_owner || s->closed || FAILED(s->error) || s->primary_released ||
        s->type!=D3D12_COMMAND_LIST_TYPE_DIRECT || count!=1) return E_NOTIMPL;
    D3D12_BARRIER_GROUP g;
    D3D12_TEXTURE_BARRIER b;
    SIZE_T n;
    if (!ReadProcessMemory(GetCurrentProcess(),groups,&g,sizeof g,&n) || n!=sizeof g) return E_POINTER;
    if (g.Type!=D3D12_BARRIER_TYPE_TEXTURE || g.NumBarriers!=1) return E_NOTIMPL;
    /* The guest need not supply readable tail padding after Flags. */
    SIZE_T bytes=offsetof(D3D12_TEXTURE_BARRIER,Flags)+sizeof b.Flags;
    if (!ReadProcessMemory(GetCurrentProcess(),g.pTextureBarriers,&b,bytes,&n) || n!=bytes) return E_POINTER;
    const D3D12_BARRIER_SUBRESOURCE_RANGE all={.IndexOrFirstMipLevel=UINT_MAX};
    if (b.Flags || memcmp(&b.Subresources,&all,sizeof all)) return E_NOTIMPL;
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
    } else {
        if (b.SyncBefore!=D3D12_BARRIER_SYNC_ALL_SHADING || b.SyncAfter!=D3D12_BARRIER_SYNC_RENDER_TARGET ||
            b.AccessBefore!=D3D12_BARRIER_ACCESS_UNORDERED_ACCESS || b.AccessAfter!=D3D12_BARRIER_ACCESS_RENDER_TARGET ||
            b.LayoutBefore!=D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS || b.LayoutAfter!=D3D12_BARRIER_LAYOUT_RENDER_TARGET) return E_NOTIMPL;
        if (!(s->primary=xs_retain_primary(s->pool_owner,b.pResource))) return E_INVALIDARG;
    }
    b.pResource=s->primary;
    g.pTextureBarriers=&b;
    ID3D12GraphicsCommandList7_Barrier(s->host,1,&g);
    return S_OK;
}
static void WINAPI sl_primary_barrier(IUnknown *i,UINT count,const D3D12_BARRIER_GROUP *groups)
{
    if (FAILED(record_primary_barrier(sl(i),count,groups))) xs_stop();
    batch_step(sl(i),(sl(i)->primary_released?B_RELEASED:B_ACQUIRED));
}
/* shortcut: first full transparent-black clear after this recording's primary transition. */
static HRESULT record_primary_clear(struct startup_list *s,SIZE_T slot,const FLOAT *color,UINT count,const D3D12_RECT *rects)
{
    if (s->pending_470) return E_ACCESSDENIED;
    if (!s->primary || s->closed || FAILED(s->error) || s->host_exposed || s->primary_cleared || count || rects) return E_NOTIMPL;
    UINT bits[4]; SIZE_T n;
    if (!ReadProcessMemory(GetCurrentProcess(),color,bits,sizeof bits,&n) || n!=sizeof bits) return E_POINTER;
    if (bits[0] || bits[1] || bits[2] || bits[3]) return E_NOTIMPL;
    UINT64 serial=0;
    HRESULT hr=xs_clear_primary_rtv(s->pool_owner,s->host,slot,s->primary,&serial);
#ifdef SCARLETT_EXPERIMENTAL_BRIDGE
    if(SUCCEEDED(hr)){s->clear_slot=slot;s->clear_serial=serial;}
#endif
    if (SUCCEEDED(hr)) s->primary_cleared=TRUE;
    return hr;
}
static void WINAPI sl_primary_clear(IUnknown *i,SIZE_T slot,const FLOAT *color,UINT count,const D3D12_RECT *rects)
{
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
    return open_direct_state(s) && topology==D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
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
    struct startup_list *s=sl(i);
    if (!can_set_topology(s,topology)) xs_stop();
    ID3D12GraphicsCommandList7_IASetPrimitiveTopology(s->host,D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
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
    if (FAILED(record_pool_rate(sl(i),rate,pair))) xs_stop();
}
static HRESULT record_graphics_root(struct startup_list *s,IUnknown *identity)
{
    if(!open_direct_state(s) || !identity)return E_INVALIDARG;
    if((s->required_root && s->required_root!=identity) ||
        (s->graphics_identity && s->graphics_identity!=identity))return E_NOTIMPL;
    if(!s->graphics_identity){
        s->graphics_root=xs_retain_graphics_root(s->pool_owner,identity);
        if(!s->graphics_root)return E_INVALIDARG;
        s->graphics_identity=identity;
    }
    ID3D12GraphicsCommandList7_SetGraphicsRootSignature(s->host,s->graphics_root);
    recorded_direct_state(s);return S_OK;
}
static void WINAPI pool_root(IUnknown *i,IUnknown *root)
{
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
    if(FAILED(record_stencil(sl(i),reference)))xs_stop();
}
static HRESULT record_clear4(struct startup_list *s,IUnknown *identity)
{
    HRESULT hr=E_NOTIMPL;
    AcquireSRWLockExclusive(&s->borrow_lock);
    if(open_direct_state(s) && s->cache_page && !memcmp(s->cache_page,cache_template,sizeof cache_template) &&
        (!s->bound || s->bound==identity)){
        IUnknown *root=NULL;
        ID3D12PipelineState *p=xs_pin_clear4(s->pool_owner,s->device,identity,&root);
        if(p){
            if(!s->graphics_identity || s->graphics_identity==root){
                if(!s->bound){s->bound=identity;s->pipeline=p;s->required_root=root;}
                else IUnknown_Release(identity);
                ID3D12GraphicsCommandList7_SetPipelineState(s->host,p);recorded_direct_state(s);hr=S_OK;
            }else IUnknown_Release(identity);
        }
    }
    ReleaseSRWLockExclusive(&s->borrow_lock);return hr;
}
static void WINAPI pool_pipeline(IUnknown *i,IUnknown *pso)
{
    if(FAILED(record_clear4(sl(i),pso)))xs_stop();
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
    if (FAILED(unbind_index(sl(i),view))) xs_stop();
}
/* Only these slots have been traced on the pool's returned pointer. */
static void WINAPI pool_470(IUnknown *,UINT64);
static const struct {
    IUnknownVtbl prefix;
    xs_stop_fn gap18[6];
    __typeof__(&sl_close) close;
    xs_stop_fn gap50[10];
    __typeof__(&pool_topology) topology;
    xs_stop_fn gapa8[2]; __typeof__(&pool_blend) blend; __typeof__(&pool_stencil) stencil; __typeof__(&pool_pipeline) pipeline; xs_stop_fn gapd0[6];
    __typeof__(&sl_bind) bind;
    xs_stop_fn gap108; __typeof__(&pool_root) root; xs_stop_fn gap118[12];
    __typeof__(&pool_index) index; xs_stop_fn gap180[4];
    __typeof__(&sl_primary_clear) clear;
    xs_stop_fn gap1a8[18];
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
    {sl_query,sl_addref,sl_release}, {[0 ... 5]=xs_stop}, sl_close, {[0 ... 9]=xs_stop}, pool_topology, {xs_stop,xs_stop},pool_blend,pool_stencil,pool_pipeline,{[0 ... 5]=xs_stop}, sl_bind, xs_stop, pool_root, {[0 ... 11]=xs_stop},pool_index,{[0 ... 3]=xs_stop}, sl_primary_clear, {[0 ... 17]=xs_stop}, sl_marker, sl_timestamp, xs_stop, sl_wait64, pool_depth_order, {[0 ... 49]=xs_stop}, pool_rate, {[0 ... 3]=xs_stop}, pool_sync418, {[0 ... 6]=xs_stop}, sl_reset, {xs_stop,xs_stop}, pool_470, {[0 ... 2]=xs_stop}, sl_primary_barrier
};
_Static_assert(offsetof(__typeof__(pool_vtbl),blend)==0xb8,"blend factor ABI");
_Static_assert(offsetof(__typeof__(pool_vtbl),state470)==0x470,"pooled opaque state ABI");
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

/* shortcut: retain only #4's measured qword on its logical recording. No host
 * lowering or submit until a consumer establishes the meaning of this state. */
static HRESULT record_470(IUnknown *i,UINT64 value)
{
    if (!i || i->lpVtbl!=&pool_vtbl.prefix || value!=UINT64_MAX) return E_NOTIMPL;
    struct startup_list *s=sl(i);
    HRESULT hr=E_NOTIMPL;
    AcquireSRWLockExclusive(&s->borrow_lock);
    /* record_clear4 already pins bound for this exact pool owner and host device. */
    if (open_direct_state(s) && s->bound && s->required_root && s->graphics_identity==s->required_root) {
        s->pending_470=TRUE;
        recorded_direct_state(s);
        hr=S_OK;
    }
    ReleaseSRWLockExclusive(&s->borrow_lock);
    return hr;
}
static void WINAPI pool_470(IUnknown *i,UINT64 value)
{
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
    if (!s->primary_released && !s->depth_zero_pending && !s->pending_470 && !s->bound
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
struct heap_retirement { IUnknown *bound; ID3D12RootSignature *root; ID3D12Resource *primary; ID3D12CommandAllocator *tail_alloc; IUnknown *head_alloc; struct prefix_marker *native; struct system_timestamp *stamp; IUnknown *pair[2]; ID3D12Fence *fence,*gate; HANDLE event;
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
    if(r->bound)IUnknown_Release(r->bound);
    if(r->root)ID3D12RootSignature_Release(r->root);
    if (r->primary) ID3D12Resource_Release(r->primary);
    if (r->head_alloc) IUnknown_Release(r->head_alloc);
    if (r->tail_alloc) ID3D12CommandAllocator_Release(r->tail_alloc);
    if (r->gate) ID3D12Fence_Release(r->gate);
    drop_timestamp(r->stamp); drop_marker(r->native); drop_heaps(r->pair); ID3D12Fence_Release(r->fence); CloseHandle(r->event); HeapFree(GetProcessHeap(),0,r);
}
static void retain_recording(struct heap_retirement *r,struct startup_list *s)
{
    if (s->marker && s->marker->buffer) {r->native=s->marker;InterlockedIncrement(&r->native->refs);}
    if ((r->bound=s->bound))IUnknown_AddRef(r->bound);
    if ((r->root=s->graphics_root))ID3D12RootSignature_AddRef(r->root);
    if ((r->primary=s->primary)) ID3D12Resource_AddRef(r->primary);
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
    if (!s->closed || FAILED(s->error)) return E_INVALIDARG;
    if (s->pending_470) return E_ACCESSDENIED;
    D3D12_COMMAND_QUEUE_DESC desc;
    q->lpVtbl->GetDesc(q, &desc);
    ID3D12Device *device;
    if (FAILED(ID3D12CommandQueue_GetDevice(q, &IID_ID3D12Device, (void **)&device))) xs_stop();
    BOOL match = device == s->device;
    ID3D12Device_Release(device);
    if (!match || desc.Type != s->type) return E_INVALIDARG;
    struct heap_retirement *r=NULL;
    PTP_WAIT wait=NULL;
    if (s->heaps[0] || s->stamp || s->primary || s->graphics_root || s->bound) {
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
    if(s->primary && FAILED(xs_primary_writer(s->pool_owner,s->primary,q)))xs_stop();
    forget_release(CONTAINING_RECORD(iface,struct adapter,iface));
#endif
    execute_recording(q,s,r);
    if (r) SetThreadpoolWait(wait,r->event,NULL);
    ReleaseSRWLockExclusive(lock);
    return S_OK;
}
static void WINAPI startup_submit(IUnknown *iface, UINT count, IUnknown *const *lists, UINT64 token, UINT flags)
{
    if (checked_startup_submit(iface,count,lists,token,flags)!=S_OK) xs_stop();
}
