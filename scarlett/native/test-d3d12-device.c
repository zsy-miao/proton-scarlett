#ifdef SCARLETT_DEVICE_PRIVATE_TEST
/* White-box build keeps the helper symbols out of the public DLL ABI. */
#include "d3d12_device.c"
#else
#define COBJMACROS
#define INITGUID
#include <windows.h>
#include <d3d12.h>
__declspec(dllimport) HRESULT WINAPI D3D12XboxCreateDevice(void *, const void *, REFIID, void **);
#endif
#include <stdio.h>

static const IID retail_device = {0x5405c344,0xd457,0x444e,{0xb4,0xdd,0x23,0x66,0xe4,0x5a,0xee,0x39}};
static const GUID cookie_key = {0x73064dc1,0x3d80,0x4a20,{0xa8,0xaa,0x28,0x66,0x87,0x02,0x2e,0x5b}};
#define CHECK(expr) do { if (!(expr)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #expr); return 1; } } while (0)

#ifdef SCARLETT_DEVICE_PRIVATE_TEST
static int test_device_init(struct xbox_device *device)
{
    D3D12_HEAP_PROPERTIES props;
    D3D12_COMMAND_QUEUE_DESC desc = {D3D12_COMMAND_LIST_TYPE_DIRECT, 0, 0, 1};
    ID3D12CommandQueue *queue;
    ID3D12Fence *fence;
    HANDLE event;

    for (D3D12_HEAP_TYPE type = D3D12_HEAP_TYPE_DEFAULT; type <= D3D12_HEAP_TYPE_READBACK; ++type) {
        CHECK(device_heap_properties(&props, device, 1, type) == S_OK);
        CHECK(props.Type == D3D12_HEAP_TYPE_CUSTOM);
        CHECK(props.CPUPageProperty != D3D12_CPU_PAGE_PROPERTY_UNKNOWN);
        CHECK(props.MemoryPoolPreference != D3D12_MEMORY_POOL_UNKNOWN);
        CHECK(props.CreationNodeMask == 1 && props.VisibleNodeMask == 1);
    }
    CHECK(device_heap_properties(&props, device, 1, D3D12_HEAP_TYPE_CUSTOM) == E_INVALIDARG);
    CHECK(!props.Type && !props.CreationNodeMask);
    CHECK(device_heap_properties(&props, device, 2, D3D12_HEAP_TYPE_DEFAULT) == E_INVALIDARG);
    CHECK(device_direct_queue(device, &desc, &queue) == S_OK);
    CHECK(ID3D12Device_CreateFence(device->host, 0, D3D12_FENCE_FLAG_SHARED,
            &IID_ID3D12Fence, (void **)&fence) == S_OK);
    event = CreateEventW(NULL, FALSE, FALSE, NULL);
    CHECK(event);
    CHECK(ID3D12Fence_SetEventOnCompletion(fence, 1, event) == S_OK);
    CHECK(ID3D12CommandQueue_Signal(queue, fence, 1) == S_OK);
    CHECK(WaitForSingleObject(event, 5000) == WAIT_OBJECT_0 && ID3D12Fence_GetCompletedValue(fence) == 1);
    CloseHandle(event);
    ID3D12Fence_Release(fence);
    ID3D12CommandQueue_Release(queue);
    desc.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
    CHECK(device_direct_queue(device, &desc, &queue) == E_INVALIDARG && !queue);
    puts("PASS private device init: heap properties, Direct queue and shared-fence completion");
    return 0;
}
#endif

int main(void)
{
    DWORD params[20] = {0x20004}; /* Opaque input; identity does not depend on the GDK layout. */
    IUnknown *identity, *again;
    ID3D12Device *desktop;
    UINT cookie = 0xbaf01234, value = 0, size = sizeof(value);
    void *out = &cookie;

    CHECK(D3D12XboxCreateDevice(NULL, params, &retail_device, &out) == E_NOINTERFACE && !out);
    out = &cookie;
    CHECK(D3D12XboxCreateDevice(NULL, params, &IID_ID3D12Device, &out) == E_NOINTERFACE && !out);
    out = &cookie;
    CHECK(D3D12XboxCreateDevice(&cookie, params, &IID_IUnknown, &out) == E_INVALIDARG && !out);
    out = &cookie;
    CHECK(D3D12XboxCreateDevice(NULL, NULL, &IID_IUnknown, &out) == E_INVALIDARG && !out);
    out = &cookie;
    CHECK(D3D12XboxCreateDevice(NULL, params, NULL, &out) == E_INVALIDARG && !out);
    CHECK(D3D12XboxCreateDevice(NULL, params, &IID_IUnknown, NULL) == E_POINTER);

    CHECK(D3D12XboxCreateDevice(NULL, params, &IID_IUnknown, (void **)&identity) == S_OK);
    CHECK(IUnknown_QueryInterface(identity, &IID_IUnknown, (void **)&again) == S_OK && again == identity);
    IUnknown_Release(identity);
    IUnknown_AddRef(again);
    IUnknown_Release(again);
    out = &cookie;
    CHECK(IUnknown_QueryInterface(again, &retail_device, &out) == E_NOINTERFACE && !out);
    out = &cookie;
    CHECK(IUnknown_QueryInterface(again, &IID_ID3D12Device, &out) == E_NOINTERFACE && !out);
#ifdef SCARLETT_DEVICE_PRIVATE_TEST
    CHECK(!test_device_init(CONTAINING_RECORD(again, struct xbox_device, iface)));
#endif

    /* Only the Xbox wrapper owns the per-adapter device between desktop factory calls. */
    CHECK(D3D12CreateDevice(NULL, D3D_FEATURE_LEVEL_12_0, &IID_ID3D12Device, (void **)&desktop) == S_OK);
    CHECK(ID3D12Device_SetPrivateData(desktop, &cookie_key, sizeof(cookie), &cookie) == S_OK);
    ID3D12Device_Release(desktop);
    CHECK(D3D12CreateDevice(NULL, D3D_FEATURE_LEVEL_12_0, &IID_ID3D12Device, (void **)&desktop) == S_OK);
    CHECK(ID3D12Device_GetPrivateData(desktop, &cookie_key, &size, &value) == S_OK);
    CHECK(size == sizeof(cookie) && value == cookie);
    CHECK(IUnknown_Release(again) == 0);
    CHECK(ID3D12Device_Release(desktop) == 0);
    puts("PASS Xbox device: IUnknown identity, retail IID rejection, shared D3D12 device lifetime");
    return 0;
}
