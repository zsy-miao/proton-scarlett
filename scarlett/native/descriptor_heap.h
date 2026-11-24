/* Included by the bootstrap device. No desktop heap or descriptor handle crosses the title ABI. */
enum { R8_VIEW=0x230, RAW38_VIEW=0x231, VOLUME1_VIEW=0x232, CUBE1_VIEW=0x233, CUBE_PAIR_VIEW=0x235, SRGB_ROW_VIEW=0x236, STRUCT_VIEW=0x237, ATLAS1K_VIEW, ATLAS2K_VIEW, A8_LINE_VIEW, ARRAY_VIEW_BASE=0x240, TEXEL32_BASE=0x300 };
enum { TILE_UINT_SRV=0x400, TILE_UINT_UAV, TILE_RGB_SRV, TILE_R11_SRV, TILE_R11_UAV, TILE_RG_SRV, TILE_RG_UAV,TILE_F_SRV,TILE_F_UAV, TILE_UINT160_SRV,TILE_UINT160_UAV };
static BOOL tile_view(UINT kind) { return kind>=TILE_UINT_SRV && kind<=TILE_UINT160_UAV; }
static UINT array_view_layers(UINT kind)
{
    UINT layers=kind-ARRAY_VIEW_BASE;
    return layers==2 || layers==CUBE1_FACES ? layers : 0;
}
static DXGI_FORMAT refresh_format(UINT kind)
{
    if (kind==ATLAS1K_VIEW || kind==ATLAS2K_VIEW) return DXGI_FORMAT_A8_UNORM;
    if (kind==A8_LINE_VIEW) return DXGI_FORMAT_A8_UNORM;
    if (kind==SRGB_ROW_VIEW) return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    if (kind==VOLUME1_VIEW || kind==CUBE1_VIEW || array_view_layers(kind) || kind==CUBE_PAIR_VIEW) return DXGI_FORMAT_R8G8B8A8_UNORM;
    DXGI_FORMAT format=(DXGI_FORMAT)(kind-TEXEL32_BASE);
    return kind==R8_VIEW ? DXGI_FORMAT_R8_UNORM : texel32_format(format) ? format : DXGI_FORMAT_UNKNOWN;
}
/* Bound temporary allocations per recorded use. */
enum { STRUCT_MAX_BYTES=16u<<20 };
static BOOL refresh_required(UINT kind) { return kind==STRUCT_VIEW || refresh_format(kind)!=DXGI_FORMAT_UNKNOWN; }
struct descriptor_record {
    UINT64 serial;
    UINT kind, offset; /* Address views: byte offset in the retained WC buffer */
    UINT elements,stride; /* structured SRV only */
    ID3D12Resource *resource;
#ifdef SCARLETT_STAGED_PLACED
    IUnknown *placed; /* exactly first=0, 174 MiB / 4 RAW dwords */
#endif
};
static void drop_record(struct descriptor_record r)
{
    if (r.resource) ID3D12Resource_Release(r.resource);
#ifdef SCARLETT_STAGED_PLACED
    if (r.placed) IUnknown_Release(r.placed);
#endif
}
struct desc_heap {
    IUnknown iface;
    LONG refs;
    ID3D12DescriptorHeap *host, *tail;
    SIZE_T start;
    UINT count, stride, host_stride;
    BOOL visible,rtv,dsv,sampler;
    struct xbox_device *owner;
    struct desc_heap *next;
    struct descriptor_record *records; /* resource, RTV and DSV heaps */
    HMODULE xmem;
    void (WINAPI *xfree)(void *,UINT64);
    UINT64 sampler_serial[];
};
static struct desc_heap **heap_list(struct xbox_device *owner,struct desc_heap *h)
{
    return h->sampler ? &owner->sampler_heaps : h->rtv ? &owner->rtv_heaps : h->dsv ? &owner->dsv_heaps : &owner->heaps;
}
enum { DESC_ATTR = (0x80 << 16) | 4 };
enum { DESC_BANK = D3D12_MAX_SHADER_VISIBLE_DESCRIPTOR_HEAP_SIZE_TIER_1, DESC_STARTUP_COUNT = 0x130000 };
static const struct desc_table {
    IUnknownVtbl unknown;
    xs_stop_fn gap[6];
    SIZE_T (WINAPI *cpu)(IUnknown *);
    UINT64 (WINAPI *gpu)(IUnknown *);
} desc_vtbl;
_Static_assert(offsetof(struct desc_table,cpu)==0x48 && offsetof(struct desc_table,gpu)==0x50,"descriptor start ABI");

static ULONG WINAPI desc_addref(IUnknown *i)
{
    return InterlockedIncrement(&CONTAINING_RECORD(i,struct desc_heap,iface)->refs);
}
static ULONG WINAPI desc_release(IUnknown *i)
{
    struct desc_heap *h = CONTAINING_RECORD(i,struct desc_heap,iface);
    ULONG refs = InterlockedDecrement(&h->refs);
    if (!refs) {
        struct xbox_device *owner = h->owner;
        if (owner) {
            AcquireSRWLockExclusive(&owner->descriptor_lock);
            struct desc_heap **p = heap_list(owner,h);
            while (*p != h) p = &(*p)->next;
            *p = h->next;
            ReleaseSRWLockExclusive(&owner->descriptor_lock);
        }
        if (h->records) {
            for (UINT n=0;n<h->count;n++) drop_record(h->records[n]);
            HeapFree(GetProcessHeap(),0,h->records);
        }
        ID3D12DescriptorHeap_Release(h->host);
        if (h->tail) ID3D12DescriptorHeap_Release(h->tail);
        if (h->xfree && h->start) h->xfree((void *)h->start,DESC_ATTR);
        else if (h->start) VirtualFree((void *)h->start,0,MEM_RELEASE);
        if (h->xmem) FreeLibrary(h->xmem);
        HeapFree(GetProcessHeap(),0,h);
        if (owner) IUnknown_Release(&owner->iface);
    }
    return refs;
}
static HRESULT WINAPI desc_query(IUnknown *i, REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    *out = NULL;
    if (!iid) return E_INVALIDARG;
    if (!IsEqualIID(iid,&IID_IUnknown) && !IsEqualIID(iid,&IID_ID3D12DescriptorHeap)) return E_NOINTERFACE;
    desc_addref(i);
    *out = i;
    return S_OK;
}
static SIZE_T WINAPI desc_cpu(IUnknown *i)
{
    return CONTAINING_RECORD(i,struct desc_heap,iface)->start;
}
static UINT64 WINAPI desc_gpu(IUnknown *i)
{
    struct desc_heap *h = CONTAINING_RECORD(i,struct desc_heap,iface);
    return h->visible ? h->start : 0;
}
static const struct desc_table desc_vtbl = {
    {desc_query,desc_addref,desc_release}, {[0 ... 5]=xs_stop}, desc_cpu,desc_gpu
};

static UINT WINAPI descriptor_stride(IUnknown *device, UINT type)
{
    /* Title storage/padding, independent of the ICD's opaque handle increments. */
    static const UINT strides[] = {32,16,32,32};
    (void)device;
    return type < ARRAYSIZE(strides) ? strides[type] : 0;
}

static HRESULT WINAPI descriptor_heap(IUnknown *device,const void *input,REFIID iid,void **out)
{
    ID3D12Device *host = CONTAINING_RECORD(device,struct xbox_device,iface)->host;
    D3D12_DESCRIPTOR_HEAP_DESC d;
    SIZE_T copied;
    if (!out) return E_POINTER;
    *out = NULL;
    if (!input || !iid) return E_INVALIDARG;
    if (!IsEqualIID(iid,&IID_ID3D12DescriptorHeap)) return E_NOINTERFACE;
    if (!ReadProcessMemory(GetCurrentProcess(),input,&d,sizeof(d),&copied) || copied != sizeof(d)) return E_POINTER;
    if (!d.NumDescriptors) return E_INVALIDARG;
    if (d.NodeMask != 1 || !((d.Type == 0 && (d.Flags == 1 || (!d.Flags && d.NumDescriptors == 0xc38))) ||
        (d.Type == 1 && d.NumDescriptors == 128 && (UINT)d.Flags <= 1) ||
        (d.Type == 2 && d.NumDescriptors == 4096 && !d.Flags) ||
        (d.Type == 3 && d.NumDescriptors == 2048 && !d.Flags))) return E_NOTIMPL;
    BOOL sampler=d.Type==D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
    struct desc_heap *h = HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof *h +
            (sampler ? sizeof(UINT64)*d.NumDescriptors : 0));
    if (!h) return E_OUTOFMEMORY;
    h->count = d.NumDescriptors;
    h->sampler=sampler;
    if (d.Type == 0 && d.NumDescriptors == DESC_STARTUP_COUNT) d.NumDescriptors = DESC_BANK;
    HRESULT hr = ID3D12Device_CreateDescriptorHeap(host,&d,&IID_ID3D12DescriptorHeap,(void **)&h->host);
    if (FAILED(hr)) { HeapFree(GetProcessHeap(),0,h); return hr; }
    h->iface.lpVtbl = &desc_vtbl.unknown;
    h->refs = 1;
    if (h->count != d.NumDescriptors) {
        d.NumDescriptors = h->count - DESC_BANK;
        hr = ID3D12Device_CreateDescriptorHeap(host,&d,&IID_ID3D12DescriptorHeap,(void **)&h->tail);
        if (FAILED(hr)) { desc_release(&h->iface); return hr; }
    }
    h->stride = descriptor_stride(device,d.Type);
    h->host_stride = ID3D12Device_GetDescriptorHandleIncrementSize(host,d.Type);
    h->visible = d.Flags == 1;
    SIZE_T size = (SIZE_T)h->stride * h->count;
    h->rtv = d.Type==D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    h->dsv = d.Type==D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    if (!d.Type || h->rtv || h->dsv) {
        if (!(h->records = HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(*h->records)*h->count))) {
            desc_release(&h->iface); return E_OUTOFMEMORY;
        }
    }
    if (!d.Type) {
        void *(WINAPI *alloc)(SIZE_T,UINT64) = NULL;
        if (GetModuleHandleExW(0,L"xmem.dll",&h->xmem)) {
            alloc = (void *)GetProcAddress(h->xmem,"XMemAlloc");
            h->xfree = (void *)GetProcAddress(h->xmem,"XMemFree");
        }
        if (!alloc || !h->xfree) { desc_release(&h->iface); return E_NOTIMPL; }
        /* This must be the last fallible allocation: the free interceptor is installed at final Release. */
        h->start = (SIZE_T)alloc(size,DESC_ATTR);
    } else h->start = (SIZE_T)VirtualAlloc(NULL,size,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if (!h->start) { desc_release(&h->iface); return E_OUTOFMEMORY; }
    if (h->records || h->sampler) {
        h->owner = CONTAINING_RECORD(device,struct xbox_device,iface);
        IUnknown_AddRef(device);
        AcquireSRWLockExclusive(&h->owner->descriptor_lock);
        struct desc_heap **head=heap_list(h->owner,h);
        h->next = *head;
        *head = h;
        ReleaseSRWLockExclusive(&h->owner->descriptor_lock);
    }
    *out = &h->iface;
    return S_OK;
}

/* Caller holds this heap until consuming commands retire. This resolves a slot address,
 * not the contents of a title slot. Only use the result with an evidenced descriptor writer. */
static HRESULT descriptor_range(IUnknown *i,SIZE_T slot,UINT count,ID3D12DescriptorHeap **bank,
        D3D12_CPU_DESCRIPTOR_HANDLE *cpu,D3D12_GPU_DESCRIPTOR_HANDLE *gpu)
{
    if (bank) *bank = NULL;
    if (cpu) cpu->ptr = 0;
    if (gpu) gpu->ptr = 0;
    if (!i || i->lpVtbl != &desc_vtbl.unknown || (!cpu && !gpu)) return E_INVALIDARG;
    struct desc_heap *h = CONTAINING_RECORD(i,struct desc_heap,iface);
    if (slot < h->start || (slot-h->start) % h->stride || (gpu && !h->visible)) return E_INVALIDARG;
    SIZE_T n = (slot-h->start) / h->stride;
    if (!count || n >= h->count || count > h->count-n) return E_INVALIDARG;
    ID3D12DescriptorHeap *host=h->host;
    if (h->tail) {
        if (n >= DESC_BANK) { n-=DESC_BANK; host=h->tail; }
        else if (count > DESC_BANK-n) return E_NOTIMPL; /* cannot bind this as one table */
    }
    if (bank) *bank=host; /* borrowed, same lifetime as the title heap */
    if (cpu) {
        host->lpVtbl->GetCPUDescriptorHandleForHeapStart(host,cpu);
        cpu->ptr += n * h->host_stride;
    }
    if (gpu) {
        host->lpVtbl->GetGPUDescriptorHandleForHeapStart(host,gpu);
        gpu->ptr += n * h->host_stride;
    }
    return S_OK;
}

/* Resolve the initial pair by guest device identity, not by a shared desktop device. */
HRESULT xs_initial_heap_banks(IUnknown *owner,IUnknown *const pair[2],ID3D12DescriptorHeap *banks[2])
{
    struct xbox_device *d=CONTAINING_RECORD(owner,struct xbox_device,iface);
    HRESULT hr=E_INVALIDARG;
    AcquireSRWLockShared(&d->descriptor_lock);
    struct desc_heap *r=d->heaps,*s=d->sampler_heaps;
    while (r && &r->iface!=pair[0]) r=r->next;
    while (s && &s->iface!=pair[1]) s=s->next;
    if (r && r->visible && r->count==DESC_STARTUP_COUNT && r->tail && s && s->visible && s->count==128) {
        D3D12_GPU_DESCRIPTOR_HANDLE gpu;
        hr=descriptor_range(&r->iface,r->start,1,&banks[0],NULL,&gpu);
        if (SUCCEEDED(hr)) hr=descriptor_range(&s->iface,s->start,1,&banks[1],NULL,&gpu);
    }
    ReleaseSRWLockShared(&d->descriptor_lock);
    return hr;
}

static HRESULT descriptor_slot(IUnknown *i,SIZE_T slot,D3D12_CPU_DESCRIPTOR_HANDLE *cpu,D3D12_GPU_DESCRIPTOR_HANDLE *gpu)
{
    return descriptor_range(i,slot,1,NULL,cpu,gpu);
}

/* Raw bank access cannot represent a refresh-required texture. */
HRESULT xs_descriptor_range(IUnknown *i,SIZE_T slot,UINT count,ID3D12DescriptorHeap **bank,
        D3D12_CPU_DESCRIPTOR_HANDLE *cpu,D3D12_GPU_DESCRIPTOR_HANDLE *gpu)
{
    HRESULT hr=descriptor_range(i,slot,count,bank,cpu,gpu);
    if (FAILED(hr)) return hr;
    struct desc_heap *h=CONTAINING_RECORD(i,struct desc_heap,iface);
    if (!h->records || h->rtv || h->dsv) return S_OK;
    AcquireSRWLockShared(&h->owner->descriptor_lock);
    SIZE_T first=(slot-h->start)/h->stride;
    for (UINT n=0;n<count;n++) if (tile_view(h->records[first+n].kind) || refresh_required(h->records[first+n].kind)) {
        if (bank) *bank=NULL;
        if (cpu) cpu->ptr=0;
        if (gpu) gpu->ptr=0;
        hr=E_NOTIMPL; break;
    }
    ReleaseSRWLockShared(&h->owner->descriptor_lock);
    return hr;
}

HRESULT xs_descriptor_slot(IUnknown *i,SIZE_T slot,D3D12_CPU_DESCRIPTOR_HANDLE *cpu,D3D12_GPU_DESCRIPTOR_HANDLE *gpu)
{
    return xs_descriptor_range(i,slot,1,NULL,cpu,gpu);
}
