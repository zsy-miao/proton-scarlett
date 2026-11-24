#pragma once
#include <dxgi.h>

/* Internal presentation consumer. Caller serializes presents to this chain, which
 * must belong to the wrapper's host device. The scheduler retains a reference. */
HRESULT xs_frame_present(IUnknown *device, UINT64 token, IDXGISwapChain *chain);
HRESULT xs_frame_present_after(IUnknown *device,UINT64 token,IDXGISwapChain *chain,
        HRESULT (*prepare)(void *),void *context);
