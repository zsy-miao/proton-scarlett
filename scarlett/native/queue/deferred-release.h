/* Only metadata crosses the close/bind boundary. A checked ordered batch must
 * independently retain and match both recording generations before execution. */
struct release_receipt {
    LONG refs;BOOL claimed;
    struct release_receipt *next;
    IUnknown *producer,*owner,*key;
    ID3D12Resource *host; /* key owns host; key also owns owner/device */
    ID3D12CommandQueue *queue;
    SIZE_T slot;UINT64 serial,table_epoch,token;
    UINT epoch;DWORD tid;
#ifdef SCARLETT_PAIR_SUBMIT
    struct system_timestamp *producer_stamp;
    UINT64 writer_epoch;
    UINT_PTR node;
    UINT node_gen;
#endif
};
static SRWLOCK receipt_lock=SRWLOCK_INIT;
static struct release_receipt *offers;
static void drop_receipt(struct release_receipt *r)
{
    if(!InterlockedDecrement(&r->refs)){
#ifdef SCARLETT_PAIR_SUBMIT
        drop_timestamp(r->producer_stamp);
#endif
        ID3D12CommandQueue_Release(r->queue);IUnknown_Release(r->key);HeapFree(GetProcessHeap(),0,r);
    }
}
static void forget_receipt(struct startup_list *s)
{
    if(s->offered){
        AcquireSRWLockExclusive(&receipt_lock);
        struct release_receipt **p=&offers;while(*p!=s->offered)p=&(*p)->next;
        *p=s->offered->next;
        ReleaseSRWLockExclusive(&receipt_lock);
        drop_receipt(s->offered);s->offered=NULL;
    }
    if(s->deferred){drop_receipt(s->deferred);s->deferred=NULL;memset(&s->deferred_wire,0,sizeof s->deferred_wire);}
}
static void offer_release(struct startup_list *s,IUnknown *id)
{
    if(!s->closed || s->error!=S_OK || s->host_exposed || !s->clear4_drew || !s->previous_vb.buffer ||
       !s->pending_470 || !s->prior_pso || !s->offscreen_sr || !s->output_heap ||
       !s->primary_pin || s->primary_key!=(UINT_PTR)s->primary_pin || !s->primary || s->primary_released ||
       s->table0.cells!=3 || s->table0.slot!=368373 || s->prior_b27_slot!=368504 ||
       s->table0.heap!=s->heaps[0] || s->table0.root!=s->graphics_identity || s->pipeline || s->bound)return;
    UINT64 token;UINT epoch;ID3D12CommandQueue *q=xs_release_clock(s->pool_owner,&token,&epoch);
    if(!q)return;
    struct release_receipt *r=NULL;
    if(xs_live_output(s->pool_owner,s->primary_pin,s->primary,s->output_slot,s->output_serial))
        r=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof *r);
    if(!r){ID3D12CommandQueue_Release(q);return;}
    *r=(struct release_receipt){.refs=1,.producer=id,.owner=s->pool_owner,.key=s->primary_pin,
        .host=s->primary,.slot=s->output_slot,.serial=s->output_serial,.table_epoch=s->table_epoch,
        .queue=q,.token=token,.epoch=epoch,.tid=GetCurrentThreadId()};
    IUnknown_AddRef(r->key);
#ifdef SCARLETT_PAIR_SUBMIT
    if(s->stamp && (s->stamp->successor_bank || (s->stamp->second_bank && s->stamp->cpu_section))){
        r->producer_stamp=s->stamp;InterlockedIncrement(&s->stamp->refs);
    }
    if(FAILED(xs_pair_writer(r->owner,r->key,r->host,r->slot,r->serial,r->queue,&r->writer_epoch,FALSE))){
        drop_receipt(r);return;
    }
#endif
    AcquireSRWLockExclusive(&receipt_lock);
    r->next=offers;offers=s->offered=r;
    ReleaseSRWLockExclusive(&receipt_lock);
}
static HRESULT defer_release(struct startup_list *s,const D3D12_TEXTURE_BARRIER *b)
{
    if(s->phase!=B_BOUND || !s->heaps[0] || !s->heaps[1] || s->marker || s->post || s->head ||
       s->stamp || s->bound || s->graphics_identity || s->attachments[0].identity ||
       s->primary || s->primary_pin || s->primary_released || s->primary_cleared || s->table0.cells ||
       s->pending_470 || s->depth_zero_pending || !b->pResource ||
       b->SyncBefore!=D3D12_BARRIER_SYNC_RENDER_TARGET || b->SyncAfter!=D3D12_BARRIER_SYNC_NONE ||
       b->AccessBefore!=D3D12_BARRIER_ACCESS_RENDER_TARGET || b->AccessAfter!=D3D12_BARRIER_ACCESS_NO_ACCESS ||
       b->LayoutBefore!=D3D12_BARRIER_LAYOUT_RENDER_TARGET || b->LayoutAfter!=(D3D12_BARRIER_LAYOUT)0x1f)return E_NOTIMPL;
    UINT epoch;UINT64 token;ID3D12CommandQueue *q=xs_release_clock(s->pool_owner,&token,&epoch);
    if(!q)return E_ACCESSDENIED;
    HRESULT hr=E_ACCESSDENIED;
    AcquireSRWLockExclusive(&s->borrow_lock);
    AcquireSRWLockExclusive(&receipt_lock);
    struct release_receipt *match=NULL;
    for(struct release_receipt *r=offers;r;r=r->next){
        if(r->owner!=s->pool_owner || r->queue!=q || r->epoch!=epoch || r->token!=token || r->tid!=GetCurrentThreadId())continue;
        if(match){match=NULL;break;}match=r;
    }
    if(!s->host_exposed && !s->deferred && match && !match->claimed && match->key==(IUnknown*)b->pResource &&
       xs_live_output(match->owner,match->key,match->host,match->slot,match->serial)){
#ifdef SCARLETT_PAIR_SUBMIT
        /* Close exactly the bind-only prefix. Following commands are recorded
         * normally on a separate native list; the release goes between them. */
        ID3D12DescriptorHeap *banks[2];
        ID3D12CommandAllocator *alloc=NULL;
        ID3D12GraphicsCommandList7 *tail=NULL;
        hr=xs_initial_heap_banks(s->pool_owner,s->heaps,banks);
        if(SUCCEEDED(hr))hr=ID3D12Device_CreateCommandAllocator(s->device,s->type,&IID_ID3D12CommandAllocator,(void**)&alloc);
        if(SUCCEEDED(hr))hr=ID3D12Device_CreateCommandList(s->device,1,s->type,alloc,NULL,&IID_ID3D12GraphicsCommandList7,(void**)&tail);
        if(SUCCEEDED(hr)){
            ID3D12GraphicsCommandList7_SetDescriptorHeaps(tail,2,banks);
            hr=ID3D12GraphicsCommandList7_Close(s->host);
            if(FAILED(hr))s->error=hr;
        }
        if(FAILED(hr)){
            if(tail)ID3D12GraphicsCommandList7_Release(tail);
            if(alloc)ID3D12CommandAllocator_Release(alloc);
        }else{
        s->pre_release=s->host;s->after_alloc=alloc;s->host=tail;
#endif
        match->claimed=TRUE;InterlockedIncrement(&match->refs);s->deferred=match;
        s->deferred_wire=*b;s->phase=B_INELIGIBLE;hr=S_OK;
#ifdef SCARLETT_PAIR_SUBMIT
        }
#endif
    }
    ReleaseSRWLockExclusive(&receipt_lock);
    ReleaseSRWLockExclusive(&s->borrow_lock);ID3D12CommandQueue_Release(q);return hr;
}
