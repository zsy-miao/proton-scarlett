#pragma once
#include <dxgi.h>
#ifdef SCARLETT_EXPERIMENTAL_TAGGED_BATCH
#include <d3d12.h>
#if !defined(SCARLETT_EXPERIMENTAL_DIRECT_WAIT) || !defined(SCARLETT_EXPERIMENTAL_PRIMARY_RELEASE)
#error "Tagged batch requires direct wait and primary release"
#endif
HRESULT xs_frame_submit(IUnknown *,UINT64,ID3D12CommandQueue *,BOOL,ID3D12Fence *,void (*)(void *),void *);
HRESULT xs_frame_submission_query(IUnknown *,UINT64,ID3D12CommandQueue *,BOOL);
#endif

/* Internal presentation consumer. Caller serializes presents to this chain, which
 * must belong to the wrapper's host device. The scheduler retains a reference. */
HRESULT xs_frame_present(IUnknown *device, UINT64 token, IDXGISwapChain *chain);
HRESULT xs_frame_present_after(IUnknown *device,UINT64 token,IDXGISwapChain *chain,
        HRESULT (*prepare)(void *),void *context);
