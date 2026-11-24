#define COBJMACROS
#define CONST_VTABLE
#define INITGUID
#include "queue.h"
#include <stdio.h>
#include <stdint.h>
#ifdef XS_TEST_MWAITX
#include <x86intrin.h>
#endif

#define CHECK(expr) do { if (!(expr)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #expr); return 1; } } while (0)

static int identity(IUnknown *obj, REFIID refused)
{
    IUnknown *alias = NULL;
    void *out = obj;
    CHECK(IUnknown_QueryInterface(obj, &IID_IUnknown, (void **)&alias) == S_OK && alias == obj);
    CHECK(IUnknown_QueryInterface(alias, refused, &out) == E_NOINTERFACE && !out);
    CHECK(IUnknown_Release(alias) == 1);
    return 0;
}

static const volatile UINT64 *address(IUnknown *f)
{
    typedef const volatile UINT64 *(WINAPI *Address)(IUnknown *);
    return ((Address)((void *const *)f->lpVtbl)[0x60 / 8])(f);
}

static int poll_value(const volatile UINT64 *word, UINT64 wanted)
{
    ULONGLONG start = GetTickCount64();
    while (*word != wanted) {
        if (GetTickCount64() - start >= 5000) return 0;
#ifdef XS_TEST_MWAITX
        _mm_monitorx((const void *)word, 0, 0);
        if (*word != wanted) _mm_mwaitx(2, 0, 100000);
#else
        Sleep(1);
#endif
    }
    return 1;
}

/* Exercise the removed-reason branch without resetting the physical GPU.
 * Only fake the device result: creation and completed-value reads use a real shared fence. */
static ID3D12Device *real_device;
static LONG lost;
static HRESULT WINAPI forward_fence(ID3D12Device *unused, UINT64 initial, D3D12_FENCE_FLAGS flags,
        REFIID iid, void **out)
{
    (void)unused;
    return ID3D12Device_CreateFence(real_device, initial, flags, iid, out);
}
static HRESULT WINAPI removed_reason(ID3D12Device *unused)
{
    (void)unused;
    return InterlockedCompareExchange(&lost, 0, 0) ? DXGI_ERROR_DEVICE_REMOVED : S_OK;
}
static int test_removed(ID3D12Device *host)
{
    static const ID3D12DeviceVtbl vt = {.CreateFence=forward_fence, .GetDeviceRemovedReason=removed_reason};
    ID3D12Device proxy = {&vt};
    IUnknown *f;
    real_device = host;
    CHECK(xs_shared_fence(&proxy, &f) == S_OK);
    const volatile UINT64 *word = address(f);
    CHECK(*word == 0);
    InterlockedExchange(&lost, 1);
    CHECK(poll_value(word, UINT64_MAX));
    CHECK(!IUnknown_Release(f));
    return 0;
}

int main(void)
{
    ID3D12Device *host;
    IUnknown *queue, *gate, *done, *pending;
    const struct xs_queue_prefix *v;
    const UINT64 value = (1ULL << 63) + 3, next = value + (1ULL << 32);
    UINT64 hz;
    HANDLE completed = CreateEventW(NULL, FALSE, FALSE, NULL);
    CHECK(completed);
    CHECK(D3D12CreateDevice(NULL, D3D_FEATURE_LEVEL_12_0, &IID_ID3D12Device, (void **)&host) == S_OK);
    CHECK(xs_direct_queue(host, &queue) == S_OK);
    CHECK(xs_shared_fence(host, &gate) == S_OK);
    CHECK(xs_shared_fence(host, &done) == S_OK);
    CHECK(queue != gate && gate != done);
    CHECK(!identity(queue, &IID_ID3D12CommandQueue) && !identity(gate, &IID_ID3D12Fence));
    const volatile UINT64 *gw = address(gate), *dw = address(done);
    CHECK(gw && dw && gw != dw && !((UINT_PTR)dw & 7) && !((UINT_PTR)gw & 7));
    CHECK(address(done) == dw && *dw == 0 && *gw == 0);

    v = CONTAINING_RECORD(queue->lpVtbl, const struct xs_queue_prefix, unknown);
    CHECK(v->frequency(queue, &hz) == S_OK && hz);
    CHECK(v->signal(queue, queue, value) == E_INVALIDARG);
    CHECK(v->signal(queue, (IUnknown *)host, value) == E_INVALIDARG);
    CHECK(xs_fence_event(done, value, completed) == S_OK);
    CHECK(xs_cpu_signal(gate, 3) == S_OK);
    CHECK(*gw == 3);
    CHECK(xs_queue_wait(queue, gate, value) == S_OK);
    CHECK(xs_shared_fence(host, &pending) == S_OK && *address(pending) == 0);
    CHECK(v->signal(queue, pending, value) == S_OK && !IUnknown_Release(pending));
    CHECK(v->signal(queue, done, value) == S_OK);
    CHECK(WaitForSingleObject(completed, 100) == WAIT_TIMEOUT && *dw == 0);
#ifdef XS_TEST_MWAITX
    _mm_monitorx((const void *)dw, 0, 0);
    if (*dw != value) _mm_mwaitx(2, 0, 100000); /* Timed wait like the title; gate still closed. */
    CHECK(*dw == 0);
#endif
    CHECK(xs_cpu_signal(gate, value) == S_OK);
    CHECK(poll_value(dw, value)); /* No COM call to done after opening the gate. */
    CHECK(WaitForSingleObject(completed, 5000) == WAIT_OBJECT_0);
    CHECK(xs_queue_wait(queue, gate, next) == S_OK && v->signal(queue, done, next) == S_OK);
    Sleep(50);
    CHECK(*dw == value && *gw == value);
    CHECK(xs_cpu_signal(gate, next) == S_OK && poll_value(dw, next));
    CHECK(address(done) == dw && *gw == next);
    CHECK(IUnknown_Release(queue) == 0);
    CHECK(IUnknown_Release(gate) == 0 && IUnknown_Release(done) == 0);
    CHECK(!test_removed(host));
    CHECK(ID3D12Device_Release(host) == 0);
    CloseHandle(completed);
    puts("PASS queue adapter: monitored CPU word, gated 64-bit GPU signals, pending release, injected removal");
    return 0;
}
