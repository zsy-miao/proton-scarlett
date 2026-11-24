/* Private to queue.c: only the recording and submission shapes used by the startup lists. */
#include "prefix_marker.h"
#include "system_timestamp.h"
struct startup_list {
    void *reservation;
    LONG refs;
    ID3D12GraphicsCommandList7 *host;
    ID3D12Device *device; /* Kept alive by host. */
    IUnknown *allocator,*pool_owner,*heaps[2];
    D3D12_COMMAND_LIST_TYPE type;
    HRESULT error;
    struct prefix_marker *marker,*post;
    ID3D12GraphicsCommandList7 *head; /* closed segment preceding post */
    ID3D12CommandAllocator *tail_allocator;
    struct system_timestamp *stamp;
    BOOL closed,host_exposed;
    IUnknown iface; /* Last: pooled callers' inline packet fields are not host bookkeeping. */
};
_Static_assert(offsetof(struct startup_list,iface)+sizeof(IUnknown)==sizeof(struct startup_list),"interface at end");
const IID xs_sync_list_id = {0xc3827890,0x4024,0x474c,{0x98,0xcf,0x64,0x5b,0x51,0xdc,0x48,0x74}};
const IID xs_pool_list_id = {0x5b160d0f,0xac1b,0x4185,{0x8b,0xa8,0xb3,0xae,0x42,0xa5,0xa4,0x55}};

static void sl_free(struct startup_list *s)
{
    if (s->reservation) VirtualFree(s->reservation, 0, MEM_RELEASE);
    else HeapFree(GetProcessHeap(), 0, s);
}
static struct startup_list *sl(IUnknown *i) { return CONTAINING_RECORD(i, struct startup_list, iface); }
static ULONG WINAPI sl_addref(IUnknown *i) { return InterlockedIncrement(&sl(i)->refs); }
static void drop_heaps(IUnknown *pair[2])
{
    for (unsigned i=0;i<2;i++) if (pair[i]) { IUnknown_Release(pair[i]); pair[i]=NULL; }
}
static void drop_split(struct startup_list *s)
{
    drop_marker(s->post); s->post=NULL;
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
        drop_heaps(s->heaps);
        if (s->pool_owner) IUnknown_Release(s->pool_owner);
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
static HRESULT WINAPI sl_close(IUnknown *i)
{
    struct startup_list *s = sl(i);
    if (s->closed) return E_FAIL;
    HRESULT hr = ID3D12GraphicsCommandList7_Close(s->host);
    s->closed = TRUE;
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
    drop_heaps(s->heaps);
    drop_marker(s->marker); s->marker=NULL;
    drop_split(s); drop_timestamp(s->stamp); s->stamp=NULL;
    s->closed = FALSE;
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
    return S_OK;
}
static void WINAPI sl_bind(IUnknown *i,UINT count,IUnknown *const *input)
{
    if (FAILED(bind_initial_heaps(sl(i),count,input))) xs_stop();
}
/* The acquire pins the original AMD control section, independent of later VA reuse. */
static HRESULT record_control_zero(struct startup_list *s,struct prefix_marker *m,const struct scarlett_section_info *id,UINT value)
{
    if (!s->heaps[0] || !s->heaps[1] || value || !id->section || id->view_size!=65536 || id->offset!=8) return E_NOTIMPL;
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
    return S_OK;
}
static HRESULT prefix_write(struct startup_list *s,UINT64 addr,UINT value,UINT64 control,UINT flags)
{
    struct scarlett_section_info lease;
    /* shortcut: prefix only, before any host recorder escapes; no mid-list stage inferred. */
    if (!s->reservation || !s->pool_owner || s->closed || s->type!=D3D12_COMMAND_LIST_TYPE_DIRECT ||
        s->host_exposed || s->marker || s->stamp || control || flags) return E_NOTIMPL;
    if (!addr || (addr&3)) return E_INVALIDARG;
    struct prefix_marker *m=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof *m);
    if (!m) return E_OUTOFMEMORY;
    m->refs=1;
    HRESULT hr=scarlett_query_section(0x5343000a,(void*)addr,4,&lease);
    if (SUCCEEDED(hr)) {
        hr=record_control_zero(s,m,&lease,value);
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
    BOOL match=id.view_size==65536 && id.offset==12 && same && same(id.section,m->control_section);
    if (id.section) CloseHandle(id.section);
    if (!match) return E_INVALIDARG;
    D3D12_GLOBAL_BARRIER b={D3D12_BARRIER_SYNC_ALL,D3D12_BARRIER_SYNC_ALL,D3D12_BARRIER_ACCESS_COMMON,D3D12_BARRIER_ACCESS_COMMON};
    D3D12_BARRIER_GROUP g={.Type=D3D12_BARRIER_TYPE_GLOBAL,.NumBarriers=1,.pGlobalBarriers=&b};
    D3D12_WRITEBUFFERIMMEDIATE_PARAMETER op={ID3D12Resource_GetGPUVirtualAddress(m->buffer)+12,value};
    ID3D12GraphicsCommandList7_Barrier(s->host,1,&g);
    ID3D12GraphicsCommandList7_WriteBufferImmediate(s->host,1,&op,NULL);
    ID3D12GraphicsCommandList7_Barrier(s->host,1,&g);
    m->native_post=TRUE;
    return S_OK;
}
/* Only the initial heap state can be replayed at this boundary. Never split an
 * escaped recorder: a client could retain that pointer or set untracked state. */
static HRESULT split_after_timestamp(struct startup_list *s,UINT64 addr,UINT value,UINT64 control,UINT flags)
{
    if (!s->marker || !s->marker->alias || s->marker->value || !s->stamp || s->closed ||
        s->host_exposed || s->post || control || flags) return E_NOTIMPL;
    struct scarlett_section_info id;
    HRESULT hr=scarlett_query_section(0x53430015,(void *)addr,4,&id);
    if (FAILED(hr)) return hr;
    if (id.offset!=s->marker->generation || id.view_size!=65536 || addr!=(UINT_PTR)id.base+12 ||
        s->marker->word!=(LONG *)((BYTE *)s->marker->alias+8)) return E_INVALIDARG;
    struct prefix_marker *post=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof *post);
    if (!post) return E_OUTOFMEMORY;
    post->refs=1;post->parent=s->marker;InterlockedIncrement(&post->parent->refs);
    post->word=s->marker->word+1;post->value=value;
    ID3D12GraphicsCommandList7 *tail=NULL;
    ID3D12CommandAllocator *a=NULL;
    hr=ID3D12Device_CreateCommandAllocator(s->device,s->type,&IID_ID3D12CommandAllocator,(void **)&a);
    if (SUCCEEDED(hr)) hr=ID3D12Device_CreateCommandList(s->device,1,s->type,a,NULL,&IID_ID3D12GraphicsCommandList7,(void **)&tail);
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
    return S_OK;
}
static void WINAPI sl_marker(IUnknown *i,UINT64 addr,UINT value,UINT64 control,UINT flags)
{
    struct startup_list *s=sl(i);
    HRESULT hr=!s->stamp ? prefix_write(s,addr,value,control,flags) : s->marker && s->marker->buffer ?
        record_native_post(s,addr,value,control,flags) : split_after_timestamp(s,addr,value,control,flags);
    if (FAILED(hr)) xs_stop();
}
static HRESULT pool_timestamp(struct startup_list *s,UINT64 addr,UINT64 x,UINT64 y,UINT flags)
{
    /* shortcut: only the observed kind-2 query encoding and one query per recording. */
    if (!s->reservation || !s->pool_owner || s->closed || s->type!=D3D12_COMMAND_LIST_TYPE_DIRECT ||
        s->stamp || x || y || flags!=1) return E_NOTIMPL;
    if (!addr || (addr&7)) return E_INVALIDARG;
    return record_timestamp(s->device,s->host,addr,&s->stamp);
}
static void WINAPI sl_timestamp(IUnknown *i,UINT64 addr,UINT64 x,UINT64 y,UINT flags)
{
    if (FAILED(pool_timestamp(sl(i),addr,x,y,flags))) xs_stop();
}
/* Only these slots have been traced on the pool's returned pointer. */
static const struct {
    IUnknownVtbl prefix;
    xs_stop_fn gap18[6];
    __typeof__(&sl_close) close;
    xs_stop_fn gap50[22];
    __typeof__(&sl_bind) bind;
    xs_stop_fn gap108[38];
    __typeof__(&sl_marker) marker;
    __typeof__(&sl_timestamp) timestamp;
    xs_stop_fn gap248[66];
    __typeof__(&sl_reset) reset;
    xs_stop_fn through490[7];
} pool_vtbl = {
    {sl_query,sl_addref,sl_release}, {[0 ... 5]=xs_stop}, sl_close, {[0 ... 21]=xs_stop}, sl_bind, {[0 ... 37]=xs_stop}, sl_marker, sl_timestamp, {[0 ... 65]=xs_stop}, sl_reset, {[0 ... 6]=xs_stop}
};
_Static_assert(offsetof(__typeof__(pool_vtbl),through490[6])==0x490, "pooled list guard");
_Static_assert(offsetof(__typeof__(pool_vtbl),close)==0x48 && offsetof(__typeof__(pool_vtbl),bind)==0x100 && offsetof(__typeof__(pool_vtbl),marker)==0x238 && offsetof(__typeof__(pool_vtbl),timestamp)==0x240 && offsetof(__typeof__(pool_vtbl),reset)==0x458,
        "pooled list ABI");

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
    if (pooled) {
        /* +0x18/+0x30 are packet cursors, +0x544/+0x554 are inline writes. Trap until translated. */
        SYSTEM_INFO info;
        GetSystemInfo(&info);
        BYTE *base = VirtualAlloc(NULL, 2 * info.dwPageSize, MEM_RESERVE, PAGE_NOACCESS);
        if (!base) return E_OUTOFMEMORY;
        if (!VirtualAlloc(base, info.dwPageSize, MEM_COMMIT, PAGE_READWRITE)) {
            VirtualFree(base,0,MEM_RELEASE);
            return E_OUTOFMEMORY;
        }
        s = (struct startup_list *)(base + info.dwPageSize - sizeof(*s));
        s->reservation = base;
    } else {
        s = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*s));
        if (!s) return E_OUTOFMEMORY;
    }
    hr = ID3D12Device_CreateCommandList(device, 1, type, a, NULL, &IID_ID3D12GraphicsCommandList7, (void **)&s->host);
    if (FAILED(hr)) { sl_free(s); return hr; }
    s->iface.lpVtbl = pooled ? &pool_vtbl.prefix : &sl_vtbl.prefix;
    if ((s->pool_owner=pool_owner)) IUnknown_AddRef(pool_owner);
    s->refs = 1;
    s->device = device;
    s->type = type;
    IUnknown_AddRef(alloc);
    s->allocator = alloc;
    *out = &s->iface;
    return S_OK;
}
HRESULT xs_startup_list(ID3D12Device *device,D3D12_COMMAND_LIST_TYPE type,IUnknown *alloc,BOOL pooled,IUnknown **out)
{
    return xs_device_list(device,type,alloc,pooled,NULL,out);
}
ID3D12GraphicsCommandList7 *xs_list_host(IUnknown *i)
{
    if (!i || (i->lpVtbl!=&sl_vtbl.prefix && i->lpVtbl!=&pool_vtbl.prefix)) return NULL;
    sl(i)->host_exposed=TRUE;
    return sl(i)->host;
}

/* A list may be reset or released as soon as it is submitted. Pin this
 * recording's guest heaps independently until this queue passes the list. */
struct heap_retirement { ID3D12CommandAllocator *tail_alloc; IUnknown *head_alloc; struct prefix_marker *native; struct system_timestamp *stamp; IUnknown *pair[2]; ID3D12Fence *fence; HANDLE event; };
static void CALLBACK retire_heaps(PTP_CALLBACK_INSTANCE instance,void *ctx,PTP_WAIT wait,TP_WAIT_RESULT result)
{
    struct heap_retirement *r=ctx;
    (void)instance;(void)result;
    CloseThreadpoolWait(wait);
    if (r->head_alloc) IUnknown_Release(r->head_alloc);
    if (r->tail_alloc) ID3D12CommandAllocator_Release(r->tail_alloc);
    drop_timestamp(r->stamp); drop_marker(r->native); drop_heaps(r->pair); ID3D12Fence_Release(r->fence); CloseHandle(r->event); HeapFree(GetProcessHeap(),0,r);
}
static void WINAPI startup_submit(IUnknown *iface, UINT count, IUnknown *const *lists, UINT64 token, UINT flags)
{
    ID3D12CommandQueue *q = as_queue(iface);
    if (!q || count!=1 || !lists || !lists[0] ||
        (lists[0]->lpVtbl!=&sl_vtbl.prefix && lists[0]->lpVtbl!=&pool_vtbl.prefix) || token || flags) xs_stop();
    struct startup_list *s = sl(lists[0]);
    D3D12_COMMAND_QUEUE_DESC desc;
    q->lpVtbl->GetDesc(q, &desc);
    ID3D12Device *device;
    if (FAILED(ID3D12CommandQueue_GetDevice(q, &IID_ID3D12Device, (void **)&device))) xs_stop();
    BOOL match = device == s->device;
    ID3D12Device_Release(device);
    if (!match || !s->closed || FAILED(s->error) || desc.Type != s->type) xs_stop();
    ID3D12CommandList *host = (ID3D12CommandList *)s->host;
    struct heap_retirement *r=NULL;
    PTP_WAIT wait=NULL;
    if (s->heaps[0] || s->stamp) {
        r=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof *r);
        if (!r || FAILED(ID3D12Device_CreateFence(s->device,0,D3D12_FENCE_FLAG_NONE,&IID_ID3D12Fence,(void **)&r->fence)) ||
            !(r->event=CreateEventW(NULL,FALSE,FALSE,NULL)) || !(wait=CreateThreadpoolWait(retire_heaps,r,NULL)) ||
            FAILED(ID3D12Fence_SetEventOnCompletion(r->fence,1,r->event))) xs_stop();
        if (s->marker && s->marker->buffer) {r->native=s->marker;InterlockedIncrement(&r->native->refs);}
        if (s->post) {
            r->head_alloc=s->allocator;IUnknown_AddRef(r->head_alloc);
            r->tail_alloc=s->tail_allocator;ID3D12CommandAllocator_AddRef(r->tail_alloc);
        }
        if ((r->stamp=s->stamp)) InterlockedIncrement(&r->stamp->refs);
        for (unsigned i=0;i<2;i++) if ((r->pair[i]=s->heaps[i])) IUnknown_AddRef(r->pair[i]);
    }
    SRWLOCK *lock=&CONTAINING_RECORD(iface,struct adapter,iface)->submit_lock;
    AcquireSRWLockExclusive(lock);
    if (s->marker && s->marker->alias) enqueue_marker(q,s->device,s->marker);
    if (s->post) {
        ID3D12CommandList *head=(ID3D12CommandList *)s->head;
        ID3D12CommandQueue_ExecuteCommandLists(q,1,&head);
        enqueue_marker(q,s->device,s->post);
    }
    ID3D12CommandQueue_ExecuteCommandLists(q, 1, &host);
    if (r) {
        if (FAILED(ID3D12CommandQueue_Signal(q,r->fence,1))) xs_stop();
        SetThreadpoolWait(wait,r->event,NULL);
    }
    ReleaseSRWLockExclusive(lock);
}
