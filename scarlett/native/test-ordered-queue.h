/* Private single-use generation: one pooled recorder, one private allocator.
 * Holding the closed recorder also holds its original PSO/root, attachment
 * identities, frozen OM heaps, shader banks and captured source allocation. */
static HRESULT ordered_ready(struct startup_list *s)
{
    if (!s->single_use || !s->closed || s->error!=S_OK || !s->reservation ||
        s->type!=D3D12_COMMAND_LIST_TYPE_DIRECT || !s->host) return E_INVALIDARG;
    /* This control has no consumer lifetime contract for the colour VB/table. */
    if (s->previous_vb.buffer || s->previous_vb.placed || s->vb.buffer || s->vb.placed || s->table0.heap || s->host_exposed ||
        s->prior_pso || s->pending_470 || s->depth_zero_pending) return E_ACCESSDENIED;
    if (!s->cache_page || memcmp(s->cache_page,cache_template,sizeof cache_template)) return E_INVALIDARG;
#ifdef TEST_PAIR_SPLIT
    if(s->head) {
        struct prefix_marker *m=s->marker,*p=s->post;
        if(!s->exports_rt || s->requires_rt || s->head==s->host || !allocator_matches(s->tail_allocator,s->device) ||
           !s->heaps[0] || !s->heaps[1] || !s->stamp || !s->stamp->section || !s->stamp->query || !s->stamp->dst ||
           s->stamp->tail_recorded || !m || m->value || !p || p->parent!=m || !p->counter || p->threshold!=1 ||
           s->post_boundary)return E_NOTIMPL;
        if(m->buffer ? (!m->control_section || m->alias || m->native_post!=0x29a5ee15 || p->word || !s->stamp->retained) :
           (!m->alias || p->word!=m->word+1 || p->value!=0x29a5ee15 || p->counter!=m->alias ||
            !s->stamp->mapping))return E_NOTIMPL;
    }else
#endif
    if (s->head || s->marker || s->post || s->stamp || s->tail_allocator) return E_NOTIMPL;
#ifdef TEST_SAME_BATCH_RELEASE
    if(s->offered || s->deferred)return E_ACCESSDENIED; /* Never consume retail receipts. */
    if(s->exports_rt || s->requires_rt){
        if (!s->primary || s->primary_pin!=(IUnknown*)s->primary_key || !s->primary_pin ||
            !s->clear_slot || !s->clear_serial || s->primary_released ||
            (s->exports_rt ? !s->primary_cleared || s->requires_rt!=NULL : s->primary_cleared))
            return E_INVALIDARG;
    }else
#endif
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
        if (!named || named!=s->primary || primary_key!=s->primary_pin) hr=E_INVALIDARG;
        if (named) ID3D12Resource_Release(named);
    }
#ifdef TEST_SAME_BATCH_RELEASE
    if(hr==S_OK && s->exports_rt)hr=xs_pair_writer(s->pool_owner,s->primary_pin,s->primary,
            s->clear_slot,s->clear_serial,q,&s->writer_epoch,FALSE);
#endif
    /* Guest inline cache is the only writable ABI page on a closed pooled list. */
    DWORD old;
    if (hr==S_OK && !VirtualProtect(s->cache_page,sizeof cache_template,PAGE_READONLY,&old))
        hr=HRESULT_FROM_WIN32(GetLastError());
    if (hr!=S_OK) {IUnknown_Release(list);return hr;}
    s->ordered_queue=a;IUnknown_AddRef(iface);
    s->ordered_id=list;s->ordered_lease=lease;IUnknown_AddRef(lease);
    AcquireSRWLockExclusive(&a->submit_lock);
    s->ordered_next=a->ordered;a->ordered=s;
    ReleaseSRWLockExclusive(&a->submit_lock);
    *out=list;return S_OK;
}

#define ORDERED_MAX 16
#ifdef TEST_SAME_BATCH_RELEASE
/* Called by the private producer after its final RT use. The exact acquire,
 * clear and descriptor registration (not OM alone) establish the RT output. */
HRESULT test_export_rt(IUnknown *list,UINT64 *serial)
{
    struct startup_list *s=sl(list);
    if(!s->single_use || !open_direct_state(s) || !s->primary || !s->primary_pin || !s->primary_cleared ||
       s->requires_rt || s->exports_rt || s->output_heap || !s->clear_serial)return E_INVALIDARG;
    HRESULT hr=xs_freeze_primary_output(s->pool_owner,s->primary_pin,s->primary,s->clear_slot,s->clear_serial,
        &s->output_heap,&s->output_cpu,&s->output_serial);
    if(hr==S_OK){s->exports_rt=TRUE;*serial=s->output_serial;}
    return hr;
}
struct pair_intent {IUnknown *pred,*key;SIZE_T slot;UINT64 serial;D3D12_TEXTURE_BARRIER b;};
static HRESULT record_pair_intent(IUnknown *list,void *ctx)
{
    struct pair_intent *i=ctx;
    struct startup_list *s=sl(list);
    const D3D12_BARRIER_SUBRESOURCE_RANGE all={.IndexOrFirstMipLevel=UINT_MAX};
    D3D12_TEXTURE_BARRIER b=i->b;
    if(b.SyncBefore!=D3D12_BARRIER_SYNC_RENDER_TARGET || b.SyncAfter!=D3D12_BARRIER_SYNC_NONE ||
       b.AccessBefore!=D3D12_BARRIER_ACCESS_RENDER_TARGET || b.AccessAfter!=D3D12_BARRIER_ACCESS_NO_ACCESS ||
       b.LayoutBefore!=D3D12_BARRIER_LAYOUT_RENDER_TARGET || b.LayoutAfter!=(D3D12_BARRIER_LAYOUT)0x1f ||
       b.Flags || memcmp(&b.Subresources,&all,sizeof all) || (IUnknown*)b.pResource!=i->key ||
       !i->serial || !i->slot)return E_INVALIDARG;
    s->primary=xs_acquire_primary(s->pool_owner,i->key,&s->primary_pin);
    if(!s->primary)return E_INVALIDARG;
    s->primary_key=(UINT_PTR)s->primary_pin;
    HRESULT hr=xs_freeze_primary_output(s->pool_owner,s->primary_pin,s->primary,i->slot,i->serial,
        &s->output_heap,&s->output_cpu,&s->output_serial);
    if(FAILED(hr))return hr;
    s->requires_rt=i->pred;s->clear_slot=i->slot;s->clear_serial=s->output_serial;
    s->release_wire=b;
    return S_OK;
}
HRESULT test_record_successor(IUnknown *iface,IUnknown *pred,IUnknown *key,SIZE_T slot,UINT64 serial,
        const D3D12_TEXTURE_BARRIER *wire,IUnknown **out)
{
    if(!out)return E_POINTER;
    *out=NULL;
    if(!as_queue(iface) || !wire)return E_INVALIDARG;
    struct adapter *a=CONTAINING_RECORD(iface,struct adapter,iface);
    AcquireSRWLockShared(&a->submit_lock);
    struct startup_list *p=a->ordered;
    while(p && p->ordered_id!=pred)p=p->ordered_next;
    BOOL ready=p && !p->consumed && p->exports_rt && p->closed;
    ReleaseSRWLockShared(&a->submit_lock);
    if(!ready)return E_INVALIDARG;
    struct pair_intent i={pred,key,slot,serial,*wire};
    return xs_ordered_record(iface,record_pair_intent,&i,pred,key,out);
}
#endif
struct ordered_work {
    UINT count;
    struct startup_list *gen[ORDERED_MAX];
    ID3D12CommandList *host[ORDERED_MAX];
    ID3D12Fence *end;
    HANDLE event;
#ifdef TEST_SAME_BATCH_RELEASE
    BOOL pair;
    ID3D12GraphicsCommandList7 *suffix;
#ifdef TEST_PAIR_SPLIT
    ID3D12Fence *prefix_gate,*post_gate;
#endif
#endif
};
static void CALLBACK ordered_retire(PTP_CALLBACK_INSTANCE instance,void *ctx,PTP_WAIT wait,TP_WAIT_RESULT result)
{
    struct ordered_work *w=ctx;
    (void)instance;(void)result;
#ifdef TEST_SAME_BATCH_RELEASE
    if(w->pair)InterlockedDecrement(&pair_inflight);
    if(w->suffix)ID3D12GraphicsCommandList7_Release(w->suffix);
#ifdef TEST_PAIR_SPLIT
    if(w->prefix_gate)ID3D12Fence_Release(w->prefix_gate);
    if(w->post_gate)ID3D12Fence_Release(w->post_gate);
#endif
#endif
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
#ifdef TEST_SAME_BATCH_RELEASE
    if(w->pair){
        struct startup_list *first=w->gen[0];
        /* Dedicated successor records only intent. Use its pinned allocator for
         * the actual release at that exact position, never close/reset a guest list. */
        HRESULT hr=ID3D12Device_CreateCommandList(last->device,1,last->type,xs_allocator_host(last->allocator,last->type),
                NULL,&IID_ID3D12GraphicsCommandList7,(void**)&w->suffix);
        if(FAILED(hr))return hr;
        D3D12_TEXTURE_BARRIER b=last->release_wire;
        b.LayoutAfter=D3D12_BARRIER_LAYOUT_COMMON;b.pResource=last->primary;
        D3D12_BARRIER_GROUP g={.Type=D3D12_BARRIER_TYPE_TEXTURE,.NumBarriers=1,.pTextureBarriers=&b};
        ID3D12GraphicsCommandList7_Barrier(w->suffix,1,&g);
        hr=ID3D12GraphicsCommandList7_Close(w->suffix);
        if(FAILED(hr))return hr;
        if(!TryAcquireSRWLockExclusive(&escape_lock))return E_ACCESSDENIED;
        hr=bridge_escaped?E_ACCESSDENIED:xs_pair_writer(first->pool_owner,first->primary_pin,
            first->primary,first->clear_slot,first->clear_serial,a->host.queue,&first->writer_epoch,TRUE);
        if(hr==S_OK)InterlockedIncrement(&pair_inflight);
        ReleaseSRWLockExclusive(&escape_lock);
        if(hr!=S_OK)return hr;
        w->host[1]=(ID3D12CommandList*)w->suffix;
    }else
#endif
    if (last->primary && FAILED(xs_primary_writer(last->pool_owner,last->primary,a->host.queue)))
        return E_ACCESSDENIED;
#endif
    for (UINT i=0;i<w->count;i++) {
        sl_addref(w->gen[i]->ordered_id);
        w->gen[i]->consumed=TRUE;
    }
#ifdef TEST_PAIR_SPLIT
    if(w->pair && w->gen[0]->head) {
        struct heap_retirement segments={.tag_tail=w->gen[0]->host};
        execute_recording(a->host.queue,w->gen[0],&segments);
        w->prefix_gate=segments.prefix_gate;w->post_gate=segments.gate;
        ID3D12CommandQueue_ExecuteCommandLists(a->host.queue,1,&w->host[1]);
    }else
#endif
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
#ifdef TEST_SAME_BATCH_RELEASE
        if (s->pool_owner!=a->display.owner) goto out;
        if(s->exports_rt){
            if(count!=2 || i!=0)goto out;
            w->pair=TRUE;
        }else if(s->requires_rt){
            if(!w->pair || count!=2 || i!=1)goto out;
            struct startup_list *p=w->gen[0];
            if(p->ordered_id!=s->requires_rt || p->primary_pin!=s->primary_pin ||
               p->primary!=s->primary || p->clear_slot!=s->clear_slot || p->clear_serial!=s->clear_serial)goto out;
        }else if(w->pair || (s->primary && i+1!=count))goto out;
#else
        if (s->pool_owner!=a->display.owner || (s->primary && i+1!=count)) goto out;
#endif
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
#ifdef TEST_SAME_BATCH_RELEASE
        if(w->suffix)ID3D12GraphicsCommandList7_Release(w->suffix);
#endif
        if (wait) CloseThreadpoolWait(wait);
        if (w->event) CloseHandle(w->event);
        if (w->end) ID3D12Fence_Release(w->end);
        HeapFree(GetProcessHeap(),0,w);
    }
    return hr;
}
