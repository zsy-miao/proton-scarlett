#pragma once
#include <dxgi1_4.h>
#include <d3d12.h>

/* Private display-resource factory hooks. Open on the presenting queue; bind
 * each title interface to the actual buffer used for rendering at creation. */
HRESULT xs_console_direct_queue(ID3D12Device *host, IUnknown *owner, IUnknown **out);
HRESULT xs_display_open(IUnknown *queue, IDXGIFactory2 *factory, HWND window,
        UINT width, UINT height, IDXGISwapChain3 **out);
HRESULT xs_display_wrap(IUnknown *queue, UINT index, ID3D12Resource *render_resource, IUnknown **out);
ID3D12Resource *xs_surface_host(IUnknown *surface); /* borrowed, caller owns a valid wrapper */
ID3D12CommandQueue *xs_display_queue(IUnknown *queue); /* borrowed for the host renderer */
