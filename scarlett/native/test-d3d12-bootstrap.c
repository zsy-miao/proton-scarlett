/* Independent x64 consumer: dispatch at the observed byte offsets, not a shared device header. */
#define COBJMACROS
#define INITGUID
#include <windows.h>
#include <d3d12.h>
#include <stdio.h>
#ifdef XS_TEST_LINKED
#include "queue/queue.h"
#else
__declspec(dllimport)
#endif
HRESULT WINAPI D3D12XboxCreateDevice(void *, const void *, REFIID, void **);

typedef D3D12_HEAP_PROPERTIES *(WINAPI *Heap)(D3D12_HEAP_PROPERTIES *, IUnknown *, UINT, D3D12_HEAP_TYPE);
typedef HRESULT (WINAPI *Queue)(IUnknown *, const void *, REFIID, void **);
typedef HRESULT (WINAPI *Fence)(IUnknown *, UINT64, D3D12_FENCE_FLAGS, REFIID, void **);
typedef HRESULT (WINAPI *Signal)(IUnknown *, IUnknown *, UINT64);
typedef HRESULT (WINAPI *Frequency)(IUnknown *, UINT64 *);
#define METHOD(obj, offset, type) ((type)((void *const *)(obj)->lpVtbl)[(offset)/8])
#define CHECK(e) do { if (!(e)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #e); return 1; } } while (0)
static const IID device_id = {0x5405c344,0xd457,0x444e,{0xb4,0xdd,0x23,0x66,0xe4,0x5a,0xee,0x39}};

int main(void)
{
    DWORD params[20] = { [0]=0x20004, [2]=0x100000, [10]=1, [15]=0x20 };
    UINT direct[4] = {0,0,0,1}, ext[4] = {2,0,1,0};
    IUnknown *d, *q, *f;
    ID3D12Device *host;
    void *p = params;
    UINT64 hz = 0;
    CHECK(D3D12CreateDevice(NULL, D3D_FEATURE_LEVEL_12_0, &IID_ID3D12Device, (void **)&host) == S_OK);
    params[1] = 1;
    CHECK(D3D12XboxCreateDevice(NULL, params, &device_id, &p) == E_NOTIMPL && !p);
    params[1] = 0;
    CHECK(D3D12XboxCreateDevice(NULL, params, &device_id, (void **)&d) == S_OK);
    CHECK(IUnknown_QueryInterface(d, &device_id, &p) == S_OK && p == d);
    IUnknown_Release((IUnknown *)p);
    CHECK(IUnknown_QueryInterface(d, &IID_ID3D12Device, &p) == E_NOINTERFACE && !p);
    for (int t = 1; t <= 3; ++t) {
        struct { DWORD lo; D3D12_HEAP_PROPERTIES props; DWORD hi; } result = { .lo=123, .hi=456 };
        D3D12_HEAP_PROPERTIES expected;
        host->lpVtbl->GetCustomHeapProperties(host, &expected, 1, t);
        CHECK(METHOD(d,0xd0,Heap)(&result.props, d, 1, t) == &result.props);
        CHECK(result.lo == 123 && result.hi == 456 && !memcmp(&expected, &result.props, sizeof(expected)));
    }
    CHECK(METHOD(d,0x40,Queue)(d, direct, &IID_ID3D12Fence, &p) == E_NOINTERFACE && !p);
    CHECK(METHOD(d,0x40,Queue)(d, direct, &IID_ID3D12CommandQueue, (void **)&q) == S_OK);
    CHECK(METHOD(d,0x120,Fence)(d, 0, D3D12_FENCE_FLAG_SHARED, &IID_ID3D12Fence, (void **)&f) == S_OK);
    CHECK(IUnknown_QueryInterface(q, &IID_ID3D12CommandQueue, &p) == S_OK && p == q);
    IUnknown_Release((IUnknown *)p);
    CHECK(IUnknown_QueryInterface(f, &IID_IUnknown, &p) == S_OK && p == f);
    IUnknown_Release((IUnknown *)p);
    CHECK(METHOD(q,0x90,Frequency)(q, &hz) == S_OK && hz);
    CHECK(METHOD(q,0x80,Signal)(q, f, 0x100000001ull) == S_OK);
#ifdef XS_TEST_LINKED
    HANDLE done = CreateEventW(NULL, FALSE, FALSE, NULL);
    CHECK(done && xs_fence_event(f, 0x100000001ull, done) == S_OK);
    CHECK(WaitForSingleObject(done, 5000) == WAIT_OBJECT_0);
    CloseHandle(done);
#endif
    p = q;
    CHECK(METHOD(d,0x180,Queue)(d, ext, &IID_ID3D12CommandQueue, &p) == E_NOTIMPL && !p);
    CHECK(!IUnknown_Release(f) && !IUnknown_Release(q) && !IUnknown_Release(d));
    ID3D12Device_Release(host);
    puts("PASS bootstrap: Xbox IID -> 3 host heaps -> Direct queue/shared fence -> extended queue E_NOTIMPL");
    return 0;
}
