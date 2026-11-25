#ifdef TEST_ORDERED_BATCH
#define XS_TEST_ORDERED_QUEUE
#endif
#include "queue/queue.c"
#ifdef TEST_REACQUIRE
HRESULT xs_write_view(IUnknown *,UINT,IUnknown *,const void *,SIZE_T);
HRESULT test_reacquire(IUnknown *q,IUnknown *source,IUnknown **keys,SIZE_T slot,volatile UINT *ctrl,BOOL presented)
{
    struct startup_list *old=sl(source);IUnknown *owner=old->pool_owner;ID3D12Device *d=old->device;
    UINT64 epoch=0;
    if(!presented)return FAILED(xs_reuse_common(owner,keys[0],old->primary,NULL,&epoch))?S_OK:E_FAIL;
    if(!xs_is_rtx3070(d))return E_NOTIMPL;
    IUnknown *a,*l,*foreign,*done;
#define RCK(e) do{if(!(e)){fprintf(stderr,"FAIL primary reacquire line=%d: %s\n",__LINE__,#e);return E_FAIL;}}while(0)
    RCK(SUCCEEDED(xs_allocator(d,0,&a)) && SUCCEEDED(xs_device_list(d,0,a,TRUE,owner,&l)) &&
        SUCCEEDED(xs_direct_queue(d,&foreign)) && SUCCEEDED(xs_shared_fence(d,&done)));
    struct startup_list *s=sl(l);
    UINT64 base=(UINT_PTR)ctrl,ref=3;
    RCK(SUCCEEDED(xs_bind_pool_heaps(l,2,old->heaps)));pool_vtbl.marker(l,base+0x98,2,0,0);
    RCK(SUCCEEDED(xs_bind_pool_heaps(l,2,old->heaps)));pool_vtbl.timestamp(l,old->stamp->tail_addr+8,0,0,1);
    pool_vtbl.marker(l,base+0x9c,0x29a5ee15,0,0);
    RCK(SUCCEEDED(wait64_at(s,base,7,ref,1,0x2484b12)));pair_op(s,0x490);
    D3D12_TEXTURE_BARRIER b={.SyncBefore=D3D12_BARRIER_SYNC_NONE,.SyncAfter=D3D12_BARRIER_SYNC_RENDER_TARGET,
        .AccessBefore=D3D12_BARRIER_ACCESS_NO_ACCESS,.AccessAfter=D3D12_BARRIER_ACCESS_RENDER_TARGET,
        .LayoutBefore=(D3D12_BARRIER_LAYOUT)31,.LayoutAfter=D3D12_BARRIER_LAYOUT_RENDER_TARGET,.Subresources={UINT_MAX,0,0,0,0,0}};
    D3D12_BARRIER_GROUP g={.Type=D3D12_BARRIER_TYPE_TEXTURE,.NumBarriers=1,.pTextureBarriers=&b};
#define TRY() record_primary_barrier_at(s,1,&g,0x32113)
    b.pResource=(ID3D12Resource*)keys[1];RCK(FAILED(TRY()));
    b.pResource=old->primary;RCK(FAILED(TRY()));b.pResource=(ID3D12Resource*)keys[0];
    b.AccessBefore=0;RCK(FAILED(TRY()));b.AccessBefore=D3D12_BARRIER_ACCESS_NO_ACCESS;
    b.Subresources.NumPlanes=1;RCK(FAILED(TRY()));b.Subresources.NumPlanes=0;
    RCK(FAILED(record_primary_barrier_at(s,1,&g,0x32114)) && !s->primary);
    DWORD view[]={28,4,0,0,0,0};SIZE_T alias=slot+32;
    RCK(SUCCEEDED(xs_write_view(owner,0xa0,keys[0],view,alias)) && FAILED(TRY()));
    RCK(SUCCEEDED(xs_write_view(owner,0xa0,keys[1],view,alias)));
    (*(UINT64*)slot)++;RCK(FAILED(TRY()));(*(UINT64*)slot)--;
    RCK(SUCCEEDED(TRY()) && s->primary==old->primary && s->primary_pin==keys[0] && s->reuse_epoch);
    batch_step(s,B_ACQUIRED);
    FLOAT clear[4]={0};pool_vtbl.clear(l,slot,clear,0,NULL);
    ID3D12Resource *rb;UINT *pixel=NULL;
    D3D12_HEAP_PROPERTIES hp={.Type=D3D12_HEAP_TYPE_READBACK,.CreationNodeMask=1,.VisibleNodeMask=1};
    D3D12_RESOURCE_DESC rd={.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER,.Width=512,.Height=1,
        .DepthOrArraySize=1,.MipLevels=1,.SampleDesc.Count=1,.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR};
    RCK(SUCCEEDED(ID3D12Device_CreateCommittedResource(d,&hp,0,&rd,D3D12_RESOURCE_STATE_COPY_DEST,NULL,&IID_ID3D12Resource,(void**)&rb)) &&
        SUCCEEDED(ID3D12Resource_Map(rb,0,NULL,(void**)&pixel)));*pixel=0xdeadbeef;
    D3D12_RESOURCE_BARRIER copy={.Transition={s->primary,0,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE}};
    ID3D12GraphicsCommandList7_ResourceBarrier(s->host,1,&copy);
    D3D12_TEXTURE_COPY_LOCATION dst={.pResource=rb,.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT,
        .PlacedFootprint={0,{DXGI_FORMAT_R8G8B8A8_UNORM,1,1,1,256}}},src={.pResource=s->primary};
    D3D12_BOX box={0,0,0,1,1,1};ID3D12GraphicsCommandList7_CopyTextureRegion(s->host,&dst,0,0,0,&src,&box);
    copy.Transition.StateBefore=D3D12_RESOURCE_STATE_COPY_SOURCE;copy.Transition.StateAfter=D3D12_RESOURCE_STATE_RENDER_TARGET;
    ID3D12GraphicsCommandList7_ResourceBarrier(s->host,1,&copy);
    RCK(SUCCEEDED(sl_close(l)) && FAILED(checked_startup_submit(foreign,1,&l,0,0)));
    UINT64 stale=s->reuse_epoch;
    RCK(FAILED(xs_reuse_common(owner,keys[0],old->primary,as_queue(foreign),&stale)));
    RCK(SUCCEEDED(checked_startup_submit(q,1,&l,0,0)) && SUCCEEDED(queue_signal(q,done,1)));
    RCK(FAILED(checked_startup_submit(q,1,&l,0,0)));
    HANDLE event=CreateEventW(NULL,FALSE,FALSE,NULL);RCK(event && SUCCEEDED(xs_fence_event(done,1,event)) &&
        WaitForSingleObject(event,150)==WAIT_TIMEOUT && *pixel==0xdeadbeef);
    InterlockedExchange64((volatile LONG64*)ctrl,ref);
    RCK(WaitForSingleObject(event,10000)==WAIT_OBJECT_0 && *pixel==0 && ID3D12Device_GetDeviceRemovedReason(d)==S_OK);
    ID3D12Resource_Unmap(rb,0,NULL);ID3D12Resource_Release(rb);
    CloseHandle(event);IUnknown_Release(done);IUnknown_Release(foreign);IUnknown_Release(l);IUnknown_Release(a);
    return S_OK;
#undef TRY
#undef RCK
}
#endif
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
