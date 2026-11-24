/* Opt-in ordinary queue order for the two observed token-bearing singleton calls. */
static BOOL tagged_pool_ready(struct startup_list *s)
{
    struct prefix_marker *m=s->marker,*p=s->post;
    ID3D12DescriptorHeap *banks[2];
    if (s->phase!=B_CLOSED || !s->reservation || !s->head || s->head==s->host || !s->primary ||
        !s->primary_released || !s->primary_cleared || !s->heaps[0] || !s->heaps[1] ||
        !allocator_matches(s->tail_allocator,s->device) || FAILED(xs_initial_heap_banks(s->pool_owner,s->heaps,banks)) ||
        !m || !p || p->parent!=m || !p->counter || p->threshold!=1 || s->post_boundary ||
        !s->stamp || !s->stamp->tail_recorded || !s->stamp->section || !s->stamp->query || !s->stamp->dst) return FALSE;
    if (m->buffer)
        return m->control_section && !m->alias && m->native_post==0x29a5ee15 && !p->word && s->stamp->retained;
    return m->alias && !m->value && p->word==m->word+1 && p->value==0x29a5ee15 &&
        p->counter==m->alias && s->stamp->mapping;
}
struct tagged_submission {ID3D12CommandQueue *q;IUnknown *iface;struct startup_list *s;struct heap_retirement *r;};
static HRESULT execute_tagged(void *ctx)
{
    struct tagged_submission *t=ctx;
    struct startup_list *s=t->s;
    struct heap_retirement *r=t->r;
#ifdef SCARLETT_EXPERIMENTAL_BRIDGE
    if(s->primary && FAILED(xs_primary_writer(s->pool_owner,s->primary,t->q)))return E_ACCESSDENIED;
#endif
    retain_recording(r,s);
    r->tag_queue=t->iface;IUnknown_AddRef(r->tag_queue);
    if (!r->head_alloc) {r->head_alloc=s->allocator;IUnknown_AddRef(r->head_alloc);}
    CONTAINING_RECORD(s->allocator,struct command_allocator,a.iface)->submission_pins++;
    if ((r->tag_post=s->post)) InterlockedIncrement(&r->tag_post->refs);
    if ((r->tag_head=s->head)) ID3D12GraphicsCommandList7_AddRef(r->tag_head);
    r->tag_tail=s->host;ID3D12GraphicsCommandList7_AddRef(r->tag_tail);
    execute_recording(t->q,s,r);return S_OK;
}
static HRESULT checked_tagged_submit(IUnknown *iface,UINT count,IUnknown *const *lists,UINT64 token,UINT flags)
{
    ID3D12CommandQueue *q=as_queue(iface);
    if (!q || !token || count!=1 || (flags!=0 && flags!=3)) return E_NOTIMPL;
    IUnknown *list;const void *vt;SIZE_T n;
    if (!ReadProcessMemory(GetCurrentProcess(),lists,&list,sizeof list,&n) || n!=sizeof list ||
        !ReadProcessMemory(GetCurrentProcess(),list,&vt,sizeof vt,&n) || n!=sizeof vt ||
        vt!=(flags?&pool_vtbl.prefix:&sl_vtbl.prefix)) return E_INVALIDARG;
    struct adapter *queue=CONTAINING_RECORD(iface,struct adapter,iface);
    struct startup_list *s=sl(list);
#ifdef XS_TEST_ORDERED_QUEUE
    if (s->single_use) return E_ACCESSDENIED;
#endif
    if (!s->closed || s->error!=S_OK) return E_INVALIDARG;
    if (s->table0.heap || s->prior_pso || s->pending_470) return E_ACCESSDENIED;
    D3D12_COMMAND_QUEUE_DESC desc;q->lpVtbl->GetDesc(q,&desc);
    IUnknown *owner=flags?s->pool_owner:s->sync_owner;
    if (!owner || owner!=queue->display.owner || s->host_exposed ||
        desc.Type!=D3D12_COMMAND_LIST_TYPE_DIRECT || s->type!=desc.Type ||
        !allocator_matches(xs_allocator_host(s->allocator,s->type),s->device)) return E_INVALIDARG;
    if (flags ? !tagged_pool_ready(s) :
        (s->reservation || s->primary || s->head || s->stamp || s->marker || s->post || s->heaps[0] || s->heaps[1])) return E_INVALIDARG;
    ID3D12Device *device;
    HRESULT hr=ID3D12CommandQueue_GetDevice(q,&IID_ID3D12Device,(void**)&device);
    if (FAILED(hr)) return hr;
    BOOL same=device==s->device;ID3D12Device_Release(device);
    if (!same) return E_INVALIDARG;
    struct heap_retirement *r=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof *r);
    if (!r) return E_OUTOFMEMORY;
    PTP_WAIT wait=NULL;
    hr=ID3D12Device_CreateFence(s->device,0,0,&IID_ID3D12Fence,(void**)&r->fence);
    if (FAILED(hr)) goto reject;
    if (!(r->event=CreateEventW(NULL,FALSE,FALSE,NULL)) || !(wait=CreateThreadpoolWait(retire_heaps,r,NULL))) {
        hr=HRESULT_FROM_WIN32(GetLastError());goto reject;
    }
    struct command_allocator *a=CONTAINING_RECORD(s->allocator,struct command_allocator,a.iface);
    struct tagged_submission t={q,iface,s,r};
    AcquireSRWLockExclusive(&queue->submit_lock);
    AcquireSRWLockExclusive(&a->lock);
    hr=queue->external_submissions?E_ACCESSDENIED:xs_frame_submit(owner,token,q,flags==3,r->fence,execute_tagged,&t);
#ifdef SCARLETT_EXPERIMENTAL_BRIDGE
    if (hr==S_OK) {
        forget_release(queue);
        if (flags==3 && queue->display.captured.hwnd) {
            queue->display.released=s->primary;ID3D12Resource_AddRef(s->primary);
            queue->display.released_end=r->fence;ID3D12Fence_AddRef(r->fence);
            queue->display.released_token=token;queue->display.clear_slot=s->clear_slot;queue->display.clear_serial=s->clear_serial;
        }
    }
#endif
    ReleaseSRWLockExclusive(&a->lock);
    ReleaseSRWLockExclusive(&queue->submit_lock);
    if (hr==S_OK) {
        if (FAILED(ID3D12Fence_SetEventOnCompletion(r->fence,1,r->event))) xs_stop();
        SetThreadpoolWait(wait,r->event,NULL);
        return S_OK;
    }
reject:
    if (wait) CloseThreadpoolWait(wait);
    if (r->event) CloseHandle(r->event);
    if (r->fence) ID3D12Fence_Release(r->fence);
    HeapFree(GetProcessHeap(),0,r);return hr;
}
