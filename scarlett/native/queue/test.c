#define COBJMACROS
#define CONST_VTABLE
#define INITGUID
#include "queue.h"
#include <stdio.h>

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

int main(void)
{
    ID3D12Device *host;
    IUnknown *queue, *gate, *done;
    const struct xs_queue_prefix *v;
    const UINT64 value = (1ULL << 40) + 3;
    UINT64 hz;
    HANDLE completed = CreateEventW(NULL, FALSE, FALSE, NULL);
    CHECK(completed);
    CHECK(D3D12CreateDevice(NULL, D3D_FEATURE_LEVEL_12_0, &IID_ID3D12Device, (void **)&host) == S_OK);
    CHECK(xs_direct_queue(host, &queue) == S_OK);
    CHECK(xs_shared_fence(host, &gate) == S_OK);
    CHECK(xs_shared_fence(host, &done) == S_OK);
    CHECK(queue != gate && gate != done);
    CHECK(!identity(queue, &IID_ID3D12CommandQueue) && !identity(gate, &IID_ID3D12Fence));

    v = CONTAINING_RECORD(queue->lpVtbl, const struct xs_queue_prefix, unknown);
    CHECK(v->frequency(queue, &hz) == S_OK && hz);
    CHECK(v->signal(queue, queue, value) == E_INVALIDARG);
    CHECK(v->signal(queue, (IUnknown *)host, value) == E_INVALIDARG);
    CHECK(xs_fence_event(done, value, completed) == S_OK);
    CHECK(xs_cpu_signal(gate, 3) == S_OK);
    CHECK(xs_queue_wait(queue, gate, value) == S_OK);
    CHECK(v->signal(queue, done, value) == S_OK);
    CHECK(WaitForSingleObject(completed, 100) == WAIT_TIMEOUT);
    CHECK(xs_cpu_signal(gate, value) == S_OK);
    CHECK(WaitForSingleObject(completed, 5000) == WAIT_OBJECT_0);
    CHECK(IUnknown_Release(queue) == 0);
    CHECK(IUnknown_Release(gate) == 0 && IUnknown_Release(done) == 0);
    CHECK(ID3D12Device_Release(host) == 0);
    CloseHandle(completed);
    puts("PASS queue adapter: COM identity, timestamp frequency, 64-bit wait/signal, shared-fence event and release");
    return 0;
}
