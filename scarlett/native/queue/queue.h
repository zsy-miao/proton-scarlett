#ifndef SCARLETT_QUEUE_H
#define SCARLETT_QUEUE_H
#include <windows.h>
#include <d3d12.h>

/* Internal constructors: each result owns one reference and advertises only IUnknown. */
HRESULT xs_direct_queue(ID3D12Device *host, IUnknown **out);
HRESULT xs_compute_queue(ID3D12Device *host, IUnknown **out);
HRESULT xs_shared_fence(ID3D12Device *host, IUnknown **out);
#ifdef SCARLETT_DEVICE_BOOTSTRAP
HRESULT xs_copy_queue(ID3D12Device *host, IUnknown **out);
#define XS_INDIRECT_TYPE_69 ((D3D12_INDIRECT_ARGUMENT_TYPE)0x69)
HRESULT xs_query_heap(ID3D12Device *host, const D3D12_QUERY_HEAP_DESC *desc, void **out);
HRESULT xs_signature(ID3D12Device *host, D3D12_INDIRECT_ARGUMENT_TYPE type, UINT stride, IUnknown **out);
/* Borrowed for the specified original type; caller holds the wrapper reference. */
ID3D12CommandSignature *xs_signature_host(IUnknown *iface, D3D12_INDIRECT_ARGUMENT_TYPE original_type);
HRESULT xs_allocator(ID3D12Device *host, D3D12_COMMAND_LIST_TYPE type, IUnknown **out);
/* Borrowed; for host list constructors only. Caller holds the Xbox reference. */
ID3D12CommandAllocator *xs_allocator_host(IUnknown *allocator, D3D12_COMMAND_LIST_TYPE type);
extern const IID xs_sync_list_id, xs_pool_list_id;
HRESULT xs_startup_list(ID3D12Device *device, D3D12_COMMAND_LIST_TYPE type, IUnknown *allocator, BOOL pooled, IUnknown **out);
HRESULT xs_device_list(ID3D12Device *host,D3D12_COMMAND_LIST_TYPE type,IUnknown *allocator,BOOL pooled,IUnknown *pool_owner,IUnknown **out);
/* On success identity is retained; returned host and optional root are borrowed from it. */
ID3D12PipelineState *xs_pin_clear4(IUnknown *owner,ID3D12Device *host,IUnknown *identity,IUnknown **root);
ID3D12RootSignature *xs_retain_colour_root(IUnknown *,IUnknown *);
ID3D12PipelineState *xs_pin_colour_pso(IUnknown *,ID3D12Device *,IUnknown *,IUnknown *);
#ifdef XS_IM96_FACTORY
ID3D12PipelineState *xs_pin_im96_pso(IUnknown *,ID3D12Device *,IUnknown *,IUnknown *,ID3D12RootSignature **);
#endif
ID3D12PipelineState *xs_clear4_effective(IUnknown *pinned,UINT64 lanes);
ID3D12RootSignature *xs_retain_graphics_root(IUnknown *owner,IUnknown *identity);
HRESULT xs_initial_heap_banks(IUnknown *owner,IUnknown *const pair[2],ID3D12DescriptorHeap *banks[2]);
struct xs_attachment_pin {IUnknown *identity; ID3D12Resource *host; SIZE_T slot; UINT64 serial;};
/* The wrapper pins both its XMem module and the original generation. */
#ifdef SCARLETT_PAIR_SUBMIT
#include "lease-range.h"
#endif
struct xs_clear_source {IUnknown *placed;ID3D12Resource *buffer;
#ifdef SCARLETT_PAIR_SUBMIT
    struct lease_span span;
    const void *capture;
    BOOL needs_publication;
#endif
};
HRESULT xs_active_table_slot(IUnknown*,IUnknown*,UINT64,UINT,UINT*);
HRESULT xs_copy_colour_vb(IUnknown*,ID3D12Device*,ID3D12GraphicsCommandList7*,UINT64,BOOL,IUnknown*,struct xs_clear_source*);
HRESULT xs_copy_clear_source(IUnknown *,ID3D12Device *,ID3D12GraphicsCommandList7 *,UINT64,struct xs_clear_source *);
#ifdef SCARLETT_PAIR_SUBMIT
HRESULT xs_copy_restored_clear(IUnknown *,ID3D12Device *,ID3D12GraphicsCommandList7 *,UINT64,struct xs_clear_source *);
HRESULT xs_copy_restored_first_vb(IUnknown*,ID3D12Device*,ID3D12GraphicsCommandList7*,UINT64,const struct xs_clear_source*,struct xs_clear_source*);
#endif
struct om_views;
HRESULT xs_freeze_om(IUnknown *,const struct xs_attachment_pin [2],const SIZE_T [2],struct om_views *);
HRESULT xs_pin_attachment_pair(IUnknown *,ID3D12Device *,const D3D12_TEXTURE_BARRIER *,struct xs_attachment_pin [2]);
ID3D12Resource *xs_retain_primary(IUnknown *owner,const void *identity);
ID3D12Resource *xs_acquire_primary(IUnknown *,const void *,IUnknown **);
HRESULT xs_freeze_primary_output(IUnknown *,IUnknown *,ID3D12Resource *,SIZE_T,UINT64,
        ID3D12DescriptorHeap **,D3D12_CPU_DESCRIPTOR_HANDLE *,UINT64 *);
BOOL xs_live_output(IUnknown *,IUnknown *,ID3D12Resource *,SIZE_T,UINT64);
ID3D12CommandQueue *xs_release_clock(IUnknown *,UINT64 *,UINT *);
#ifdef SCARLETT_EXPERIMENTAL_BRIDGE
#if defined(TEST_SAME_BATCH_RELEASE) || defined(SCARLETT_PAIR_SUBMIT)
HRESULT xs_pair_writer(IUnknown *,IUnknown *,ID3D12Resource *,SIZE_T,UINT64,ID3D12CommandQueue *,UINT64 *,BOOL);
#endif
HRESULT xs_primary_writer(IUnknown *,ID3D12Resource *,ID3D12CommandQueue *);
IUnknown *xs_claim_view(IUnknown *,IUnknown *,ID3D12Resource *,ID3D12CommandQueue *,SIZE_T,UINT64,BOOL);
void xs_finish_flip(IUnknown *);
#ifdef SCARLETT_PAIR_SUBMIT
void xs_returned_common(IUnknown *,SIZE_T,UINT64);
HRESULT xs_reuse_common(IUnknown *,IUnknown *,ID3D12Resource *,ID3D12CommandQueue *,UINT64 *);
struct xs_sr_ticket {struct xs_attachment_pin rt,ds;ID3D12Fence *end;};
BOOL xs_sr_submitted(IUnknown *,const struct xs_attachment_pin *,const struct xs_attachment_pin *,ID3D12CommandQueue *,ID3D12Fence *);
HRESULT xs_sr_snapshot(IUnknown *,const void *,struct xs_sr_ticket *);
HRESULT xs_sr_view(IUnknown *,const struct xs_sr_ticket *,ID3D12DescriptorHeap **,D3D12_CPU_DESCRIPTOR_HANDLE *);
HRESULT xs_reuse_with_sr(IUnknown *,IUnknown *,ID3D12Resource *,ID3D12CommandQueue *,UINT64 *,const struct xs_sr_ticket *);
#endif
#endif
HRESULT xs_clear_primary_rtv(IUnknown *owner,ID3D12GraphicsCommandList7 *list,SIZE_T slot,ID3D12Resource *primary,UINT64 *serial);
/* Borrowed recorder, for internal typed command translation only. */
ID3D12GraphicsCommandList7 *xs_list_host(IUnknown *list);
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
