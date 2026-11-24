#define COBJMACROS
#define CONST_VTABLE
#include "queue.h"
#include <stddef.h>
#ifdef SCARLETT_DEVICE_BOOTSTRAP
#include "bootstrap_guard.h"
#endif

struct adapter {
    IUnknown iface;
    LONG refs;
    union {
        ID3D12CommandQueue *queue;
        ID3D12Fence *fence;
        ID3D12CommandAllocator *allocator;
        ID3D12CommandSignature *signature;
    } host;
    union {
        D3D12_COMMAND_LIST_TYPE allocator_type;
        D3D12_INDIRECT_ARGUMENT_TYPE signature_type;
    };
};

struct qtable {
    struct xs_queue_prefix prefix;
    void *other[16];
    void (WINAPI *execute)(IUnknown *, UINT, IUnknown *const *, UINT64, UINT);
};
struct ftable {
    IUnknownVtbl prefix;
    void *other[10]; /* Through observed +0x60. */
};
static const struct qtable qt;
static const struct ftable ft;
_Static_assert(offsetof(struct qtable, execute) == 0x118, "x64 queue submit");
#ifdef SCARLETT_DEVICE_BOOTSTRAP
struct atable {
    IUnknownVtbl prefix;
    xs_stop_fn gap18[5];
    HRESULT (WINAPI *reset)(IUnknown *);
    xs_stop_fn callback;
};
static const struct atable at;
static const struct {
    IUnknownVtbl prefix;
    xs_stop_fn inherited[5];
} st;
_Static_assert(offsetof(struct atable, reset) == 0x40 && offsetof(struct atable, callback) == 0x48,
        "x64 allocator slots");
#endif
#define queue_prefix qt.prefix
#define fence_prefix ft.prefix
_Static_assert(offsetof(struct xs_queue_prefix, signal) == 0x80, "x64 queue Signal");
_Static_assert(offsetof(struct xs_queue_prefix, frequency) == 0x90, "x64 queue frequency");

static ULONG WINAPI addref(IUnknown *iface)
{
    return InterlockedIncrement(&CONTAINING_RECORD(iface, struct adapter, iface)->refs);
}

static ULONG WINAPI release(IUnknown *iface)
{
    struct adapter *a = CONTAINING_RECORD(iface, struct adapter, iface);
    ULONG refs = InterlockedDecrement(&a->refs);
    if (!refs) {
        if (iface->lpVtbl == &fence_prefix) ID3D12Fence_Release(a->host.fence);
#ifdef SCARLETT_DEVICE_BOOTSTRAP
        else if (iface->lpVtbl == &at.prefix) ID3D12CommandAllocator_Release(a->host.allocator);
        else if (iface->lpVtbl == &st.prefix) ID3D12CommandSignature_Release(a->host.signature);
#endif
        else ID3D12CommandQueue_Release(a->host.queue);
        HeapFree(GetProcessHeap(), 0, a);
    }
    return refs;
}

static HRESULT WINAPI query(IUnknown *iface, REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    *out = NULL;
    if (!iid) return E_INVALIDARG;
    if (!IsEqualIID(iid, &IID_IUnknown)
#ifdef SCARLETT_DEVICE_BOOTSTRAP
            && !(iface->lpVtbl == &queue_prefix.unknown && IsEqualIID(iid, &IID_ID3D12CommandQueue))
            && !(iface->lpVtbl == &fence_prefix && IsEqualIID(iid, &IID_ID3D12Fence))
            && !(iface->lpVtbl == &at.prefix && IsEqualIID(iid, &IID_ID3D12CommandAllocator))
            && !(iface->lpVtbl == &st.prefix && IsEqualIID(iid, &IID_ID3D12CommandSignature))
#endif
            ) return E_NOINTERFACE;
    addref(iface);
    *out = iface;
    return S_OK;
}

static ID3D12CommandQueue *as_queue(IUnknown *iface)
{
    return iface && iface->lpVtbl == &queue_prefix.unknown ?
            CONTAINING_RECORD(iface, struct adapter, iface)->host.queue : NULL;
}

#ifdef SCARLETT_DEVICE_BOOTSTRAP
static const __typeof__(st) st = { {query, addref, release}, { [0 ... 4] = xs_stop } };

ID3D12CommandSignature *xs_signature_host(IUnknown *iface, D3D12_INDIRECT_ARGUMENT_TYPE original_type)
{
    if (!iface || iface->lpVtbl != &st.prefix) return NULL;
    struct adapter *a = CONTAINING_RECORD(iface, struct adapter, iface);
    return a->signature_type == original_type ? a->host.signature : NULL;
}

HRESULT xs_signature(ID3D12Device *host, D3D12_INDIRECT_ARGUMENT_TYPE type, UINT stride, IUnknown **out)
{
    if (!out) return E_POINTER;
    *out = NULL;
    if (!host) return E_INVALIDARG;
    UINT expected;
    /* The paired meshlet-culling paths consume the same dispatch record (command-signatures.md). */
    D3D12_INDIRECT_ARGUMENT_DESC arg = {.Type = type == XS_INDIRECT_TYPE_69 ? D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH : type};
    switch (arg.Type) {
        case D3D12_INDIRECT_ARGUMENT_TYPE_DRAW: expected = sizeof(D3D12_DRAW_ARGUMENTS); break;
        case D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED: expected = sizeof(D3D12_DRAW_INDEXED_ARGUMENTS); break;
        case D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH: expected = sizeof(D3D12_DISPATCH_ARGUMENTS); break;
        case D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH_MESH: {
            D3D12_FEATURE_DATA_D3D12_OPTIONS7 caps;
            HRESULT hr = ID3D12Device_CheckFeatureSupport(host, D3D12_FEATURE_D3D12_OPTIONS7, &caps, sizeof(caps));
            if (FAILED(hr)) return hr;
            if (!caps.MeshShaderTier) return DXGI_ERROR_UNSUPPORTED;
            expected = sizeof(D3D12_DISPATCH_MESH_ARGUMENTS);
            break;
        }
        default: return E_NOTIMPL;
    }
    if (stride != expected) return E_NOTIMPL;
    D3D12_COMMAND_SIGNATURE_DESC desc = {stride, 1, &arg, 0};
    struct adapter *a = HeapAlloc(GetProcessHeap(), 0, sizeof(*a));
    if (!a) return E_OUTOFMEMORY;
    HRESULT hr = ID3D12Device_CreateCommandSignature(host, &desc, NULL,
            &IID_ID3D12CommandSignature, (void **)&a->host.signature);
    if (FAILED(hr)) {
        HeapFree(GetProcessHeap(), 0, a);
        return hr;
    }
    a->iface.lpVtbl = &st.prefix;
    a->signature_type = type;
    a->refs = 1;
    *out = &a->iface;
    return S_OK;
}

static HRESULT WINAPI allocator_reset(IUnknown *iface)
{
    return ID3D12CommandAllocator_Reset(CONTAINING_RECORD(iface, struct adapter, iface)->host.allocator);
}

static const struct atable at = {
    .prefix = {query, addref, release}, .gap18 = { [0 ... 4] = xs_stop },
    .reset = allocator_reset, .callback = xs_stop
};

ID3D12CommandAllocator *xs_allocator_host(IUnknown *iface, D3D12_COMMAND_LIST_TYPE type)
{
    if (!iface || iface->lpVtbl != &at.prefix) return NULL;
    struct adapter *a = CONTAINING_RECORD(iface, struct adapter, iface);
    return a->allocator_type == type ? a->host.allocator : NULL;
}

HRESULT xs_allocator(ID3D12Device *host, D3D12_COMMAND_LIST_TYPE type, IUnknown **out)
{
    if (!out) return E_POINTER;
    *out = NULL;
    if (!host) return E_INVALIDARG;
    if (type != D3D12_COMMAND_LIST_TYPE_DIRECT && type != D3D12_COMMAND_LIST_TYPE_COMPUTE) return E_NOTIMPL;
    struct adapter *a = HeapAlloc(GetProcessHeap(), 0, sizeof(*a));
    if (!a) return E_OUTOFMEMORY;
    HRESULT hr = ID3D12Device_CreateCommandAllocator(host, type, &IID_ID3D12CommandAllocator,
            (void **)&a->host.allocator);
    if (FAILED(hr)) {
        HeapFree(GetProcessHeap(), 0, a);
        return hr;
    }
    a->refs = 1;
    a->allocator_type = type;
    a->iface.lpVtbl = &at.prefix;
    *out = &a->iface;
    return S_OK;
}
#endif

static ID3D12Fence *as_fence(IUnknown *iface)
{
    return iface && iface->lpVtbl == &fence_prefix ?
            CONTAINING_RECORD(iface, struct adapter, iface)->host.fence : NULL;
}

static HRESULT WINAPI queue_signal(IUnknown *queue, IUnknown *fence, UINT64 value)
{
    ID3D12CommandQueue *q = as_queue(queue);
    ID3D12Fence *f = as_fence(fence);
    return q && f ? ID3D12CommandQueue_Signal(q, f, value) : E_INVALIDARG;
}

static HRESULT WINAPI frequency(IUnknown *queue, UINT64 *out)
{
    ID3D12CommandQueue *q = as_queue(queue);
    if (!out) return E_POINTER;
    *out = 0;
    return q ? ID3D12CommandQueue_GetTimestampFrequency(q, out) : E_INVALIDARG;
}

#ifdef SCARLETT_DEVICE_BOOTSTRAP
#include "startup_list.h"
#endif

static const struct qtable qt = {
    .prefix = {
        .unknown = {query, addref, release}, .signal = queue_signal, .frequency = frequency,
#ifdef SCARLETT_DEVICE_BOOTSTRAP
        .reserved18 = { [0 ... 12] = (void *)xs_stop }, .reserved88 = (void *)xs_stop,
#endif
    },
#ifdef SCARLETT_DEVICE_BOOTSTRAP
    .other = { [0 ... 15] = (void *)xs_stop }, .execute = startup_submit,
#endif
};
static const struct ftable ft = {
    .prefix = {query, addref, release},
#ifdef SCARLETT_DEVICE_BOOTSTRAP
    .other = { [0 ... 9] = (void *)xs_stop },
#endif
};

HRESULT xs_queue_wait(IUnknown *queue, IUnknown *fence, UINT64 value)
{
    ID3D12CommandQueue *q = as_queue(queue);
    ID3D12Fence *f = as_fence(fence);
    return q && f ? ID3D12CommandQueue_Wait(q, f, value) : E_INVALIDARG;
}

HRESULT xs_cpu_signal(IUnknown *fence, UINT64 value)
{
    ID3D12Fence *f = as_fence(fence);
    return f ? ID3D12Fence_Signal(f, value) : E_INVALIDARG;
}

HRESULT xs_fence_event(IUnknown *fence, UINT64 value, HANDLE event)
{
    ID3D12Fence *f = as_fence(fence);
    return f ? ID3D12Fence_SetEventOnCompletion(f, value, event) : E_INVALIDARG;
}

static HRESULT create(ID3D12Device *host, const D3D12_COMMAND_QUEUE_DESC *desc, IUnknown **out)
{
    struct adapter *a;
    HRESULT hr;
    if (!out) return E_POINTER;
    *out = NULL;
    if (!host) return E_INVALIDARG;
    a = HeapAlloc(GetProcessHeap(), 0, sizeof(*a));
    if (!a) return E_OUTOFMEMORY;
    hr = desc ? ID3D12Device_CreateCommandQueue(host, desc, &IID_ID3D12CommandQueue, (void **)&a->host.queue) :
            ID3D12Device_CreateFence(host, 0, D3D12_FENCE_FLAG_SHARED, &IID_ID3D12Fence, (void **)&a->host.fence);
    if (FAILED(hr)) {
        HeapFree(GetProcessHeap(), 0, a);
        return hr;
    }
    a->iface.lpVtbl = desc ? &queue_prefix.unknown : &fence_prefix;
    a->refs = 1;
    *out = &a->iface;
    return S_OK;
}

HRESULT xs_direct_queue(ID3D12Device *host, IUnknown **out)
{
    const D3D12_COMMAND_QUEUE_DESC desc = {D3D12_COMMAND_LIST_TYPE_DIRECT, 0, 0, 1};
    return create(host, &desc, out);
}

HRESULT xs_compute_queue(ID3D12Device *host, IUnknown **out)
{
    const D3D12_COMMAND_QUEUE_DESC desc = {D3D12_COMMAND_LIST_TYPE_COMPUTE, 0, 0, 1};
    return create(host, &desc, out);
}

HRESULT xs_shared_fence(ID3D12Device *host, IUnknown **out)
{
    return create(host, NULL, out);
}
