#define COBJMACROS
#define CONST_VTABLE
#include "queue.h"
#include <stddef.h>
#include <stdint.h>
#ifdef SCARLETT_DEVICE_BOOTSTRAP
#include "bootstrap_guard.h"
#include "present.h"
#include "../frame.h"
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
#ifdef SCARLETT_DEVICE_BOOTSTRAP
    struct {
        SRWLOCK lock;
        IUnknown *owner;
        IDXGISwapChain3 *chain;
        IUnknown *keys[2];
        ID3D12Resource *buffers[2];
        UINT width, height;
    } display;
#endif
};

struct qtable {
    struct xs_queue_prefix prefix;
    void *other[13];
    HRESULT (WINAPI *present)(IUnknown *, UINT, const void *, const void *);
    void *other108[2];
    void (WINAPI *execute)(IUnknown *, UINT, IUnknown *const *, UINT64, UINT);
};
struct ftable {
    IUnknownVtbl prefix;
    void *other[9];
    const volatile UINT64 *(WINAPI *address)(IUnknown *);
};
_Static_assert(offsetof(struct ftable, address) == 0x60, "x64 monitored fence address");

struct monitored_fence {
    struct adapter a;
    ID3D12Device *device; /* Kept alive by the host fence. */
    PTP_TIMER timer;
    SRWLOCK lock;
    BOOL active;
    char separate_from_sampler_lock[64]; /* Do not wake MONITORX on an unchanged sample. */
    volatile UINT64 value;
};
_Static_assert(offsetof(struct monitored_fence, value) % 8 == 0, "atomic x64 reader");
static const struct qtable qt;
static const struct ftable ft;
_Static_assert(offsetof(struct qtable, execute) == 0x118, "x64 queue submit");
_Static_assert(offsetof(struct qtable, present) == 0x100, "x64 PresentX");
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
        if (iface->lpVtbl == &fence_prefix) {
            struct monitored_fence *f = CONTAINING_RECORD(a, struct monitored_fence, a);
            SetThreadpoolTimer(f->timer, NULL, 0, 0);
            WaitForThreadpoolTimerCallbacks(f->timer, TRUE);
            CloseThreadpoolTimer(f->timer);
            ID3D12Fence_Release(a->host.fence);
        }
#ifdef SCARLETT_DEVICE_BOOTSTRAP
        else if (iface->lpVtbl == &at.prefix) ID3D12CommandAllocator_Release(a->host.allocator);
        else if (iface->lpVtbl == &st.prefix) ID3D12CommandSignature_Release(a->host.signature);
#endif
        else {
#ifdef SCARLETT_DEVICE_BOOTSTRAP
            for (unsigned j=0;j<2;++j) if (a->display.keys[j]) {
                IUnknown_Release(a->display.keys[j]);
                ID3D12Resource_Release(a->display.buffers[j]);
            }
            if (a->display.chain) IDXGISwapChain3_Release(a->display.chain);
            if (a->display.owner) IUnknown_Release(a->display.owner);
#endif
            ID3D12CommandQueue_Release(a->host.queue);
        }
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

/* Called with the sampling lock. Never substitute a submitted value or apply a high-water mark. */
static void sample_completed(struct monitored_fence *f)
{
    UINT64 value = ID3D12Fence_GetCompletedValue(f->a.host.fence);
    if (FAILED(ID3D12Device_GetDeviceRemovedReason(f->device))) value = UINT64_MAX;
    if (f->value != value) InterlockedExchange64((volatile LONG64 *)&f->value, (LONG64)value);
}

static void CALLBACK sample_timer(PTP_CALLBACK_INSTANCE instance, void *context, PTP_TIMER timer)
{
    struct monitored_fence *f = context;
    (void)instance; (void)timer;
    if (TryAcquireSRWLockExclusive(&f->lock)) {
        sample_completed(f);
        ReleaseSRWLockExclusive(&f->lock);
    }
}

static const volatile UINT64 * WINAPI fence_address(IUnknown *iface)
{
    struct monitored_fence *f = CONTAINING_RECORD(iface, struct monitored_fence, a.iface);
    AcquireSRWLockExclusive(&f->lock);
    sample_completed(f);
    if (!f->active) {
        FILETIME due = { (DWORD)-10000, (DWORD)-1 }; /* Relative 1 ms. */
        SetThreadpoolTimer(f->timer, &due, 1, 0);
        f->active = TRUE;
    }
    ReleaseSRWLockExclusive(&f->lock);
    return &f->value;
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
#include "present_impl.h"
#endif

static const struct qtable qt = {
    .prefix = {
        .unknown = {query, addref, release}, .signal = queue_signal, .frequency = frequency,
#ifdef SCARLETT_DEVICE_BOOTSTRAP
        .reserved18 = { [0 ... 12] = (void *)xs_stop }, .reserved88 = (void *)xs_stop,
#endif
    },
#ifdef SCARLETT_DEVICE_BOOTSTRAP
    .other = { [0 ... 12] = (void *)xs_stop }, .present = queue_present,
    .other108 = { (void *)xs_stop, (void *)xs_stop }, .execute = startup_submit,
#endif
};
static const struct ftable ft = {
    .prefix = {query, addref, release}, .address = fence_address,
#ifdef SCARLETT_DEVICE_BOOTSTRAP
    .other = { [0 ... 8] = (void *)xs_stop },
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
    if (!f) return E_INVALIDARG;
    struct monitored_fence *m = CONTAINING_RECORD(fence, struct monitored_fence, a.iface);
    HRESULT hr = ID3D12Fence_Signal(f, value);
    AcquireSRWLockExclusive(&m->lock);
    if (m->active) sample_completed(m);
    ReleaseSRWLockExclusive(&m->lock);
    return hr;
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
    a = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, desc ? sizeof(*a) : sizeof(struct monitored_fence));
    if (!a) return E_OUTOFMEMORY;
    hr = desc ? ID3D12Device_CreateCommandQueue(host, desc, &IID_ID3D12CommandQueue, (void **)&a->host.queue) :
            ID3D12Device_CreateFence(host, 0, D3D12_FENCE_FLAG_SHARED, &IID_ID3D12Fence, (void **)&a->host.fence);
    if (FAILED(hr)) {
        HeapFree(GetProcessHeap(), 0, a);
        return hr;
    }
    if (!desc) {
        struct monitored_fence *f = CONTAINING_RECORD(a, struct monitored_fence, a);
        f->device = host;
        f->timer = CreateThreadpoolTimer(sample_timer, f, NULL);
        if (!f->timer) {
            hr = HRESULT_FROM_WIN32(GetLastError());
            ID3D12Fence_Release(a->host.fence);
            HeapFree(GetProcessHeap(), 0, a);
            return hr;
        }
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
