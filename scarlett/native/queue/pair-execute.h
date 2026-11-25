/* A reset creates a fresh native list. The registry is weak: the guest wrapper
 * owns one generation ref; a successful queue commit owns a separate ref. */
#include "pair-publication.h"
struct pair_gen {
    struct pair_gen *next;
    LONG refs;
    IUnknown *id,*pins[32];UINT npins;
    struct command_allocator *allocator;
    struct release_receipt *receipt;
    struct startup_list segments; /* Only the five fields consumed by execute_recording. */
    ID3D12GraphicsCommandList7 *pre;
    struct system_timestamp *stamp;
    struct xs_attachment_pin final_sr; /* identity already held in pins[] */
    struct pub_seal publication;
    struct lease_span spans[3];
    BOOL producer,used,allocator_unpinned;
};
static SRWLOCK pair_lock=SRWLOCK_INIT;
static struct pair_gen *pair_registry;

static void pair_pin(struct pair_gen *g,void *obj)
{
    if(!obj)return;
    if(g->npins==ARRAYSIZE(g->pins))xs_stop();
    g->pins[g->npins++]=obj;IUnknown_AddRef((IUnknown*)obj);
}
static void pair_unpin_allocator(struct pair_gen *g)
{
    AcquireSRWLockExclusive(&g->allocator->lock);
    if(!g->allocator_unpinned){g->allocator_unpinned=TRUE;g->allocator->submission_pins--;}
    ReleaseSRWLockExclusive(&g->allocator->lock);
}
static void pair_drop(struct pair_gen *g)
{
    if(InterlockedDecrement(&g->refs))return;
    pair_unpin_allocator(g);
    drop_marker(g->segments.post);drop_marker(g->segments.marker);
    drop_timestamp(g->stamp);drop_receipt(g->receipt);
    while(g->npins){IUnknown *p=g->pins[--g->npins];IUnknown_Release(p);}
    HeapFree(GetProcessHeap(),0,g);
}
static void pair_retired(struct pair_gen *g)
{
    pair_unpin_allocator(g);pair_drop(g);
}
static void pair_unseal(struct startup_list *s)
{
    struct pair_gen *g=s->seal;
    if(!g)return;
    AcquireSRWLockExclusive(&pair_lock);
    struct pair_gen **p=&pair_registry;
    while(*p!=g)p=&(*p)->next;
    *p=g->next;s->seal=NULL;
    ReleaseSRWLockExclusive(&pair_lock);
    pair_drop(g);
}

#include "pair-profile.h"

static void pair_seal(struct startup_list *s,IUnknown *id)
{
    pair_inventory(s,id);
    if(s->seal || !s->closed || s->error!=S_OK || s->host_exposed || !s->pool_owner ||
       !s->reservation || s->type!=D3D12_COMMAND_LIST_TYPE_DIRECT || !s->host ||
       !s->allocator || s->allocator->lpVtbl!=&at.prefix || !pair_profile(s))return;
    struct pair_gen *g=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof *g);if(!g)return;
    g->allocator=CONTAINING_RECORD(s->allocator,struct command_allocator,a.iface);
    BOOL producer=s->offered!=NULL;
    struct release_receipt *r=producer?s->offered:s->deferred;
    if(producer){
        const struct xs_clear_source *inputs[]={&s->source,&s->previous_vb,&s->vb};
        const UINT offsets[]={0x3ff80,0x3ef30,0x3df10},sizes[]={100,4080,4080},holds[]={2,3,3};
        for(UINT j=0;j<3;j++){
            struct lease_span v=inputs[j]->span;
            if(!s->source.capture || inputs[j]->capture!=s->source.capture || v.bytes!=sizes[j] ||
               v.va-v.arena.base!=offsets[j] || v.stamp!=~1ull || v.holds!=holds[j] ||
               v.node!=s->source.span.node || v.gen!=s->source.span.gen ||
               memcmp(&v.arena,&s->source.span.arena,sizeof v.arena))goto no_publication;
            g->spans[j]=v;
        }
        r->node=g->spans[0].node;r->node_gen=g->spans[0].gen;
    }
    AcquireSRWLockExclusive(&g->allocator->lock);
    if(s->alloc_generation!=(UINT64)g->allocator->generation ||
       (UINT_PTR)g->allocator->entry!=(UINT_PTR)GetModuleHandleW(NULL)+0x555df10 ||
       !pub_close((UINT_PTR)g->allocator->context,(UINT_PTR)id,producer,r->node,r->node_gen,&g->publication)){
        ReleaseSRWLockExclusive(&g->allocator->lock);goto no_publication;
    }
    g->allocator->submission_pins++;ReleaseSRWLockExclusive(&g->allocator->lock);
    g->refs=1;g->id=id;g->producer=s->offered!=NULL;
    g->receipt=g->producer?s->offered:s->deferred;InterlockedIncrement(&g->receipt->refs);
    struct startup_list *v=&g->segments;
    v->device=s->device;v->head=s->head;v->host=s->host;g->pre=s->pre_release;
    if(producer && s->offscreen_sr)g->final_sr=s->attachments[0];
    if((v->marker=s->marker))InterlockedIncrement(&v->marker->refs);
    if((v->post=s->post))InterlockedIncrement(&v->post->refs);
    if((g->stamp=s->stamp))InterlockedIncrement(&g->stamp->refs);
    void *pins[]={s->pool_owner,s->allocator,s->head,s->host,s->tail_allocator,s->pre_release,s->after_alloc,
        s->heaps[0],s->heaps[1],s->primary,s->primary_pin,s->output_heap,s->om.heap[0],s->om.heap[1],s->first_om.heap[0],s->first_om.heap[1],
        s->attachments[0].identity,s->attachments[1].identity,s->prior_pso,s->prior_root,s->graphics_identity,s->bound,
        s->source.placed,s->source.buffer,s->previous_vb.placed,s->previous_vb.buffer,s->vb.placed,s->vb.buffer};
    _Static_assert(ARRAYSIZE(pins)<=ARRAYSIZE(g->pins),"recording ownership");
    for(UINT i=0;i<ARRAYSIZE(pins);i++)pair_pin(g,pins[i]);
    AcquireSRWLockExclusive(&pair_lock);
    g->next=pair_registry;pair_registry=s->seal=g;
    ReleaseSRWLockExclusive(&pair_lock);
    return;
no_publication:
    fprintf(stderr,"PAIR refused close publication list=%p role=%u node=%zx gen=%u\n",id,producer,r->node,r->node_gen);
    HeapFree(GetProcessHeap(),0,g);
}
struct pair_job {
    struct adapter *queue;
    struct pair_gen *g[2];
    ID3D12Fence *end,*prefix,*post;
    ID3D12CommandAllocator *allocator;
    ID3D12GraphicsCommandList7 *release;
    struct monitored_fence *book_fence;
    UINT_PTR lists;
    HANDLE event;
};
static void pair_cleanup(struct pair_job *w)
{
    if(w->prefix)ID3D12Fence_Release(w->prefix);
    if(w->post)ID3D12Fence_Release(w->post);
    if(w->release)ID3D12GraphicsCommandList7_Release(w->release);
    if(w->allocator)ID3D12CommandAllocator_Release(w->allocator);
    if(w->end)ID3D12Fence_Release(w->end);
    if(w->event)CloseHandle(w->event);
    HeapFree(GetProcessHeap(),0,w);
}
static void CALLBACK retire_pair(PTP_CALLBACK_INSTANCE instance,void *ctx,PTP_WAIT wait,TP_WAIT_RESULT result)
{
    struct pair_job *w=ctx;(void)instance;(void)result;
    fprintf(stderr,"PAIR end=%p sampled=%llu\n",w->end,ID3D12Fence_GetCompletedValue(w->end));
    CloseThreadpoolWait(wait);
    AcquireSRWLockExclusive(&publisher_lock);w->book_fence->protected_pair=FALSE;ReleaseSRWLockExclusive(&publisher_lock);
    IUnknown_Release(&w->book_fence->a.iface);
    pair_retired(w->g[0]);pair_retired(w->g[1]);IUnknown_Release(&w->queue->iface);
    InterlockedDecrement(&pair_inflight);pair_cleanup(w);
}
/* Both publisher_lock and the Direct queue's submit_lock are held. The latter
 * orders any later signal on this queue after the private batch end. */
static struct monitored_fence *pair_book(struct pair_job *w)
{
    const struct pair_gen *p=w->g[0],*s=w->g[1];
    UINT_PTR exe=(UINT_PTR)GetModuleHandleW(NULL),book=0,b[7],outer[3];
    if(!pub_pair(&p->publication,&s->publication,w->lists,3) || w->lists<0x100 ||
       !lease_read(w->lists-0x100,outer,sizeof outer) || !lease_read(exe+0x68929d0,&book,sizeof book) ||
       book!=exe+0x75e0e98 || !lease_read(book,b,sizeof b) || (BYTE)b[1] || b[0]!=(UINT_PTR)&w->queue->iface ||
       outer[0]!=book+0x28 || !b[2] || b[2]!=outer[1] || outer[2]!=3 || b[3]>=3 || b[5]!=2 ||
       b[6]<3)return NULL;
    for(UINT j=0;j<3;j++){
        const struct lease_span *v=p->spans+j;struct lease_span live;
        if(!lease_span_read(v->arena.base,v->va,v->bytes,&live) || memcmp(&v->arena,&live.arena,sizeof v->arena) ||
           live.node!=p->publication.node || live.gen!=p->publication.gen || live.holds || live.stamp!=3)return NULL;
    }
    for(struct monitored_fence *f=live_fences;f;f=f->next){
        if((UINT_PTR)&f->a.iface!=b[2])continue;
        if(f->mixed || f->protected_pair || f->high!=2 || f->publisher!=w->queue->host.queue ||
           f->device!=p->segments.device)return NULL;
        UINT64 completed=fence_completed(&f->a.iface);
        if(completed>=3 || completed<b[3])return NULL;
        fprintf(stderr,"PAIR lease node=%zx gen=%u fence=%p prior=%llu sampled=%llu cached=%zu submitted=%zu pending=3\n",
            p->publication.node,p->publication.gen,&f->a.iface,f->high,completed,b[3],b[5]);
        return f;
    }
    return NULL;
}
static HRESULT pair_execute(void *ctx)
{
    struct pair_job *w=ctx;struct adapter *a=w->queue;
    struct pair_gen *p=w->g[0],*s=w->g[1];struct release_receipt *r=p->receipt;
    ID3D12CommandQueue *q=a->host.queue;
    if(!TryAcquireSRWLockExclusive(&escape_lock))return E_ACCESSDENIED;
    HRESULT hr=E_ACCESSDENIED;
    AcquireSRWLockExclusive(&publisher_lock);
    struct monitored_fence *f=bridge_escaped?NULL:pair_book(w);
    if(f && xs_sr_submitted(r->owner,&p->final_sr,q,NULL))hr=xs_pair_writer(r->owner,r->key,r->host,r->slot,r->serial,q,&r->writer_epoch,TRUE);
    if(hr==S_OK){
        f->protected_pair=TRUE;w->book_fence=f;IUnknown_AddRef(&f->a.iface);InterlockedIncrement(&pair_inflight);
    }
    ReleaseSRWLockExclusive(&publisher_lock);
    ReleaseSRWLockExclusive(&escape_lock);
    if(hr!=S_OK)return hr;
    IUnknown_AddRef(&a->iface);InterlockedIncrement(&p->refs);InterlockedIncrement(&s->refs);
    p->used=s->used=TRUE;
    struct heap_retirement segments={.tag_tail=p->segments.host};
    execute_recording(q,&p->segments,&segments);
    w->prefix=segments.prefix_gate;w->post=segments.gate;
    ID3D12CommandList *ordered[]={(ID3D12CommandList*)s->pre,(ID3D12CommandList*)w->release,
        (ID3D12CommandList*)s->segments.host};
    ID3D12CommandQueue_ExecuteCommandLists(q,ARRAYSIZE(ordered),ordered);
    if(FAILED(ID3D12CommandQueue_Signal(q,w->end,1)))xs_stop();
    if(!xs_sr_submitted(r->owner,&p->final_sr,q,w->end))xs_stop();
    forget_release(a);
    if(a->display.captured.hwnd){
        a->display.released=r->host;ID3D12Resource_AddRef(r->host);
        a->display.released_end=w->end;ID3D12Fence_AddRef(w->end);
        a->display.released_token=r->token;a->display.clear_slot=r->slot;a->display.clear_serial=r->serial;
    }
    fprintf(stderr,"PAIR submitted producer=%p successor=%p receipt=%p key=%p serial=%llu end=%p\n",
            p->id,s->id,r,r->key,r->serial,w->end);
    return S_OK;
}
static HRESULT submit_pair(IUnknown *iface,IUnknown *const *lists)
{
    IUnknown *ids[2];SIZE_T got;
    if(!as_queue(iface) || !ReadProcessMemory(GetCurrentProcess(),lists,ids,sizeof ids,&got) || got!=sizeof ids ||
       !ids[0] || !ids[1] || ids[0]==ids[1])return E_INVALIDARG;
    struct adapter *a=CONTAINING_RECORD(iface,struct adapter,iface);
    struct pair_job *w=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof *w);if(!w)return E_OUTOFMEMORY;
    w->queue=a;w->lists=(UINT_PTR)lists;HRESULT hr=E_ACCESSDENIED;PTP_WAIT waiter=NULL;
    AcquireSRWLockExclusive(&a->submit_lock);AcquireSRWLockExclusive(&pair_lock);
    if(a->external_submissions)goto out;
    for(struct pair_gen *g=pair_registry;g;g=g->next)
        for(UINT j=0;j<2;j++)if(g->id==ids[j])w->g[j]=g;
    struct pair_gen *p=w->g[0],*s=w->g[1];
    if(!p || !s || !p->producer || s->producer || p->used || s->used || p->receipt!=s->receipt ||
       p->allocator!=s->allocator || p->segments.device!=s->segments.device)goto out;
    struct release_receipt *r=p->receipt;
    if(!r->claimed || r->producer!=ids[0] || r->token!=2 || r->queue!=a->host.queue || r->owner!=a->display.owner)goto out;
    ID3D12Device *d=p->segments.device;
    if(FAILED(hr=ID3D12Device_CreateCommandAllocator(d,D3D12_COMMAND_LIST_TYPE_DIRECT,&IID_ID3D12CommandAllocator,(void**)&w->allocator)) ||
       FAILED(hr=ID3D12Device_CreateCommandList(d,1,D3D12_COMMAND_LIST_TYPE_DIRECT,w->allocator,NULL,&IID_ID3D12GraphicsCommandList7,(void**)&w->release)))goto out;
    D3D12_TEXTURE_BARRIER b={.SyncBefore=D3D12_BARRIER_SYNC_RENDER_TARGET,.SyncAfter=D3D12_BARRIER_SYNC_NONE,
        .AccessBefore=D3D12_BARRIER_ACCESS_RENDER_TARGET,.AccessAfter=D3D12_BARRIER_ACCESS_NO_ACCESS,
        .LayoutBefore=D3D12_BARRIER_LAYOUT_RENDER_TARGET,.LayoutAfter=D3D12_BARRIER_LAYOUT_COMMON,
        .pResource=r->host,.Subresources={.IndexOrFirstMipLevel=UINT_MAX}};
    D3D12_BARRIER_GROUP bg={.Type=D3D12_BARRIER_TYPE_TEXTURE,.NumBarriers=1,.pTextureBarriers=&b};
    ID3D12GraphicsCommandList7_Barrier(w->release,1,&bg);
    if(FAILED(hr=ID3D12GraphicsCommandList7_Close(w->release)) ||
       FAILED(hr=ID3D12Device_CreateFence(d,0,0,&IID_ID3D12Fence,(void**)&w->end)))goto out;
    if(!(w->event=CreateEventW(NULL,FALSE,FALSE,NULL)) || !(waiter=CreateThreadpoolWait(retire_pair,w,NULL))){
        hr=HRESULT_FROM_WIN32(GetLastError());goto out;
    }
    hr=xs_pair_frame_submit(r->owner,2,r->queue,TRUE,w->end,pair_execute,w,&r->epoch);
    if(hr==S_OK){
        if(FAILED(ID3D12Fence_SetEventOnCompletion(w->end,1,w->event)))xs_stop();
        SetThreadpoolWait(waiter,w->event,NULL);
    }
out:
    ReleaseSRWLockExclusive(&pair_lock);ReleaseSRWLockExclusive(&a->submit_lock);
    if(hr!=S_OK){if(waiter)CloseThreadpoolWait(waiter);pair_cleanup(w);}
    return hr;
}
