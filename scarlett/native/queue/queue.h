#ifndef SCARLETT_QUEUE_H
#define SCARLETT_QUEUE_H
#include <windows.h>
#include <d3d12.h>

/* Internal constructors: each result owns one reference and advertises only IUnknown. */
HRESULT xs_direct_queue(ID3D12Device *host, IUnknown **out);
HRESULT xs_compute_queue(ID3D12Device *host, IUnknown **out);
HRESULT xs_shared_fence(ID3D12Device *host, IUnknown **out);
#ifdef SCARLETT_DEVICE_BOOTSTRAP
HRESULT xs_allocator(ID3D12Device *host, D3D12_COMMAND_LIST_TYPE type, IUnknown **out);
/* Borrowed; for host list constructors only. Caller holds the Xbox reference. */
ID3D12CommandAllocator *xs_allocator_host(IUnknown *allocator, D3D12_COMMAND_LIST_TYPE type);
#endif

/* Verified x64 call shapes only; reserved entries must never be dispatched. */
struct xs_queue_prefix {
    IUnknownVtbl unknown;
    void *reserved18[13];
    HRESULT (WINAPI *signal)(IUnknown *queue, IUnknown *fence, UINT64 value);
    void *reserved88;
    HRESULT (WINAPI *frequency)(IUnknown *queue, UINT64 *frequency);
};

/* Typed operations with no advertised Xbox vtable slot. */
HRESULT xs_queue_wait(IUnknown *queue, IUnknown *fence, UINT64 value);
HRESULT xs_cpu_signal(IUnknown *fence, UINT64 value);
HRESULT xs_fence_event(IUnknown *fence, UINT64 value, HANDLE event);
#endif
