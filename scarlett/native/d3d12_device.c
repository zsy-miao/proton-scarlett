#define COBJMACROS
#define CONST_VTABLE
#define INITGUID
#include <windows.h>
#include <d3d12.h>

struct xbox_device {
    IUnknown iface;
    LONG refs;
    ID3D12Device *host;
};

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
        ID3D12Device_Release(device->host);
        HeapFree(GetProcessHeap(), 0, device);
    }
    return refs;
}

static HRESULT WINAPI device_query(IUnknown *iface, REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    *out = NULL;
    if (!iid) return E_INVALIDARG;
    if (!IsEqualIID(iid, &IID_IUnknown)) return E_NOINTERFACE;
    device_addref(iface);
    *out = iface;
    return S_OK;
}

static const IUnknownVtbl device_vtbl = { device_query, device_addref, device_release };

__declspec(dllexport) HRESULT WINAPI D3D12XboxCreateDevice(
        void *adapter, const void *params, REFIID iid, void **out)
{
    struct xbox_device *device;
    HRESULT hr;

    if (!out) return E_POINTER;
    *out = NULL;
    if (adapter || !params || !iid) return E_INVALIDARG;
    /* No console device vtable has been verified. Never delegate these IIDs to the host. */
    if (!IsEqualIID(iid, &IID_IUnknown)) return E_NOINTERFACE;

    device = HeapAlloc(GetProcessHeap(), 0, sizeof(*device));
    if (!device) return E_OUTOFMEMORY;
    hr = D3D12CreateDevice(NULL, D3D_FEATURE_LEVEL_12_0, &IID_ID3D12Device, (void **)&device->host);
    if (FAILED(hr)) {
        HeapFree(GetProcessHeap(), 0, device);
        return hr;
    }
    device->iface.lpVtbl = &device_vtbl;
    device->refs = 1;
    *out = &device->iface;
    return S_OK;
}
