/* Included by present_impl.h. One in-flight handoff, ordered on the rendering queue. */
HRESULT xs_display_arm_primary(IUnknown *queue,ID3D12CommandQueue *producer,HWND window,
        UINT64 token,IUnknown *key,ID3D12Resource *source,D3D12_RESOURCE_STATES state,
        UINT guest_flags,UINT guest_heap_flags)
{
    if (!producer || as_queue(queue)!=producer || !window || !token || !key || !source) return E_INVALIDARG;
    if (guest_flags!=0x300005 || guest_heap_flags!=8) return E_NOTIMPL;
    if (state!=D3D12_RESOURCE_STATE_RENDER_TARGET
#ifdef SCARLETT_EXPERIMENTAL_PRIMARY_RELEASE
        && state!=D3D12_RESOURCE_STATE_COMMON
#endif
        ) return E_NOTIMPL;
    D3D12_RESOURCE_DESC r;
    source->lpVtbl->GetDesc(source,&r);
    if (r.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D || r.Width!=3840 || r.Height!=2160 ||
        r.DepthOrArraySize!=1 || r.MipLevels!=1 || r.Format!=DXGI_FORMAT_R8G8B8A8_UNORM ||
        r.SampleDesc.Count!=1 || r.SampleDesc.Quality || r.Layout!=D3D12_TEXTURE_LAYOUT_UNKNOWN ||
        r.Flags!=(D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET|D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)) return E_NOTIMPL;
    ID3D12Device *d=NULL,*s=NULL;
    HRESULT hr=ID3D12CommandQueue_GetDevice(producer,&IID_ID3D12Device,(void **)&d);
    if (SUCCEEDED(hr)) hr=ID3D12Resource_GetDevice(source,&IID_ID3D12Device,(void **)&s);
    if (SUCCEEDED(hr) && s!=d) hr=E_INVALIDARG;
    if (s) ID3D12Device_Release(s);
    if (d) ID3D12Device_Release(d);
    if (FAILED(hr)) return hr;
    D3D12_HEAP_PROPERTIES hp;
    D3D12_HEAP_FLAGS flags;
    if (FAILED(ID3D12Resource_GetHeapProperties(source,&hp,&flags)) || hp.Type!=D3D12_HEAP_TYPE_DEFAULT ||
        hp.CreationNodeMask!=1 || hp.VisibleNodeMask!=1) return E_INVALIDARG;
    struct adapter *a=CONTAINING_RECORD(queue,struct adapter,iface);
    AcquireSRWLockExclusive(&a->display.lock);
#ifdef SCARLETT_EXPERIMENTAL_BRIDGE
    if (a->display.wsi) {ReleaseSRWLockExclusive(&a->display.lock);return E_ACCESSDENIED;}
#endif
    hr=DXGI_ERROR_INVALID_CALL;
    if (a->display.chain && !a->display.primary && a->display.width==3840 && a->display.height==2160 &&
        !a->display.keys[0] && !a->display.keys[1] && a->display.window==window &&
        GetPropW(window,L"Scarlett.Present.Owner")==a->display.cookie) {
        IUnknown_AddRef(key); ID3D12Resource_AddRef(source);
        a->display.primary_key=key; a->display.primary=source; a->display.primary_state=state;
        a->display.primary_token=token; hr=S_OK;
    }
    ReleaseSRWLockExclusive(&a->display.lock);
    return hr;
}

static HRESULT copy_primary(void *context)
{
    struct adapter *a=context;
    ID3D12Resource *src=a->display.primary,*dst=NULL;
    ID3D12Device *d=NULL;
    ID3D12CommandAllocator *alloc=NULL;
    ID3D12GraphicsCommandList *list=NULL;
    ID3D12Fence *done=NULL;
    ID3D12CommandQueue *q=a->host.queue;
#ifdef SCARLETT_EXPERIMENTAL_BRIDGE
    if (a->display.wsi) q=a->display.wsi;
#endif
    IDXGISwapChain3 *chain=a->display.chain;
    UINT index=IDXGISwapChain3_GetCurrentBackBufferIndex(chain);
    HRESULT hr=IDXGISwapChain3_GetBuffer(chain,index,&IID_ID3D12Resource,(void **)&dst);
    if (FAILED(hr)) return hr;
    D3D12_RESOURCE_DESC r; dst->lpVtbl->GetDesc(dst,&r);
    if (src==dst || r.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D || r.Width!=3840 || r.Height!=2160 ||
        r.Format!=DXGI_FORMAT_R8G8B8A8_UNORM || r.SampleDesc.Count!=1 || r.SampleDesc.Quality ||
        r.DepthOrArraySize!=1 || r.MipLevels!=1) { hr=DXGI_ERROR_INVALID_CALL; goto end; }
    hr=ID3D12CommandQueue_GetDevice(q,&IID_ID3D12Device,(void **)&d);
    if (SUCCEEDED(hr)) hr=ID3D12Device_CreateFence(d,0,0,&IID_ID3D12Fence,(void **)&done);
    if (SUCCEEDED(hr)) hr=ID3D12Device_CreateCommandAllocator(d,D3D12_COMMAND_LIST_TYPE_DIRECT,&IID_ID3D12CommandAllocator,(void **)&alloc);
    if (SUCCEEDED(hr)) hr=ID3D12Device_CreateCommandList(d,0,D3D12_COMMAND_LIST_TYPE_DIRECT,alloc,NULL,&IID_ID3D12GraphicsCommandList,(void **)&list);
    if (FAILED(hr)) goto end;
    D3D12_RESOURCE_BARRIER b[]={
        {.Transition={src,0,a->display.primary_state,D3D12_RESOURCE_STATE_COPY_SOURCE}},
        {.Transition={dst,0,D3D12_RESOURCE_STATE_PRESENT,D3D12_RESOURCE_STATE_COPY_DEST}}};
    ID3D12GraphicsCommandList_ResourceBarrier(list,2,b);
    ID3D12GraphicsCommandList_CopyResource(list,dst,src);
    for (unsigned i=0;i<2;i++) {
        D3D12_RESOURCE_STATES s=b[i].Transition.StateBefore;
        b[i].Transition.StateBefore=b[i].Transition.StateAfter; b[i].Transition.StateAfter=s;
    }
    ID3D12GraphicsCommandList_ResourceBarrier(list,2,b);
    hr=ID3D12GraphicsCommandList_Close(list);
    if (SUCCEEDED(hr)) {
        ID3D12CommandList *batch[]={(ID3D12CommandList *)list};
        ID3D12CommandQueue_ExecuteCommandLists(q,1,batch);
        hr=ID3D12CommandQueue_Signal(q,done,1);
        if (SUCCEEDED(hr)) hr=ID3D12Fence_SetEventOnCompletion(done,1,NULL); /* synchronous retirement */
        if (FAILED(hr) || ID3D12Fence_GetCompletedValue(done)!=1) {
            hr=ID3D12Device_GetDeviceRemovedReason(d);
            if (SUCCEEDED(hr)) xs_stop(); /* cannot release potentially in-flight resources */
        }
    }
end:
    if (list) ID3D12GraphicsCommandList_Release(list);
    if (alloc) ID3D12CommandAllocator_Release(alloc);
    if (done) ID3D12Fence_Release(done);
    if (d) ID3D12Device_Release(d);
    ID3D12Resource_Release(dst);
    return hr;
}
#ifdef SCARLETT_EXPERIMENTAL_BRIDGE
static BOOL bridge_window(struct adapter *a)
{
    DWORD pid=0;HWND hwnd=NULL;
    return SUCCEEDED(IDXGISwapChain3_GetHwnd(a->display.chain,&hwnd)) && hwnd==a->display.window &&
        GetWindowThreadProcessId(hwnd,&pid)==a->display.creator && pid==GetCurrentProcessId() &&
        GetPropW(hwnd,L"Scarlett.Present.Owner")==a->display.cookie;
}
struct bridge_copy {struct adapter *a;BOOL locked;};
static HRESULT bridge_copy(void *ctx)
{
    struct bridge_copy *c=ctx;
    HRESULT hr=copy_primary(c->a);
#ifdef TEST_PRESENT_BRIDGE
    if(SUCCEEDED(hr) && xs_primary_writer(c->a->display.owner,c->a->display.primary,c->a->host.queue)!=DXGI_ERROR_WAS_STILL_DRAWING) xs_stop();
#endif
    if (SUCCEEDED(hr) && !bridge_window(c->a)) hr=DXGI_ERROR_INVALID_CALL;
    /* Source state is restored and the copy fence has retired. DXGI may dispatch
     * window messages during Present; no producer lock is needed for the flip. */
    forget_release(c->a);ReleaseSRWLockExclusive(&c->a->submit_lock);c->locked=FALSE;
    return hr;
}
static HRESULT present_released(struct adapter *a,UINT64 token,IUnknown *key,ID3D12Fence *end)
{
    HRESULT hr=DXGI_ERROR_INVALID_CALL;
    struct bridge_copy c={a,TRUE};
    AcquireSRWLockExclusive(&a->submit_lock);
    if (a->external_submissions) hr=E_ACCESSDENIED;
    else if (a->display.released && token==a->display.released_token && end==a->display.released_end) {
        ID3D12Resource *src=a->display.released;
        IUnknown *pin=xs_claim_view(a->display.owner,key,src,a->host.queue,a->display.clear_slot,a->display.clear_serial,TRUE);
        if (pin) {
            UINT64 done=ID3D12Fence_GetCompletedValue(a->display.released_end);
            fprintf(stderr,"WSI lease: primary=%p token=%llu serial=%llu fence=%p entry=%llu\n",key,token,a->display.clear_serial,a->display.released_end,done);
            if (done==0) hr=DXGI_ERROR_WAS_STILL_DRAWING;
            else if (done==UINT64_MAX) hr=DXGI_ERROR_DEVICE_REMOVED;
            else if (done==1 && !bridge_window(a)){HWND h=NULL;HRESULT hhr=IDXGISwapChain3_GetHwnd(a->display.chain,&h);fprintf(stderr,"DETAIL window get=%lx chain=%p live=%p tid=%lu/%lu cookie=%p/%p\n",hhr,h,a->display.window,GetWindowThreadProcessId(h,NULL),a->display.creator,GetPropW(h,L"Scarlett.Present.Owner"),a->display.cookie);}
            else if (done==1) {
#ifdef SCARLETT_PAIR_SUBMIT
                const SIZE_T slot=a->display.clear_slot;
                const UINT64 serial=a->display.clear_serial;
                const UINT ring=IDXGISwapChain3_GetCurrentBackBufferIndex(a->display.chain);
#endif
                a->display.primary=src;a->display.primary_state=D3D12_RESOURCE_STATE_COMMON;
                /* Serialize against tracked submissions and raw queue exposure until the
                 * private queue has retired the copy. Only its independent DXGI flip follows. */
                hr=xs_frame_present_release(a->display.owner,token,(IDXGISwapChain *)a->display.chain,bridge_copy,&c,
                        a->host.queue,a->display.released_end);
                a->display.primary=NULL;
#ifdef SCARLETT_PAIR_SUBMIT
                /* Released producer fence and COMMON-restoring copy fence both retired. */
                if(hr==S_OK && token==1 && ring==0 && IDXGISwapChain3_GetCurrentBackBufferIndex(a->display.chain)==1)
                    xs_returned_common(pin,slot,serial);
#endif
            }
        }
        if (pin) {xs_finish_flip(pin);IUnknown_Release(pin);}
    }
    if (c.locked) ReleaseSRWLockExclusive(&a->submit_lock);
    return hr;
}
#endif
