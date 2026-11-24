#pragma once
#include <dxgi1_4.h>
#include <d3d12.h>

/* Private display-resource factory hooks. Open on the presenting queue; bind
 * each title interface to the actual buffer used for rendering at creation. */
HRESULT xs_console_direct_queue(ID3D12Device *host, IUnknown *owner, IUnknown **out);
HRESULT xs_display_open(IUnknown *queue, IDXGIFactory2 *factory, HWND window,
        UINT width, UINT height, IDXGISwapChain3 **out);
/* Private native-x64 provider: retrieve the uniquely captured title window on
 * its creating thread. Revalidate the same lifetime after DXGI returns. */
HRESULT xs_display_open_title(IUnknown *queue,IDXGIFactory2 *factory,UINT width,UINT height,IDXGISwapChain3 **out);
HRESULT xs_display_wrap(IUnknown *queue, UINT index, ID3D12Resource *render_resource, IUnknown **out);
ID3D12Resource *xs_surface_host(IUnknown *surface); /* borrowed, caller owns a valid wrapper */
ID3D12CommandQueue *xs_display_queue(IUnknown *queue); /* borrowed for the host renderer */
/* Private renderer handoff. Supply its factory-validated primary identity, actual
 * title HWND, and state known after the last submit on this exact host queue.
 * Do not reuse the source until +100 returns; each successful frame needs a new handoff. */
HRESULT xs_display_arm_primary(IUnknown *queue,ID3D12CommandQueue *producer,HWND window,
        UINT64 token,IUnknown *key,ID3D12Resource *source,D3D12_RESOURCE_STATES state,
        UINT guest_flags,UINT guest_heap_flags);
