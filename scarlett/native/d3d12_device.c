#define COBJMACROS
#define CONST_VTABLE
#define INITGUID
#include <windows.h>
#include <d3d12.h>
#ifdef SCARLETT_DEVICE_BOOTSTRAP
#include "queue/queue.h"
#include "queue/present.h"
#include "queue/bootstrap_guard.h"
#include "frame.h"
#include <stddef.h>
#include <stdio.h>
#endif

#ifdef SCARLETT_DEVICE_BOOTSTRAP
struct frame_record {
    UINT64 token;
    LONGLONG origin, target;
    IDXGISwapChain *chain;
    UINT present_id;
    HRESULT status; /* S_FALSE: issued but not presented; E_PENDING: present submitted. */
    BOOL presenting;
};
struct frame_clock {
    SRWLOCK lock;
    CONDITION_VARIABLE change;
    LONGLONG step, due, freq, base;
    UINT period, registered, epoch, cursor;
    struct frame_record history[16];
};
static void frame_clear(struct frame_clock *f)
{
    for (unsigned i=0; i<ARRAYSIZE(f->history); ++i)
        if (f->history[i].chain) IDXGISwapChain_Release(f->history[i].chain);
    memset(f->history, 0, sizeof(f->history));
}
#endif

struct xbox_device {
    IUnknown iface;
    LONG refs;
    ID3D12Device *host;
#ifdef SCARLETT_DEVICE_BOOTSTRAP
    BYTE inline_descriptor_mode;
    struct frame_clock clock;
#endif
};

#ifdef SCARLETT_DEVICE_BOOTSTRAP
_Static_assert(offsetof(struct xbox_device,inline_descriptor_mode)==0x18,"inline descriptor flag");
#include "descriptor_heap.h"
#endif

/* Private host-side operations. No console method returns a host interface. */
static inline HRESULT device_heap_properties(D3D12_HEAP_PROPERTIES *out,
        struct xbox_device *device, UINT node_mask, D3D12_HEAP_TYPE type)
{
    if (!out) return E_POINTER;
    *out = (D3D12_HEAP_PROPERTIES){0};
    if (node_mask != 1 || (type != D3D12_HEAP_TYPE_DEFAULT &&
            type != D3D12_HEAP_TYPE_UPLOAD && type != D3D12_HEAP_TYPE_READBACK))
        return E_INVALIDARG;
    /* MinGW's typed host method takes This before the hidden return destination. */
    device->host->lpVtbl->GetCustomHeapProperties(device->host, out, node_mask, type);
    return S_OK;
}

static inline HRESULT device_direct_queue(struct xbox_device *device,
        const D3D12_COMMAND_QUEUE_DESC *desc, ID3D12CommandQueue **out)
{
    if (!out) return E_POINTER;
    *out = NULL;
    if (!desc || desc->Type != D3D12_COMMAND_LIST_TYPE_DIRECT ||
            desc->Priority != D3D12_COMMAND_QUEUE_PRIORITY_NORMAL ||
            desc->Flags != D3D12_COMMAND_QUEUE_FLAG_NONE || desc->NodeMask != 1)
        return E_INVALIDARG;
    return ID3D12Device_CreateCommandQueue(device->host, desc, &IID_ID3D12CommandQueue, (void **)out);
}

static ULONG WINAPI device_addref(IUnknown *iface)
{
    struct xbox_device *device = CONTAINING_RECORD(iface, struct xbox_device, iface);
    return InterlockedIncrement(&device->refs);
}

static ULONG WINAPI device_release(IUnknown *iface)
{
    struct xbox_device *device = CONTAINING_RECORD(iface, struct xbox_device, iface);
    ULONG refs = InterlockedDecrement(&device->refs);
    if (!refs) {
#ifdef SCARLETT_DEVICE_BOOTSTRAP
        frame_clear(&device->clock);
#endif
        ID3D12Device_Release(device->host);
        HeapFree(GetProcessHeap(), 0, device);
    }
    return refs;
}

static const IUnknownVtbl device_vtbl;
#ifdef SCARLETT_DEVICE_BOOTSTRAP
static const IID bootstrap_iid = {0x5405c344,0xd457,0x444e,{0xb4,0xdd,0x23,0x66,0xe4,0x5a,0xee,0x39}};

static HRESULT WINAPI bootstrap_queue(IUnknown *iface, const D3D12_COMMAND_QUEUE_DESC *d,
        REFIID iid, void **out)
{
    IUnknown *queue = NULL;
    HRESULT hr;
    if (!out) return E_POINTER;
    *out = NULL;
    if (!iid || !d) return E_INVALIDARG;
    if (!IsEqualIID(iid, &IID_ID3D12CommandQueue)) return E_NOINTERFACE;
    if (d->Type != D3D12_COMMAND_LIST_TYPE_DIRECT || d->Priority != D3D12_COMMAND_QUEUE_PRIORITY_NORMAL ||
            d->Flags != D3D12_COMMAND_QUEUE_FLAG_NONE || d->NodeMask != 1) return E_NOTIMPL;
    hr = xs_console_direct_queue(CONTAINING_RECORD(iface, struct xbox_device, iface)->host, iface, &queue);
    *out = queue;
    return hr;
}

static HRESULT WINAPI bootstrap_fence(IUnknown *iface, UINT64 initial, D3D12_FENCE_FLAGS flags,
        REFIID iid, void **out)
{
    IUnknown *fence = NULL;
    HRESULT hr;
    if (!out) return E_POINTER;
    *out = NULL;
    if (!iid) return E_INVALIDARG;
    if (!IsEqualIID(iid, &IID_ID3D12Fence)) return E_NOINTERFACE;
    if (initial || flags != D3D12_FENCE_FLAG_SHARED) return E_NOTIMPL;
    hr = xs_shared_fence(CONTAINING_RECORD(iface, struct xbox_device, iface)->host, &fence);
    *out = fence;
    return hr;
}

static HRESULT WINAPI bootstrap_allocator(IUnknown *iface, D3D12_COMMAND_LIST_TYPE type, REFIID iid, void **out)
{
    IUnknown *allocator = NULL;
    if (!out) return E_POINTER;
    *out = NULL;
    if (!iid) return E_INVALIDARG;
    if (!IsEqualIID(iid, &IID_ID3D12CommandAllocator)) return E_NOINTERFACE;
    HRESULT hr = xs_allocator(CONTAINING_RECORD(iface, struct xbox_device, iface)->host, type, &allocator);
    *out = allocator;
    return hr;
}

static HRESULT WINAPI bootstrap_list(IUnknown *iface, const void *desc, IUnknown *allocator,
        IUnknown *initial_state, REFIID iid, void **out)
{
    BOOL pooled;
    UINT words[4];
    SIZE_T copied;
    if (!out) return E_POINTER;
    *out = NULL;
    if (!desc || !allocator || !iid) return E_INVALIDARG;
    if (!ReadProcessMemory(GetCurrentProcess(), desc, words, sizeof(words), &copied) || copied != sizeof(words))
        return E_POINTER;
    pooled = IsEqualIID(iid, &xs_pool_list_id);
    if (!pooled && !IsEqualIID(iid, &xs_sync_list_id)) {
        char msg[144];
        snprintf(msg, sizeof(msg), "scarlett: device+0x248 unsupported list IID Data1=%08lx desc=(%u,%u,%u,%u)\n",
                 iid->Data1, words[0], words[1], words[2], words[3]);
        OutputDebugStringA(msg);
        return E_NOINTERFACE;
    }
    if (!xs_allocator_host(allocator, words[0])) return E_INVALIDARG;
    if (initial_state || words[1] != (pooled ? 6u : 2u) || words[2] != 1 || words[3]) return E_NOTIMPL;
    IUnknown *list = NULL;
    HRESULT hr = xs_startup_list(CONTAINING_RECORD(iface, struct xbox_device, iface)->host,
            words[0], allocator, pooled, &list);
    *out = list;
    return hr;
}

static D3D12_HEAP_PROPERTIES *WINAPI bootstrap_heap(D3D12_HEAP_PROPERTIES *out, IUnknown *iface,
        UINT mask, D3D12_HEAP_TYPE type)
{
    if (FAILED(device_heap_properties(out, CONTAINING_RECORD(iface, struct xbox_device, iface), mask, type))
            || out->Type != D3D12_HEAP_TYPE_CUSTOM || !out->CreationNodeMask || !out->VisibleNodeMask)
        xs_stop(); /* Aggregate return has no HRESULT channel. */
    return out;
}

static HRESULT WINAPI bootstrap_extended(IUnknown *iface, const void *desc, REFIID iid, void **out)
{
    UINT words[4];
    SIZE_T copied;
    IUnknown *queue = NULL;
    HRESULT hr;
    if (!out) return E_POINTER;
    *out = NULL;
    if (!desc || !iid) return E_INVALIDARG;
    if (!IsEqualIID(iid, &IID_ID3D12CommandQueue)) return E_NOINTERFACE;
    if (!ReadProcessMemory(GetCurrentProcess(), desc, words, sizeof(words), &copied) || copied != sizeof(words))
        return E_POINTER;
    /* The host schedules each logical stream; physical engine placement is not guaranteed. */
    if (words[0] != D3D12_COMMAND_LIST_TYPE_COMPUTE || words[1] ||
        !((words[2] <= 3 && words[3] == 0) || (words[2] == 0 && words[3] == 1))) {
        char msg[128];
        snprintf(msg, sizeof(msg), "scarlett: device+0x180 unsupported queue descriptor (%#x,%#x,%#x,%#x)\n",
                 words[0], words[1], words[2], words[3]);
        OutputDebugStringA(msg);
        return E_NOTIMPL;
    }
    hr = xs_compute_queue(CONTAINING_RECORD(iface, struct xbox_device, iface)->host, &queue);
    *out = queue;
    return hr;
}

static HRESULT WINAPI bootstrap_signature(IUnknown *iface, const D3D12_COMMAND_SIGNATURE_DESC *desc,
        IUnknown *root, REFIID iid, void **out)
{
    D3D12_COMMAND_SIGNATURE_DESC d;
    D3D12_INDIRECT_ARGUMENT_TYPE type;
    SIZE_T n;
    if (!out) return E_POINTER;
    *out = NULL;
    if (!desc || !iid) return E_INVALIDARG;
    if (!IsEqualIID(iid, &IID_ID3D12CommandSignature)) return E_NOINTERFACE;
    if (root) return E_NOTIMPL;
    /* NodeMask is the last field written. Do not read padding or inactive union members. */
    const SIZE_T size = offsetof(D3D12_COMMAND_SIGNATURE_DESC, NodeMask) + sizeof(d.NodeMask);
    if (!ReadProcessMemory(GetCurrentProcess(), desc, &d, size, &n) || n != size) return E_POINTER;
    if (d.NodeMask || d.NumArgumentDescs != 1) return E_NOTIMPL;
    if (!ReadProcessMemory(GetCurrentProcess(), d.pArgumentDescs, &type, sizeof(type), &n) || n != sizeof(type))
        return E_POINTER;
    if (type == XS_INDIRECT_TYPE_69) {
        /* Only this form has a known second dword, explicitly zero at the call site. */
        UINT word;
        if (!ReadProcessMemory(GetCurrentProcess(), (const BYTE *)d.pArgumentDescs + sizeof(type),
                &word, sizeof(word), &n) || n != sizeof(word)) return E_POINTER;
        if (word) return E_NOTIMPL;
    }
    IUnknown *signature = NULL;
    HRESULT hr = xs_signature(CONTAINING_RECORD(iface, struct xbox_device, iface)->host, type, d.ByteStride, &signature);
    *out = signature;
    return hr;
}

#include "frame_clock.h"

struct bootstrap_vtbl {
    IUnknownVtbl unknown;
    xs_stop_fn gap18[5];
    __typeof__(&bootstrap_queue) queue;
    __typeof__(&bootstrap_allocator) allocator;
    xs_stop_fn gap50[4];
    __typeof__(&descriptor_heap) descriptors;
    __typeof__(&descriptor_stride) stride;
    xs_stop_fn gap80[10];
    __typeof__(&bootstrap_heap) heap;
    xs_stop_fn gapd8[9];
    __typeof__(&bootstrap_fence) fence;
    xs_stop_fn gap128[4];
    __typeof__(&bootstrap_signature) signature;
    xs_stop_fn gap150[6];
    __typeof__(&bootstrap_extended) extended;
    xs_stop_fn gap188[24];
    __typeof__(&bootstrap_list) list;
    xs_stop_fn gap250[4];
    __typeof__(&bootstrap_frame_interval) frame_interval;
    __typeof__(&bootstrap_frame_event) frame_event;
    __typeof__(&bootstrap_frame_wait) frame_wait;
    xs_stop_fn gap288;
};
_Static_assert(offsetof(struct bootstrap_vtbl, queue) == 0x40 &&
               offsetof(struct bootstrap_vtbl, descriptors) == 0x70 &&
               offsetof(struct bootstrap_vtbl, stride) == 0x78 &&
               offsetof(struct bootstrap_vtbl, frame_interval) == 0x270 &&
               offsetof(struct bootstrap_vtbl, frame_event) == 0x278 &&
               offsetof(struct bootstrap_vtbl, frame_wait) == 0x280 &&
               offsetof(struct bootstrap_vtbl, allocator) == 0x48 &&
               offsetof(struct bootstrap_vtbl, list) == 0x248 &&
               offsetof(struct bootstrap_vtbl, heap) == 0xd0 &&
               offsetof(struct bootstrap_vtbl, fence) == 0x120 &&
               offsetof(struct bootstrap_vtbl, signature) == 0x148 &&
               offsetof(struct bootstrap_vtbl, extended) == 0x180, "bootstrap x64 ABI");
static const struct bootstrap_vtbl bootstrap_vtbl;

static HRESULT bootstrap_params(const void *params)
{
    DWORD w[20];
    SIZE_T copied;
    if (!ReadProcessMemory(GetCurrentProcess(), params, w, sizeof(w), &copied) || copied != sizeof(w))
        return E_POINTER;
    if (w[0] != 0x20004 || w[2] != 0x100000 || w[8] > 1 || w[9] > 1 ||
            w[10] != 1 || w[15] != 0x20) return E_NOTIMPL;
    for (unsigned i = 0; i < ARRAYSIZE(w); ++i)
        if (i != 0 && i != 2 && i != 8 && i != 9 && i != 10 && i != 15 && w[i]) return E_NOTIMPL;
    return S_OK;
}
#endif

static HRESULT WINAPI device_query(IUnknown *iface, REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    *out = NULL;
    if (!iid) return E_INVALIDARG;
    if (!IsEqualIID(iid, &IID_IUnknown)
#ifdef SCARLETT_DEVICE_BOOTSTRAP
            && !(iface->lpVtbl == &bootstrap_vtbl.unknown && IsEqualIID(iid, &bootstrap_iid))
#endif
            ) return E_NOINTERFACE;
    device_addref(iface);
    *out = iface;
    return S_OK;
}

static const IUnknownVtbl device_vtbl = { device_query, device_addref, device_release };
#ifdef SCARLETT_DEVICE_BOOTSTRAP
static const struct bootstrap_vtbl bootstrap_vtbl = {
    .unknown = { device_query, device_addref, device_release },
    .gap18 = { [0 ... 4] = xs_stop }, .queue = bootstrap_queue,
    .allocator = bootstrap_allocator, .gap50 = { [0 ... 3] = xs_stop },
    .descriptors = descriptor_heap, .stride = descriptor_stride, .gap80 = { [0 ... 9] = xs_stop }, .heap = bootstrap_heap,
    .gapd8 = { [0 ... 8] = xs_stop }, .fence = bootstrap_fence,
    .gap128 = { [0 ... 3] = xs_stop }, .signature = bootstrap_signature,
    .gap150 = { [0 ... 5] = xs_stop }, .extended = bootstrap_extended,
    .gap188 = { [0 ... 23] = xs_stop }, .list = bootstrap_list, .gap250 = { [0 ... 3] = xs_stop },
    .frame_interval = bootstrap_frame_interval, .frame_event = bootstrap_frame_event,
    .frame_wait = bootstrap_frame_wait, .gap288 = xs_stop
};
#endif

__declspec(dllexport) HRESULT WINAPI D3D12XboxCreateDevice(
        void *adapter, const void *params, REFIID iid, void **out)
{
    struct xbox_device *device;
    const IUnknownVtbl *vtbl = &device_vtbl;
    HRESULT hr;

    if (!out) return E_POINTER;
    *out = NULL;
    if (adapter || !params || !iid) return E_INVALIDARG;
#ifdef SCARLETT_DEVICE_BOOTSTRAP
    if (IsEqualIID(iid, &bootstrap_iid)) {
        if (FAILED(hr = bootstrap_params(params))) return hr;
        vtbl = &bootstrap_vtbl.unknown;
    } else
#endif
    if (!IsEqualIID(iid, &IID_IUnknown)) return E_NOINTERFACE;

    device = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*device));
    if (!device) return E_OUTOFMEMORY;
    hr = D3D12CreateDevice(NULL, D3D_FEATURE_LEVEL_12_0, &IID_ID3D12Device, (void **)&device->host);
    if (FAILED(hr)) {
        HeapFree(GetProcessHeap(), 0, device);
        return hr;
    }
    device->iface.lpVtbl = vtbl;
    device->refs = 1;
    *out = &device->iface;
    return S_OK;
}
