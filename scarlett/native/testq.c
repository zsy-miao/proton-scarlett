#ifdef TEST_ORDERED_BATCH
#define XS_TEST_ORDERED_QUEUE
#endif
#include "queue/queue.c"
#ifdef TEST_PRESENT_BRIDGE
HANDLE test_pending_present;
ULONG_PTR test_generation(IUnknown *q){return CONTAINING_RECORD(q,struct adapter,iface)->display.captured.generation;}
HRESULT test_bridge_count(IUnknown *q,UINT *count)
{
    struct adapter *a=CONTAINING_RECORD(q,struct adapter,iface);
    if (!a->display.wsi || a->display.wsi==a->host.queue || !bridge_window(a)) return E_INVALIDARG;
    return IDXGISwapChain3_GetLastPresentCount(a->display.chain,count);
}
#endif
ID3D12CommandQueue *test_host_queue(IUnknown *q){return as_queue(q);}
#ifdef TEST_ORDERED_BATCH
#include "test-ordered-queue.h"
#ifdef TEST_SAME_BATCH_RELEASE
/* Real intervening writer through the ordinary tracked submit path. Restore
 * the fixture's UAV input layout so a newly recorded pair can retry. */
HRESULT test_intervening_write(IUnknown *iface,IUnknown *key,SIZE_T slot)
{
    struct adapter *a=CONTAINING_RECORD(iface,struct adapter,iface);
    ID3D12Device *d=NULL;IUnknown *alloc=NULL,*list=NULL;ID3D12Fence *done=NULL;
    HRESULT hr=ID3D12CommandQueue_GetDevice(a->host.queue,&IID_ID3D12Device,(void**)&d);
    if(FAILED(hr))return hr;
    if(FAILED(hr=xs_allocator(d,D3D12_COMMAND_LIST_TYPE_DIRECT,&alloc)) ||
       FAILED(hr=xs_device_list(d,D3D12_COMMAND_LIST_TYPE_DIRECT,alloc,TRUE,a->display.owner,&list)) ||
       FAILED(hr=ID3D12Device_CreateFence(d,0,0,&IID_ID3D12Fence,(void**)&done)))goto out;
    struct startup_list *s=sl(list);
    D3D12_TEXTURE_BARRIER b={D3D12_BARRIER_SYNC_ALL_SHADING,D3D12_BARRIER_SYNC_RENDER_TARGET,
        D3D12_BARRIER_ACCESS_UNORDERED_ACCESS,D3D12_BARRIER_ACCESS_RENDER_TARGET,
        D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS,D3D12_BARRIER_LAYOUT_RENDER_TARGET,
        (ID3D12Resource*)key,{.IndexOrFirstMipLevel=UINT_MAX},D3D12_TEXTURE_BARRIER_FLAG_NONE};
    D3D12_BARRIER_GROUP g={.Type=D3D12_BARRIER_TYPE_TEXTURE,.NumBarriers=1,.pTextureBarriers=&b};
    if(FAILED(hr=record_primary_barrier(s,1,&g)) ||
       FAILED(hr=record_primary_clear(s,slot,(FLOAT[4]){0},0,NULL)))goto out;
    b.SyncBefore=D3D12_BARRIER_SYNC_RENDER_TARGET;b.SyncAfter=D3D12_BARRIER_SYNC_ALL_SHADING;
    b.AccessBefore=D3D12_BARRIER_ACCESS_RENDER_TARGET;b.AccessAfter=D3D12_BARRIER_ACCESS_UNORDERED_ACCESS;
    b.LayoutBefore=D3D12_BARRIER_LAYOUT_RENDER_TARGET;b.LayoutAfter=D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS;
    b.pResource=s->primary;ID3D12GraphicsCommandList7_Barrier(s->host,1,&g);
    if(FAILED(hr=sl_close(list)) || FAILED(hr=checked_startup_submit(iface,1,&list,0,0)))goto out;
    if(FAILED(ID3D12CommandQueue_Signal(a->host.queue,done,1)) ||
       FAILED(ID3D12Fence_SetEventOnCompletion(done,1,NULL)))xs_stop();
out:
    if(done)ID3D12Fence_Release(done);
    if(list)IUnknown_Release(list);
    if(alloc)IUnknown_Release(alloc);
    ID3D12Device_Release(d);return hr;
}
#endif
#ifdef TEST_ORDERED_PRESENT
/* Observe the real private chain without touching any frame-clock record. */
HRESULT test_displayed_count(IUnknown *q,UINT *count)
{
    struct adapter *a=CONTAINING_RECORD(q,struct adapter,iface);
    if (!a->display.wsi || !bridge_window(a)) return E_INVALIDARG;
    DXGI_FRAME_STATISTICS stats;
    HRESULT hr=IDXGISwapChain3_GetFrameStatistics(a->display.chain,&stats);
    if(hr==S_OK)*count=stats.PresentCount;
    return hr;
}
UINT test_ring_index(IUnknown *q)
{
    return IDXGISwapChain3_GetCurrentBackBufferIndex(CONTAINING_RECORD(q,struct adapter,iface)->display.chain);
}
/* Inject a completed but unrelated release fence; leave the clock record intact. */
HRESULT test_replace_end(IUnknown *q,ID3D12Fence *expected,ID3D12Fence *replacement)
{
    struct adapter *a=CONTAINING_RECORD(q,struct adapter,iface);
    AcquireSRWLockExclusive(&a->submit_lock);
    HRESULT hr=E_INVALIDARG;
    if (a->display.released_end==expected && a->display.released_token==2) {
        ID3D12Fence_AddRef(replacement);a->display.released_end=replacement;ID3D12Fence_Release(expected);hr=S_OK;
    }
    ReleaseSRWLockExclusive(&a->submit_lock);return hr;
}
#endif
UINT test_ordered_host_submissions(void){return ordered_host_submissions;}
HRESULT test_ordered_ineligible(IUnknown *list,void *context)
{
    /* These faults occur independently; no flag is cleared to qualify a generation. */
    switch (*(UINT *)context) {
    case 0: sl(list)->error=E_FAIL;break;
    case 1: sl(list)->pending_470=TRUE;break;
    case 2: if (!xs_list_host(list)) return E_FAIL;break;
    case 3: sl(list)->cache_page[0]^=1;break;
    }
    return S_OK;
}
#endif
#ifdef SCARLETT_EXPERIMENTAL_TAGGED_BATCH
HRESULT test_tagged(IUnknown *q,UINT n,IUnknown *const *lists,UINT64 token,UINT flags)
{return checked_tagged_submit(q,n,lists,token,flags);}
HRESULT test_incomplete_batch(IUnknown *q,IUnknown *list,UINT64 token,BOOL stamp)
{
    struct startup_list *s=sl(list);enum batch_phase saved=s->phase;BOOL paired=s->stamp->tail_recorded;
    if (stamp) s->stamp->tail_recorded=FALSE; else s->phase=B_INELIGIBLE;
    HRESULT hr=checked_tagged_submit(q,1,&list,token,3);
    s->phase=saved;s->stamp->tail_recorded=paired;return hr;
}
#endif
/* Test-only samples inside the native recording, without exposing or splitting it. */
HRESULT test_post_try(IUnknown *i,UINT64 addr,UINT value,UINT64 c,UINT flags){return record_native_post(sl(i),addr,value,c,flags);}
void test_post_snapshot(IUnknown *i,ID3D12Resource *rb,UINT dst,UINT stamp_offset)
{
    struct startup_list *s=sl(i);
    ID3D12Resource *r[2]={s->stamp->dst,s->marker->buffer};D3D12_RESOURCE_BARRIER b[2]={0};
    for(UINT j=0;j<2;j++) b[j].Transition=(D3D12_RESOURCE_TRANSITION_BARRIER){r[j],0,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COPY_SOURCE};
    ID3D12GraphicsCommandList7_ResourceBarrier(s->host,2,b);
    ID3D12GraphicsCommandList7_CopyBufferRegion(s->host,rb,dst,r[0],stamp_offset,8);
    ID3D12GraphicsCommandList7_CopyBufferRegion(s->host,rb,dst+8,r[1],8,8);
    for(UINT j=0;j<2;j++){b[j].Transition.StateBefore=D3D12_RESOURCE_STATE_COPY_SOURCE;b[j].Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_DEST;}
    ID3D12GraphicsCommandList7_ResourceBarrier(s->host,2,b);
}

HRESULT test_primary_try(IUnknown *i,UINT n,const D3D12_BARRIER_GROUP *g){return record_primary_barrier(sl(i),n,g);}

ID3D12GraphicsCommandList7 *test_clear_recorder(IUnknown *i){return sl(i)->host;}
#ifdef TEST_TAIL_TIMESTAMP
HRESULT test_stamp_try(IUnknown *i,UINT64 addr,UINT64 y){return pool_timestamp(sl(i),addr,0,y,1);}
void test_copy_ticks(IUnknown *i,ID3D12Resource *read,UINT64 offset)
{
    struct startup_list *s=sl(i);
    ID3D12Resource *src=s->stamp->dst;
    D3D12_RESOURCE_BARRIER b={.Transition={src,0,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COPY_SOURCE}};
    ID3D12GraphicsCommandList7_ResourceBarrier(s->host,1,&b);
    ID3D12GraphicsCommandList7_CopyBufferRegion(s->host,read,offset,src,s->stamp->tail_offset%65536,16);
    b.Transition.StateBefore=D3D12_RESOURCE_STATE_COPY_SOURCE;b.Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_DEST;
    ID3D12GraphicsCommandList7_ResourceBarrier(s->host,1,&b);
}
#endif
HRESULT test_clear_try(IUnknown *i,SIZE_T slot,const FLOAT *c,UINT n,const RECT *r){return record_primary_clear(sl(i),slot,c,n,r);}
