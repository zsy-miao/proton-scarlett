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

ID3D12CommandQueue *xs_display_queue(IUnknown *queue)
{
    ID3D12CommandQueue *q=as_queue(queue);
#ifdef SCARLETT_EXPERIMENTAL_DIRECT_WAIT
    if (q) {
#ifdef SCARLETT_EXPERIMENTAL_BRIDGE
        if(!escape_native())return NULL;
#endif
        struct adapter *a=CONTAINING_RECORD(queue,struct adapter,iface);
        AcquireSRWLockExclusive(&a->submit_lock);
        a->external_submissions=TRUE;
        ReleaseSRWLockExclusive(&a->submit_lock);
    }
#endif
    return q;
}

#ifdef SCARLETT_EXPERIMENTAL_BRIDGE
static void initialize_private_display(struct adapter *a);
#endif
HRESULT xs_console_direct_queue(ID3D12Device *host, IUnknown *owner, IUnknown **out)
{
    if (!out) return E_POINTER;
    *out=NULL;
    if (!owner) return E_INVALIDARG;
    HRESULT hr=xs_direct_queue(host,out);
    if (SUCCEEDED(hr)) {
        IUnknown_AddRef(owner);
        struct adapter *a=CONTAINING_RECORD(*out,struct adapter,iface);a->display.owner=owner;
#ifdef SCARLETT_EXPERIMENTAL_BRIDGE
        if(xs_title_window_query(&a->display.captured,NULL)) initialize_private_display(a);
#endif
    }
    return hr;
}

#include "title_window.h"
static HRESULT display_open(IUnknown *queue, IDXGIFactory2 *factory, HWND window,
        UINT width, UINT height, IDXGISwapChain3 **out,const struct xs_title_window *title,ID3D12CommandQueue *private_queue)
{
    if (!out && !private_queue) return E_POINTER;
    if (out) *out=NULL;
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
        /* DXGI can enqueue outside our submit lock, just like a borrowed host queue. */
        if (out && !xs_display_queue(queue)) {ReleaseSRWLockExclusive(&a->display.lock);return E_ACCESSDENIED;}
        hr=IDXGIFactory2_CreateSwapChainForHwnd(factory,(IUnknown *)(private_queue?private_queue:q),window,&sd,NULL,NULL,&chain);
        if (SUCCEEDED(hr)) {
            hr=IDXGISwapChain1_QueryInterface(chain,&IID_IDXGISwapChain3,(void **)&a->display.chain);
            IDXGISwapChain1_Release(chain);
        }
        if (SUCCEEDED(hr)) {
            static LONG64 serial;
            struct xs_title_window confirmed;
            if (title && !xs_title_window_query(&confirmed,title)) {
                IDXGISwapChain3_Release(a->display.chain); a->display.chain=NULL;
                ReleaseSRWLockExclusive(&a->display.lock); return DXGI_ERROR_INVALID_CALL;
            }
            a->display.cookie=(HANDLE)(UINT_PTR)InterlockedIncrement64(&serial);
            if (!SetPropW(window,L"Scarlett.Present.Owner",a->display.cookie)) {
                IDXGISwapChain3_Release(a->display.chain); a->display.chain=NULL;
                ReleaseSRWLockExclusive(&a->display.lock); return E_FAIL;
            }
            a->display.window=window;
            a->display.width=width; a->display.height=height;
            if (private_queue) fprintf(stderr,"scarlett: private chain opened on creator %lu\n",GetCurrentThreadId());
            if (out) {
                IDXGISwapChain3_AddRef(a->display.chain); *out=a->display.chain;
#ifdef SCARLETT_EXPERIMENTAL_BRIDGE
                InterlockedExchange(&a->display.public_chain,1);
#endif
            }
#ifdef SCARLETT_EXPERIMENTAL_BRIDGE
            else {
                AcquireSRWLockExclusive(&a->submit_lock);
                if (a->external_submissions) {
                    RemovePropW(window,L"Scarlett.Present.Owner");a->display.window=NULL;
                    IDXGISwapChain3_Release(a->display.chain);a->display.chain=NULL;hr=E_ACCESSDENIED;
                } else {
                    a->display.creator=title->tid;
                    ID3D12CommandQueue_AddRef(private_queue);a->display.wsi=private_queue;
                }
                ReleaseSRWLockExclusive(&a->submit_lock);
            }
#endif
        }
    }
    ReleaseSRWLockExclusive(&a->display.lock);
    return hr;
}

HRESULT xs_display_open(IUnknown *queue,IDXGIFactory2 *factory,HWND window,UINT width,UINT height,IDXGISwapChain3 **out)
{
    return display_open(queue,factory,window,width,height,out,NULL,NULL);
}

HRESULT xs_display_open_title(IUnknown *queue,IDXGIFactory2 *factory,UINT width,UINT height,IDXGISwapChain3 **out)
{
    struct xs_title_window title;
    if (!out) return E_POINTER;
    *out=NULL;
    if (!xs_title_window_query(&title,NULL)) return DXGI_ERROR_NOT_CURRENTLY_AVAILABLE;
    return display_open(queue,factory,title.hwnd,width,height,out,&title,NULL);
}
#ifdef SCARLETT_EXPERIMENTAL_BRIDGE
/* No native queue or chain crosses the API. DXGI gets a separate Direct queue. */
static HRESULT open_on_creator(struct adapter *a,const struct xs_title_window *expected)
{
    struct xs_title_window w;ID3D12Device *d;IDXGIFactory2 *factory;ID3D12CommandQueue *wsi;
    IUnknown *iface=&a->iface;ID3D12CommandQueue *q=as_queue(iface);
    if (!xs_title_window_query(&w,expected)) return DXGI_ERROR_INVALID_CALL;
    HRESULT hr=ID3D12CommandQueue_GetDevice(q,&IID_ID3D12Device,(void**)&d);
    if (FAILED(hr)) return hr;
    D3D12_COMMAND_QUEUE_DESC desc={.Type=D3D12_COMMAND_LIST_TYPE_DIRECT,.NodeMask=1};
    hr=ID3D12Device_CreateCommandQueue(d,&desc,&IID_ID3D12CommandQueue,(void**)&wsi);
    ID3D12Device_Release(d);
    if (FAILED(hr)) return hr;
    hr=CreateDXGIFactory1(&IID_IDXGIFactory2,(void**)&factory);
    if (SUCCEEDED(hr)) {
        hr=display_open(iface,factory,w.hwnd,3840,2160,NULL,&w,wsi);
        IDXGIFactory2_Release(factory);
    }
    ID3D12CommandQueue_Release(wsi);
    return hr;
}
static SRWLOCK initializing=SRWLOCK_INIT;
static HWND opening;
static LRESULT CALLBACK guard_open(int code,WPARAM w,LPARAM l)
{
    if(code==HCBT_DESTROYWND && (HWND)w==opening)return 1;
    return CallNextHookEx(NULL,code,w,l);
}
static void initialize_private_display(struct adapter *a)
{
    if(!TryAcquireSRWLockExclusive(&initializing))return;
    opening=a->display.captured.hwnd;
    HHOOK h=SetWindowsHookExW(WH_CBT,guard_open,NULL,GetCurrentThreadId());
    HRESULT hr=h?open_on_creator(a,&a->display.captured):E_ACCESSDENIED;
    UINT count=~0u;
    HRESULT query=SUCCEEDED(hr)?IDXGISwapChain3_GetLastPresentCount(a->display.chain,&count):hr;
    fprintf(stderr,"WSI init tid=%lu hwnd=%p generation=%zu hr=%lx count_hr=%lx count=%u\n",GetCurrentThreadId(),opening,a->display.captured.generation,hr,query,count);
    if(h)UnhookWindowsHookEx(h);
    opening=NULL;ReleaseSRWLockExclusive(&initializing);
}

#endif

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
#ifdef SCARLETT_EXPERIMENTAL_BRIDGE
    if (a->display.wsi) {ReleaseSRWLockExclusive(&a->display.lock);return E_ACCESSDENIED;}
#endif
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
#ifdef SCARLETT_EXPERIMENTAL_BRIDGE
#include "present_relay.h"
#endif

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
#ifdef SCARLETT_EXPERIMENTAL_BRIDGE
    if (a->display.captured.hwnd && !InterlockedCompareExchange(&a->display.public_chain,0,0)) {
        UINT_PTR ret=(UINT_PTR)__builtin_return_address(0)-(UINT_PTR)GetModuleHandleW(NULL);
        fprintf(stderr,"scarlett: captured-window PresentX return RVA +%zx token=%llu key=%p queue=%p tid=%lu creator=%lu src=%ld,%ld,%ld,%ld\n",
                ret,p.token,key,iface,GetCurrentThreadId(),a->display.captured.tid,rect.left,rect.top,rect.right,rect.bottom);
        HRESULT hr=(rect.left || rect.top || rect.right!=3840 || rect.bottom!=2160)?E_INVALIDARG:worker_present(a,p.token,key);
        fprintf(stderr,"scarlett: private PresentX result=%lx token=%llu\n",hr,p.token);
        return hr;
    }
#endif
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
