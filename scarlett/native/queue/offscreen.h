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
