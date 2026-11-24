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
    } host;
};

struct qtable {
    struct xs_queue_prefix prefix;
    void *other[17]; /* Through observed +0x118. */
};
struct ftable {
    IUnknownVtbl prefix;
    void *other[10]; /* Through observed +0x60. */
};
static const struct qtable qt;
static const struct ftable ft;
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

static const struct qtable qt = {
    .prefix = {
        .unknown = {query, addref, release}, .signal = queue_signal, .frequency = frequency,
#ifdef SCARLETT_DEVICE_BOOTSTRAP
        .reserved18 = { [0 ... 12] = (void *)xs_stop }, .reserved88 = (void *)xs_stop,
#endif
    },
#ifdef SCARLETT_DEVICE_BOOTSTRAP
    .other = { [0 ... 16] = (void *)xs_stop },
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
