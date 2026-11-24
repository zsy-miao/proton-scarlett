/* Private to queue.c: only the recording and submission shapes used by the startup lists. */
struct startup_list {
    void *reservation;
    LONG refs;
    ID3D12GraphicsCommandList7 *host;
    ID3D12Device *device; /* Kept alive by host. */
    IUnknown *allocator;
    D3D12_COMMAND_LIST_TYPE type;
    HRESULT error;
    BOOL closed;
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
static ULONG WINAPI sl_release(IUnknown *i)
{
    struct startup_list *s = sl(i);
    ULONG refs = InterlockedDecrement(&s->refs);
    if (!refs) {
        ID3D12GraphicsCommandList7_Release(s->host);
        IUnknown_Release(s->allocator);
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

/* Only these slots have been traced on the pool's returned pointer. */
static const struct {
    IUnknownVtbl prefix;
    xs_stop_fn gap18[6];
    __typeof__(&sl_close) close;
    xs_stop_fn gap50[129];
    __typeof__(&sl_reset) reset;
} pool_vtbl = {
    {sl_query,sl_addref,sl_release}, {[0 ... 5]=xs_stop}, sl_close, {[0 ... 128]=xs_stop}, sl_reset
};
_Static_assert(offsetof(__typeof__(pool_vtbl),close)==0x48 && offsetof(__typeof__(pool_vtbl),reset)==0x458,
        "pooled list ABI");

HRESULT xs_startup_list(ID3D12Device *device, D3D12_COMMAND_LIST_TYPE type, IUnknown *alloc, BOOL pooled, IUnknown **out)
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
    s->refs = 1;
    s->device = device;
    s->type = type;
    IUnknown_AddRef(alloc);
    s->allocator = alloc;
    *out = &s->iface;
    return S_OK;
}
ID3D12GraphicsCommandList7 *xs_list_host(IUnknown *i)
{
    return i && (i->lpVtbl == &sl_vtbl.prefix || i->lpVtbl == &pool_vtbl.prefix) ? sl(i)->host : NULL;
}

static void WINAPI startup_submit(IUnknown *iface, UINT count, IUnknown *const *lists, UINT64 token, UINT flags)
{
    ID3D12CommandQueue *q = as_queue(iface);
    if (!q || count != 1 || !lists || !xs_list_host(lists[0]) || token || flags) xs_stop();
    struct startup_list *s = sl(lists[0]);
    D3D12_COMMAND_QUEUE_DESC desc;
    q->lpVtbl->GetDesc(q, &desc);
    ID3D12Device *device;
    if (FAILED(ID3D12CommandQueue_GetDevice(q, &IID_ID3D12Device, (void **)&device))) xs_stop();
    BOOL match = device == s->device;
    ID3D12Device_Release(device);
    if (!match || !s->closed || FAILED(s->error) || desc.Type != s->type) xs_stop();
    ID3D12CommandList *host = (ID3D12CommandList *)s->host;
    ID3D12CommandQueue_ExecuteCommandLists(q, 1, &host);
}
