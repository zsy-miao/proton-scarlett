/* Private single-use generation: one pooled recorder, one private allocator.
 * Holding the closed recorder also holds its original PSO/root, attachment
 * identities, frozen OM heaps, shader banks and captured source allocation. */
static HRESULT ordered_ready(struct startup_list *s)
{
    if (!s->single_use || !s->closed || s->error!=S_OK || !s->reservation ||
        s->type!=D3D12_COMMAND_LIST_TYPE_DIRECT || !s->host) return E_INVALIDARG;
    /* This control has no consumer lifetime contract for the colour VB/table. */
    if (s->previous_vb.buffer || s->previous_vb.placed || s->vb.buffer || s->vb.placed || s->b27.heap || s->host_exposed ||
        s->prior_pso || s->pending_470 || s->depth_zero_pending) return E_ACCESSDENIED;
    if (!s->cache_page || memcmp(s->cache_page,cache_template,sizeof cache_template)) return E_INVALIDARG;
    /* Split recordings have queue-side operations between segments. Never flatten them. */
    if (s->head || s->marker || s->post || s->stamp || s->tail_allocator) return E_NOTIMPL;
    if (s->primary && (!s->primary_released || !s->primary_cleared)) return E_INVALIDARG;
    return S_OK;
}
HRESULT xs_ordered_record(IUnknown *iface,HRESULT (*record)(IUnknown *,void *),void *ctx,
        IUnknown *lease,IUnknown *primary_key,IUnknown **out)
{
    if (!out) return E_POINTER;
    *out=NULL;
    ID3D12CommandQueue *q=as_queue(iface);
    if (!q || !record || !lease) return E_INVALIDARG;
    struct adapter *a=CONTAINING_RECORD(iface,struct adapter,iface);
    D3D12_COMMAND_QUEUE_DESC desc;q->lpVtbl->GetDesc(q,&desc);
    if (!a->display.owner || desc.Type!=D3D12_COMMAND_LIST_TYPE_DIRECT) return E_INVALIDARG;
    ID3D12Device *d;
    HRESULT hr=ID3D12CommandQueue_GetDevice(q,&IID_ID3D12Device,(void**)&d);
    if (FAILED(hr)) return hr;
    IUnknown *alloc=NULL,*list=NULL;
    hr=xs_allocator(d,desc.Type,&alloc);
    if (hr==S_OK) hr=xs_device_list(d,desc.Type,alloc,TRUE,a->display.owner,&list);
    if (alloc) IUnknown_Release(alloc);
    ID3D12Device_Release(d);
    if (hr!=S_OK) return hr;
    struct startup_list *s=sl(list);
    s->single_use=TRUE;
    hr=record(list,ctx);
    if (hr==S_OK) hr=sl_close(list);
    if (hr==S_OK) hr=ordered_ready(s);
    if (hr==S_OK && (primary_key || s->primary)) {
        ID3D12Resource *named=xs_retain_primary(s->pool_owner,primary_key);
        if (!named || named!=s->primary) hr=E_INVALIDARG;
        if (named) ID3D12Resource_Release(named);
    }
    /* Guest inline cache is the only writable ABI page on a closed pooled list. */
    DWORD old;
    if (hr==S_OK && !VirtualProtect(s->cache_page,sizeof cache_template,PAGE_READONLY,&old))
        hr=HRESULT_FROM_WIN32(GetLastError());
    if (hr!=S_OK) {IUnknown_Release(list);return hr;}
    s->ordered_queue=a;IUnknown_AddRef(iface);
    s->ordered_id=list;s->ordered_lease=lease;IUnknown_AddRef(lease);
    if ((s->primary_key=primary_key)) IUnknown_AddRef(primary_key);
    AcquireSRWLockExclusive(&a->submit_lock);
    s->ordered_next=a->ordered;a->ordered=s;
    ReleaseSRWLockExclusive(&a->submit_lock);
    *out=list;return S_OK;
}

#define ORDERED_MAX 16
struct ordered_work {
    UINT count;
    struct startup_list *gen[ORDERED_MAX];
    ID3D12CommandList *host[ORDERED_MAX];
    ID3D12Fence *end;
    HANDLE event;
};
static void CALLBACK ordered_retire(PTP_CALLBACK_INSTANCE instance,void *ctx,PTP_WAIT wait,TP_WAIT_RESULT result)
{
    struct ordered_work *w=ctx;
    (void)instance;(void)result;
    for (UINT i=0;i<w->count;i++) sl_release(w->gen[i]->ordered_id);
    ID3D12Fence_Release(w->end);CloseHandle(w->event);CloseThreadpoolWait(wait);
    HeapFree(GetProcessHeap(),0,w);
}
#ifdef TEST_ORDERED_BATCH
static UINT ordered_host_submissions;
#endif
/* Both submit_lock and frame clock locked. No recoverable failure after Execute. */
static HRESULT ordered_execute(void *ctx)
{
    struct ordered_work *w=ctx;
    struct adapter *a=w->gen[0]->ordered_queue;
#ifdef SCARLETT_EXPERIMENTAL_BRIDGE
    struct startup_list *last=w->gen[w->count-1];
    if (last->primary && FAILED(xs_primary_writer(last->pool_owner,last->primary,a->host.queue)))
        return E_ACCESSDENIED;
#endif
    for (UINT i=0;i<w->count;i++) {
        sl_addref(w->gen[i]->ordered_id);
        w->gen[i]->consumed=TRUE;
    }
    ID3D12CommandQueue_ExecuteCommandLists(a->host.queue,w->count,w->host);
#ifdef TEST_ORDERED_BATCH
    ordered_host_submissions++;
#endif
    if (FAILED(ID3D12CommandQueue_Signal(a->host.queue,w->end,1))) xs_stop();
#ifdef SCARLETT_EXPERIMENTAL_BRIDGE
    forget_release(a);
    if (last->primary && a->display.captured.hwnd) {
        a->display.released=last->primary;ID3D12Resource_AddRef(last->primary);
        a->display.released_end=w->end;ID3D12Fence_AddRef(w->end);
        a->display.released_token=2;a->display.clear_slot=last->clear_slot;a->display.clear_serial=last->clear_serial;
    }
#endif
    return S_OK;
}
HRESULT xs_ordered_submit(IUnknown *iface,UINT count,IUnknown *const *lists,UINT64 token,UINT flags,ID3D12Fence **end)
{
    if (end) *end=NULL;
    ID3D12CommandQueue *q=as_queue(iface);
    if (!q || count<2 || count>ORDERED_MAX || flags!=3 || token!=2) return E_NOTIMPL;
    IUnknown *ids[ORDERED_MAX];SIZE_T got,size=count*sizeof *ids;
    if (!ReadProcessMemory(GetCurrentProcess(),lists,ids,size,&got) || got!=size) return E_POINTER;
    struct adapter *a=CONTAINING_RECORD(iface,struct adapter,iface);
    struct ordered_work *w=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof *w);
    if (!w) return E_OUTOFMEMORY;
    w->count=count;
    HRESULT hr=E_INVALIDARG;
    PTP_WAIT wait=NULL;
    AcquireSRWLockExclusive(&a->submit_lock);
    if (a->external_submissions) {hr=E_ACCESSDENIED;goto out;}
    for (UINT i=0;i<count;i++) {
        struct startup_list *s=a->ordered;
        while (s && s->ordered_id!=ids[i]) s=s->ordered_next;
        if (!s || s->consumed) goto out;
        for (UINT j=0;j<i;j++) if (w->gen[j]==s) goto out;
        if (s->pool_owner!=a->display.owner || (s->primary && i+1!=count)) goto out;
        HRESULT ready=ordered_ready(s);
        if (ready!=S_OK) {hr=ready;goto out;}
        w->gen[i]=s;w->host[i]=(ID3D12CommandList *)s->host;
    }
    ID3D12Device *d=w->gen[0]->device;
    hr=ID3D12Device_CreateFence(d,0,0,&IID_ID3D12Fence,(void**)&w->end);
    if (FAILED(hr)) goto out;
    if (!(w->event=CreateEventW(NULL,FALSE,FALSE,NULL)) || !(wait=CreateThreadpoolWait(ordered_retire,w,NULL))) {
        hr=HRESULT_FROM_WIN32(GetLastError());goto out;
    }
    hr=xs_frame_submit(a->display.owner,token,q,TRUE,w->end,ordered_execute,w);
    if (hr==S_OK) {
        if (FAILED(ID3D12Fence_SetEventOnCompletion(w->end,1,w->event))) xs_stop();
        if (end) {ID3D12Fence_AddRef(w->end);*end=w->end;}
        SetThreadpoolWait(wait,w->event,NULL);
    }
out:
    ReleaseSRWLockExclusive(&a->submit_lock);
    if (hr!=S_OK) {
        if (wait) CloseThreadpoolWait(wait);
        if (w->event) CloseHandle(w->event);
        if (w->end) ID3D12Fence_Release(w->end);
        HeapFree(GetProcessHeap(),0,w);
    }
    return hr;
}
