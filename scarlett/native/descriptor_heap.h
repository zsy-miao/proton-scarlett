/* Included by the bootstrap device. No desktop heap or descriptor handle crosses the title ABI. */
struct desc_heap {
    IUnknown iface;
    LONG refs;
    ID3D12DescriptorHeap *host;
    SIZE_T start;
    UINT count, stride, host_stride;
    BOOL visible;
    HMODULE xmem;
    void (WINAPI *xfree)(void *,UINT64);
};
enum { DESC_ATTR = (0x80 << 16) | 4 };
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
        ID3D12DescriptorHeap_Release(h->host);
        if (h->xfree && h->start) h->xfree((void *)h->start,DESC_ATTR);
        else if (h->start) VirtualFree((void *)h->start,0,MEM_RELEASE);
        if (h->xmem) FreeLibrary(h->xmem);
        HeapFree(GetProcessHeap(),0,h);
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
    if (d.NodeMask != 1 || !((d.Type == 0 && d.Flags == 1) ||
        (d.Type == 1 && d.NumDescriptors == 128 && (UINT)d.Flags <= 1) ||
        (d.Type == 2 && d.NumDescriptors == 4096 && !d.Flags) ||
        (d.Type == 3 && d.NumDescriptors == 2048 && !d.Flags))) return E_NOTIMPL;
    struct desc_heap *h = HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof *h);
    if (!h) return E_OUTOFMEMORY;
    HRESULT hr = ID3D12Device_CreateDescriptorHeap(host,&d,&IID_ID3D12DescriptorHeap,(void **)&h->host);
    if (FAILED(hr)) { HeapFree(GetProcessHeap(),0,h); return hr; }
    h->iface.lpVtbl = &desc_vtbl.unknown;
    h->refs = 1;
    h->stride = descriptor_stride(device,d.Type);
    h->host_stride = ID3D12Device_GetDescriptorHandleIncrementSize(host,d.Type);
    h->count = d.NumDescriptors;
    h->visible = d.Flags == 1;
    SIZE_T size = (SIZE_T)h->stride * h->count;
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
    *out = &h->iface;
    return S_OK;
}

/* Caller holds this heap until consuming commands retire. This resolves a slot address,
 * not the contents of a title slot. Only use the result with an evidenced descriptor writer. */
HRESULT xs_descriptor_slot(IUnknown *i,SIZE_T slot,D3D12_CPU_DESCRIPTOR_HANDLE *cpu,D3D12_GPU_DESCRIPTOR_HANDLE *gpu)
{
    if (cpu) cpu->ptr = 0;
    if (gpu) gpu->ptr = 0;
    if (!i || i->lpVtbl != &desc_vtbl.unknown || (!cpu && !gpu)) return E_INVALIDARG;
    struct desc_heap *h = CONTAINING_RECORD(i,struct desc_heap,iface);
    if (slot < h->start || (slot-h->start) % h->stride || (gpu && !h->visible)) return E_INVALIDARG;
    SIZE_T n = (slot-h->start) / h->stride;
    if (n >= h->count) return E_INVALIDARG;
    if (cpu) {
        h->host->lpVtbl->GetCPUDescriptorHandleForHeapStart(h->host,cpu);
        cpu->ptr += n * h->host_stride;
    }
    if (gpu) {
        h->host->lpVtbl->GetGPUDescriptorHandleForHeapStart(h->host,gpu);
        gpu->ptr += n * h->host_stride;
    }
    return S_OK;
}
