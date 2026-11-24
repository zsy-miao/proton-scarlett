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
typedef HRESULT (WINAPI *Allocator)(IUnknown *, D3D12_COMMAND_LIST_TYPE, REFIID, void **);
typedef HRESULT (WINAPI *List)(IUnknown *, const void *, IUnknown *, IUnknown *, REFIID, void **);
typedef HRESULT (WINAPI *ResetAllocator)(IUnknown *);
#define METHOD(obj, offset, type) ((type)((void *const *)(obj)->lpVtbl)[(offset)/8])
#define CHECK(e) do { if (!(e)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #e); return 1; } } while (0)
static const IID device_id = {0x5405c344,0xd457,0x444e,{0xb4,0xdd,0x23,0x66,0xe4,0x5a,0xee,0x39}};
static const IID list_id = {0xc3827890,0x4024,0x474c,{0x98,0xcf,0x64,0x5b,0x51,0xdc,0x48,0x74}};

static int test_list(ID3D12Device *, IUnknown *, IUnknown *, IUnknown *, UINT);

static int test_indexed(ID3D12Device *host, IUnknown *d, IUnknown *direct, IUnknown *default_compute)
{
    const UINT desc[4][4] = {{2,0,1,0}, {2,0,2,0}, {2,0,3,0}, {2,0,0,1}};
    IUnknown *q[4], *f[4], *gate;
    Fence make_fence = METHOD(d,0x120,Fence);
    CHECK(make_fence(d, 0, D3D12_FENCE_FLAG_SHARED, &IID_ID3D12Fence, (void **)&gate) == S_OK);
    for (unsigned i = 0; i < 4; ++i) {
        IUnknown *identity;
        CHECK(METHOD(d,0x180,Queue)(d, desc[i], &IID_ID3D12CommandQueue, (void **)&q[i]) == S_OK);
        CHECK(IUnknown_QueryInterface(q[i], &IID_IUnknown, (void **)&identity) == S_OK && identity == q[i]);
        CHECK(identity != direct && identity != default_compute);
        for (unsigned j = 0; j < i; ++j) CHECK(identity != q[j]);
        IUnknown_Release(identity);
        CHECK(make_fence(d, 0, D3D12_FENCE_FLAG_SHARED, &IID_ID3D12Fence, (void **)&f[i]) == S_OK);
        CHECK(METHOD(q[i],0x80,Signal)(q[i], f[i], 1ull << 32) == S_OK);
        IUnknown *a, *l;
        const UINT list_desc[] = {2,2,1,0};
        CHECK(METHOD(d,0x48,Allocator)(d,2,&IID_ID3D12CommandAllocator,(void **)&a) == S_OK);
        CHECK(METHOD(d,0x248,List)(d,list_desc,a,NULL,&list_id,(void **)&l) == S_OK);
        CHECK(!test_list(host,q[i],a,l,2));
        CHECK(!IUnknown_Release(a));
    }
#ifdef XS_TEST_LINKED
    for (unsigned leader = 0; leader < 4; ++leader) {
        /* Constant low word: a truncated wait is satisfied before the GPU opens the gate. */
        const UINT64 prev = ((UINT64)leader + 1) << 40, next = prev + (1ull << 40);
        HANDLE blocked[3], free_queue = CreateEventW(NULL, FALSE, FALSE, NULL);
        unsigned n = 0;
        CHECK(free_queue && xs_cpu_signal(gate, prev) == S_OK);
        for (unsigned i = 0; i < 4; ++i) {
            if (i == leader) continue;
            blocked[n] = CreateEventW(NULL, FALSE, FALSE, NULL);
            CHECK(blocked[n] && xs_fence_event(f[i], next, blocked[n]) == S_OK);
            CHECK(xs_queue_wait(q[i], gate, next) == S_OK);
            CHECK(METHOD(q[i],0x80,Signal)(q[i], f[i], next) == S_OK);
            ++n;
        }
        CHECK(xs_fence_event(f[leader], next, free_queue) == S_OK);
        CHECK(METHOD(q[leader],0x80,Signal)(q[leader], f[leader], next) == S_OK);
        CHECK(WaitForSingleObject(free_queue, 5000) == WAIT_OBJECT_0);
        CHECK(WaitForMultipleObjects(3, blocked, FALSE, 50) == WAIT_TIMEOUT);
        CHECK(METHOD(q[leader],0x80,Signal)(q[leader], gate, next) == S_OK);
        CHECK(WaitForMultipleObjects(3, blocked, TRUE, 5000) == WAIT_OBJECT_0);
        CloseHandle(free_queue);
        for (unsigned j = 0; j < 3; ++j) CloseHandle(blocked[j]);
    }
    puts("PASS indexed progress: each of four Compute queues advanced with the other three gated; full-width ordered GPU signals");
#endif
    for (unsigned i = 0; i < 4; ++i) CHECK(!IUnknown_Release(q[i]) && !IUnknown_Release(f[i]));
    CHECK(!IUnknown_Release(gate));
    return 0;
}
typedef void (WINAPI *Sync)(IUnknown *, UINT, UINT, UINT64, UINT64);
typedef HRESULT (WINAPI *ResetList)(IUnknown *, IUnknown *, IUnknown *, UINT);
typedef void (WINAPI *Submit)(IUnknown *, UINT, IUnknown *const *, UINT64, UINT);

#ifdef XS_TEST_LINKED
static HRESULT buffer(ID3D12Device *d, D3D12_HEAP_TYPE type, ID3D12Resource **out)
{
    D3D12_HEAP_PROPERTIES h = {.Type=type,.CreationNodeMask=1,.VisibleNodeMask=1};
    D3D12_RESOURCE_DESC b = {.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER,.Width=4096,.Height=1,
        .DepthOrArraySize=1,.MipLevels=1,.SampleDesc={1,0},.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR};
    D3D12_RESOURCE_STATES state = type == D3D12_HEAP_TYPE_UPLOAD ? D3D12_RESOURCE_STATE_GENERIC_READ :
            type == D3D12_HEAP_TYPE_READBACK ? D3D12_RESOURCE_STATE_COPY_DEST : D3D12_RESOURCE_STATE_COMMON;
    return ID3D12Device_CreateCommittedResource(d, &h, 0, &b, state, NULL, &IID_ID3D12Resource, (void **)out);
}
#endif

static int test_list(ID3D12Device *host, IUnknown *q, IUnknown *a, IUnknown *l, UINT type)
{
    void *id;
    CHECK(IUnknown_QueryInterface(l, &list_id, &id) == S_OK && id == l); IUnknown_Release(l);
    CHECK(IUnknown_QueryInterface(l, &IID_IUnknown, &id) == S_OK && id == l); IUnknown_Release(l);
    CHECK(IUnknown_QueryInterface(l, &IID_ID3D12GraphicsCommandList7, &id) == E_NOINTERFACE && !id);
    CHECK(FAILED(METHOD(l,0x458,ResetList)(l,a,NULL,0))); /* still recording */
#ifdef XS_TEST_LINKED
    ID3D12GraphicsCommandList7 *rec = xs_list_host(l);
    ID3D12Resource *up, *tmp, *back;
    IUnknown *gate, *done;
    HANDLE event = CreateEventW(NULL,FALSE,FALSE,NULL);
    CHECK(event && rec && !xs_list_host(a));
    CHECK(xs_shared_fence(host,&gate) == S_OK && xs_shared_fence(host,&done) == S_OK);
    CHECK(buffer(host,D3D12_HEAP_TYPE_UPLOAD,&up) == S_OK);
    CHECK(buffer(host,D3D12_HEAP_TYPE_DEFAULT,&tmp) == S_OK);
    CHECK(buffer(host,D3D12_HEAP_TYPE_READBACK,&back) == S_OK);
    UINT *map;
    CHECK(ID3D12Resource_Map(up,0,NULL,(void **)&map) == S_OK);
    for (UINT i=0;i<1024;++i) map[i] = (i * 7919) ^ (0x305234 + type);
    ID3D12Resource_Unmap(up,0,NULL);
    ID3D12GraphicsCommandList7_CopyBufferRegion(rec,tmp,0,up,0,4096);
#else
    (void)host;
    (void)q;
#endif
    METHOD(l,0x420,Sync)(l,type ? 0x100000 : 0x700000,0,0,0xffffffffff00ull);
    METHOD(l,0x418,Sync)(l,type ? 0xc380 : 0xc3c0,type ? 0x20000 : 0x80020000,0,0xffffffffff00ull);
#ifdef XS_TEST_LINKED
    ID3D12GraphicsCommandList7_CopyBufferRegion(rec,back,0,tmp,0,4096);
#endif
    CHECK(METHOD(l,0x48,ResetAllocator)(l) == S_OK);
    CHECK(FAILED(METHOD(l,0x48,ResetAllocator)(l)));
#ifdef XS_TEST_LINKED
    CHECK(xs_queue_wait(q,gate,1) == S_OK);
    METHOD(q,0x118,Submit)(q,1,&l,0,0);
    CHECK(METHOD(q,0x80,Signal)(q,done,1) == S_OK);
    CHECK(xs_fence_event(done,1,event) == S_OK && WaitForSingleObject(event,50) == WAIT_TIMEOUT);
    CHECK(xs_cpu_signal(gate,1) == S_OK && WaitForSingleObject(event,5000) == WAIT_OBJECT_0);
    CHECK(ID3D12Resource_Map(back,0,NULL,(void **)&map) == S_OK);
    for (UINT i=0;i<1024;++i) CHECK(map[i] == ((i*7919) ^ (0x305234+type)));
    ID3D12Resource_Unmap(back,0,NULL);
    ID3D12Resource_Release(up); ID3D12Resource_Release(tmp); ID3D12Resource_Release(back);
    IUnknown_Release(gate); IUnknown_Release(done); CloseHandle(event);
#endif
    CHECK(METHOD(a,0x40,ResetAllocator)(a) == S_OK);
    CHECK(METHOD(l,0x458,ResetList)(l,a,NULL,1) == E_NOTIMPL);
    CHECK(METHOD(l,0x458,ResetList)(l,a,NULL,0) == S_OK);
    METHOD(l,0x420,Sync)(l,0,0,0,0);
    CHECK(METHOD(l,0x48,ResetAllocator)(l) == E_NOTIMPL);
    CHECK(!IUnknown_Release(l));
    return 0;
}

int main(void)
{
    DWORD params[20] = { [0]=0x20004, [2]=0x100000, [10]=1, [15]=0x20 };
    UINT direct[4] = {0,0,0,1}, compute[4] = {2,0,0,0};
    const UINT unsupported[][4] = {{2,0,4,0}, {2,0,1,1}, {2,0,0,2}, {2,0,~0u,0},
                                   {2,1,0,0}, {3,0,0,0}};
    IUnknown *d, *q, *f, *cq;
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
    Queue extended = METHOD(d,0x180,Queue);
    CHECK(extended(d, compute, &IID_ID3D12CommandQueue, (void **)&cq) == S_OK && cq != q);
    CHECK(METHOD(cq,0x80,Signal)(cq, f, 0x100000002ull) == S_OK);
    CHECK(!test_indexed(host, d, q, cq));
#ifdef XS_TEST_LINKED
    /* Queue both sides of a dependency before opening the CPU gate. */
    const UINT64 n = 1ull << 42;
    IUnknown *gate, *ready, *complete;
    Fence shared = METHOD(d,0x120,Fence);
    CHECK(shared(d, 0, D3D12_FENCE_FLAG_SHARED, &IID_ID3D12Fence, (void **)&gate) == S_OK);
    CHECK(shared(d, 0, D3D12_FENCE_FLAG_SHARED, &IID_ID3D12Fence, (void **)&ready) == S_OK);
    CHECK(shared(d, 0, D3D12_FENCE_FLAG_SHARED, &IID_ID3D12Fence, (void **)&complete) == S_OK);
    done = CreateEventW(NULL, FALSE, FALSE, NULL);
    CHECK(done && xs_fence_event(complete, n, done) == S_OK);
    CHECK(xs_queue_wait(cq, ready, n) == S_OK);
    CHECK(METHOD(cq,0x80,Signal)(cq, complete, n) == S_OK);
    CHECK(xs_queue_wait(q, gate, n) == S_OK);
    CHECK(METHOD(q,0x80,Signal)(q, ready, n) == S_OK);
    CHECK(WaitForSingleObject(done, 100) == WAIT_TIMEOUT);
    CHECK(xs_cpu_signal(gate, n) == S_OK);
    CHECK(WaitForSingleObject(done, 5000) == WAIT_OBJECT_0);
    CloseHandle(done);
    CHECK(!IUnknown_Release(gate) && !IUnknown_Release(ready) && !IUnknown_Release(complete));
#endif
    for (unsigned i = 0; i < ARRAYSIZE(unsupported); ++i) {
        p = q;
        CHECK(extended(d, unsupported[i], &IID_ID3D12CommandQueue, &p) == E_NOTIMPL && !p);
    }
    p = q;
    CHECK(extended(d, compute, &IID_ID3D12Fence, &p) == E_NOINTERFACE && !p);
    p = q;
    CHECK(extended(d, NULL, &IID_ID3D12CommandQueue, &p) == E_INVALIDARG && !p);
    CHECK(extended(d, compute, &IID_ID3D12CommandQueue, NULL) == E_POINTER);
    Allocator new_allocator = METHOD(d,0x48,Allocator);
    List new_list = METHOD(d,0x248,List);
    CHECK(new_allocator(d, D3D12_COMMAND_LIST_TYPE_COPY, &IID_ID3D12CommandAllocator, &p) == E_NOTIMPL && !p);
    CHECK(new_allocator(d, 0, &IID_ID3D12Fence, &p) == E_NOINTERFACE && !p);
    for (int type = 0; type <= 2; type += 2) {
        IUnknown *a;
        UINT desc[] = {type, 2, 1, 0};
        CHECK(new_allocator(d, type, &IID_ID3D12CommandAllocator, (void **)&a) == S_OK && a);
        CHECK(IUnknown_QueryInterface(a, &IID_ID3D12CommandAllocator, &p) == S_OK && p == a);
        IUnknown_Release((IUnknown *)p);
        CHECK(IUnknown_QueryInterface(a, &IID_IUnknown, &p) == S_OK && p == a);
        IUnknown_Release((IUnknown *)p);
        CHECK(IUnknown_QueryInterface(a, &IID_ID3D12Device, &p) == E_NOINTERFACE && !p);
        p = a;
        CHECK(new_list(d, desc, a, NULL, &list_id, &p) == S_OK && p);
        CHECK(!test_list(host, type ? cq : q, a, p, type));
        desc[1] = 6;
        CHECK(new_list(d, desc, a, NULL, &list_id, &p) == E_NOTIMPL && !p);
        desc[1] = 2;
        CHECK(new_list(d, desc, q, NULL, &list_id, &p) == E_INVALIDARG && !p);
        CHECK(new_list(d, desc, a, NULL, &IID_ID3D12GraphicsCommandList, &p) == E_NOINTERFACE && !p);
        desc[0] ^= 2;
        CHECK(new_list(d, desc, a, NULL, &list_id, &p) == E_INVALIDARG && !p);
        CHECK(!IUnknown_Release(a));
    }
    puts("PASS startup lists: Direct and Compute, exact masks, Close/Reset, rejected unsupported operation");
    CHECK(!IUnknown_Release(cq));
    CHECK(!IUnknown_Release(f) && !IUnknown_Release(q) && !IUnknown_Release(d));
    ID3D12Device_Release(host);
    puts("PASS bootstrap: Direct, default Compute and four distinct indexed queues; unsupported selectors rejected");
    return 0;
}
