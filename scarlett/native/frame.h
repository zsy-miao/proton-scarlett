#pragma once
#include <dxgi.h>
#if defined(SCARLETT_EXPERIMENTAL_BRIDGE) && !defined(SCARLETT_EXPERIMENTAL_TAGGED_BATCH)
#error "Private bridge requires tagged batch"
#endif
#ifdef SCARLETT_EXPERIMENTAL_TAGGED_BATCH
#include <d3d12.h>
#if !defined(SCARLETT_EXPERIMENTAL_DIRECT_WAIT) || !defined(SCARLETT_EXPERIMENTAL_PRIMARY_RELEASE)
#error "Tagged batch requires direct wait and primary release"
#endif
HRESULT xs_frame_submit(IUnknown *,UINT64,ID3D12CommandQueue *,BOOL,ID3D12Fence *,HRESULT (*)(void *),void *);
HRESULT xs_frame_submission_query(IUnknown *,UINT64,ID3D12CommandQueue *,BOOL);
#ifdef SCARLETT_EXPERIMENTAL_BRIDGE
HRESULT xs_frame_present_release(IUnknown *,UINT64,IDXGISwapChain *,HRESULT (*)(void *),void *,ID3D12CommandQueue *,ID3D12Fence *);
#endif
#endif

/* Internal presentation consumer. Caller serializes presents to this chain, which
 * must belong to the wrapper's host device. The scheduler retains a reference. */
HRESULT xs_frame_present(IUnknown *device, UINT64 token, IDXGISwapChain *chain);
HRESULT xs_frame_present_after(IUnknown *device,UINT64 token,IDXGISwapChain *chain,
        HRESULT (*prepare)(void *),void *context);
