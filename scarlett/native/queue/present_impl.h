/* Wire fields accessed separately: never compare compiler padding or call into
 * an unrecognized title resource. Only a creation-time association is trusted. */
struct xs_plane {
    UINT64 token;
    UINT count;
    IUnknown *const *resources;
    const RECT *src;
    const float *dst;
    UINT color, filter, ext_count;
    const void *extensions;
    UINT flags;
};
struct xs_present_params { float threshold; UINT views, ext_count; const void *extensions; UINT flags; };
_Static_assert(sizeof(struct xs_plane)==0x48 && offsetof(struct xs_plane,resources)==0x10 &&
        offsetof(struct xs_plane,color)==0x28 && offsetof(struct xs_plane,flags)==0x40 &&
        sizeof(struct xs_present_params)==0x20 && offsetof(struct xs_present_params,flags)==0x18,"present ABI");

ID3D12CommandQueue *xs_display_queue(IUnknown *queue) { return as_queue(queue); }

HRESULT xs_console_direct_queue(ID3D12Device *host, IUnknown *owner, IUnknown **out)
{
    if (!out) return E_POINTER;
    *out=NULL;
    if (!owner) return E_INVALIDARG;
    HRESULT hr=xs_direct_queue(host,out);
    if (SUCCEEDED(hr)) {
        IUnknown_AddRef(owner);
        CONTAINING_RECORD(*out,struct adapter,iface)->display.owner=owner;
    }
    return hr;
}

HRESULT xs_display_open(IUnknown *queue, IDXGIFactory2 *factory, HWND window,
        UINT width, UINT height, IDXGISwapChain3 **out)
{
    if (!out) return E_POINTER;
    *out=NULL;
    ID3D12CommandQueue *q=as_queue(queue);
    if (!q || !factory || !window || !width || !height) return E_INVALIDARG;
    DWORD pid=0;
    if (!GetWindowThreadProcessId(window,&pid) || pid!=GetCurrentProcessId() ||
        GetAncestor(window,GA_ROOT)!=window) return E_INVALIDARG;
    D3D12_COMMAND_QUEUE_DESC qd;
    q->lpVtbl->GetDesc(q,&qd);
    if (qd.Type!=D3D12_COMMAND_LIST_TYPE_DIRECT) return E_NOTIMPL;
    struct adapter *a=CONTAINING_RECORD(queue,struct adapter,iface);
    AcquireSRWLockExclusive(&a->display.lock);
    HRESULT hr=DXGI_ERROR_INVALID_CALL;
    if (a->display.owner && !a->display.chain && !GetPropW(window,L"Scarlett.Present.Owner")) {
        DXGI_SWAP_CHAIN_DESC1 sd={.Width=width,.Height=height,.Format=DXGI_FORMAT_R8G8B8A8_UNORM,
            .SampleDesc={1,0},.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT,.BufferCount=2,
            .SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD};
        IDXGISwapChain1 *chain;
        hr=IDXGIFactory2_CreateSwapChainForHwnd(factory,(IUnknown *)q,window,&sd,NULL,NULL,&chain);
        if (SUCCEEDED(hr)) {
            hr=IDXGISwapChain1_QueryInterface(chain,&IID_IDXGISwapChain3,(void **)&a->display.chain);
            IDXGISwapChain1_Release(chain);
        }
        if (SUCCEEDED(hr)) {
            static LONG64 serial;
            a->display.cookie=(HANDLE)(UINT_PTR)InterlockedIncrement64(&serial);
            if (!SetPropW(window,L"Scarlett.Present.Owner",a->display.cookie)) {
                IDXGISwapChain3_Release(a->display.chain); a->display.chain=NULL;
                ReleaseSRWLockExclusive(&a->display.lock); return E_FAIL;
            }
            a->display.window=window;
            a->display.width=width; a->display.height=height;
            IDXGISwapChain3_AddRef(a->display.chain); *out=a->display.chain;
        }
    }
    ReleaseSRWLockExclusive(&a->display.lock);
    return hr;
}

/* Private identity only: don't advertise a console resource ABI we cannot service. */
static const IUnknownVtbl surface_vtbl = {query, addref, release};

ID3D12Resource *xs_surface_host(IUnknown *surface)
{
    return surface && surface->lpVtbl == &surface_vtbl ?
            CONTAINING_RECORD(surface,struct adapter,iface)->host.surface : NULL;
}

HRESULT xs_display_wrap(IUnknown *queue, UINT index, ID3D12Resource *render, IUnknown **out)
{
    if (!out) return E_POINTER;
    *out=NULL;
    if (!as_queue(queue) || index>=2 || !render) return E_INVALIDARG;
    struct adapter *a=CONTAINING_RECORD(queue,struct adapter,iface);
    AcquireSRWLockExclusive(&a->display.lock);
    HRESULT hr=DXGI_ERROR_INVALID_CALL;
    if (a->display.chain && !a->display.keys[index]) {
        ID3D12Resource *buffer;
        hr=IDXGISwapChain3_GetBuffer(a->display.chain,index,&IID_ID3D12Resource,(void **)&buffer);
        if (SUCCEEDED(hr)) {
            if (buffer!=render) hr=E_INVALIDARG;
            else {
                struct adapter *s=HeapAlloc(GetProcessHeap(),0,sizeof(*s));
                if (!s) hr=E_OUTOFMEMORY;
                else {
                    s->iface.lpVtbl=&surface_vtbl;
                    s->refs=2; /* caller and queue; wrapper owns the GetBuffer reference */
                    s->host.surface=buffer;
                    a->display.keys[index]=*out=&s->iface;
                }
            }
            if (FAILED(hr)) ID3D12Resource_Release(buffer);
        }
    }
    ReleaseSRWLockExclusive(&a->display.lock);
    return hr;
}

static BOOL read_wire(const void *src, void *dst, SIZE_T bytes)
{
    SIZE_T n;
    return src && ReadProcessMemory(GetCurrentProcess(),src,dst,bytes,&n) && n==bytes;
}

#include "offscreen.h"

static HRESULT WINAPI queue_present(IUnknown *iface, UINT count, const void *planes, const void *params)
{
    struct xs_plane p;
    struct xs_present_params opts;
    IUnknown *key;
    RECT rect;
    float dst[4];
    if (count!=1) return E_NOTIMPL;
    if (!read_wire(planes,&p,sizeof(p))) return E_POINTER;
    if (p.count!=1 || p.color || p.filter || p.ext_count || p.extensions || p.flags) return E_NOTIMPL;
    if (!p.token) return E_INVALIDARG;
    if (!read_wire(p.resources,&key,sizeof(key))) return E_POINTER;
    if (!key) return E_INVALIDARG;
    if (!read_wire(params,&opts,sizeof(opts))) return E_POINTER;
    if (opts.threshold!=0 || opts.views || opts.ext_count || opts.extensions || opts.flags) return E_NOTIMPL;
    if (!read_wire(p.dst,dst,sizeof(dst)) || !read_wire(p.src,&rect,sizeof(rect))) return E_POINTER;
    if (dst[0]!=0 || dst[1]!=0 || dst[2]!=1 || dst[3]!=1) return E_NOTIMPL;
    struct adapter *a=CONTAINING_RECORD(iface,struct adapter,iface);
    AcquireSRWLockExclusive(&a->display.lock);
    HRESULT hr=E_NOTIMPL;
    if (a->display.chain && !rect.left && !rect.top && rect.right==(LONG)a->display.width &&
            rect.bottom==(LONG)a->display.height) {
        UINT index=IDXGISwapChain3_GetCurrentBackBufferIndex(a->display.chain);
        if (a->display.primary && index<2 && key==a->display.primary_key && p.token==a->display.primary_token &&
                GetPropW(a->display.window,L"Scarlett.Present.Owner")==a->display.cookie) {
            hr=xs_frame_present_after(a->display.owner,p.token,(IDXGISwapChain *)a->display.chain,copy_primary,a);
            IUnknown *held=a->display.primary_key;
            ID3D12Resource *source=a->display.primary;
            a->display.primary=NULL; a->display.primary_key=NULL;
            ReleaseSRWLockExclusive(&a->display.lock);
            ID3D12Resource_Release(source); IUnknown_Release(held);
            return hr;
        } else if (!a->display.primary && index<2 && key==a->display.keys[index])
            hr=xs_frame_present(a->display.owner,p.token,(IDXGISwapChain *)a->display.chain);
        else hr=DXGI_ERROR_INVALID_CALL;
    }
    ReleaseSRWLockExclusive(&a->display.lock);
    return hr;
}
