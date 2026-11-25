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
    BOOL native=xs_is_adapter(d,0x1002,0x164e);
    if(!native && !xs_is_rtx3070(d))return E_NOTIMPL;
    IUnknown *a,*l,*foreign,*done;
#define RCK(e) do{if(!(e)){fprintf(stderr,"FAIL primary reacquire line=%d: %s\n",__LINE__,#e);return E_FAIL;}}while(0)
    RCK(SUCCEEDED(xs_allocator(d,0,&a)) && SUCCEEDED(xs_device_list(d,0,a,TRUE,owner,&l)) &&
        SUCCEEDED(xs_direct_queue(d,&foreign)) && SUCCEEDED(xs_shared_fence(d,&done)));
    struct startup_list *s=sl(l);
    UINT64 base=(UINT_PTR)ctrl,ref=3;
    RCK(SUCCEEDED(xs_bind_pool_heaps(l,2,old->heaps)));pool_vtbl.marker(l,base+0x98,2,0,0);
    RCK(SUCCEEDED(xs_bind_pool_heaps(l,2,old->heaps)));
    volatile UINT64 *tick=NULL;
    if(native){
        RCK(old->stamp->retained && old->stamp->tail_offset==0x4196210);
        BYTE *sys=(BYTE*)(old->stamp->tail_addr-old->stamp->tail_offset);
        UINT64 array=(UINT_PTR)(sys+0x419f250),used=array+0x4010;DWORD *rec=(DWORD*)(sys+0x3fb30c0);
        fixture_wb_inputs.active=3;fixture_wb_inputs.index=1;fixture_wb_inputs.slots[3]=(UINT_PTR)rec;
        rec[2]=2048;rec[3]=8;rec[14]=2<<16;
        memcpy(rec+18,&used,8);memcpy(rec+20,&array,8);memcpy(rec+22,&array,8);
        tick=(volatile UINT64*)(array+8);*tick=0;ctrl[0x98/4]=ctrl[0x9c/4]=~0u;MemoryBarrier();
        pool_vtbl.timestamp(l,array+8,0,0,1);pair_op(s,0x238);
        RCK(SUCCEEDED(native_post_at(s,base+0x9c,0x29a5ee15,0,0,0x4ed46aa)));batch_step(s,B_POST);
    }else{
        pool_vtbl.timestamp(l,old->stamp->tail_addr+8,0,0,1);
        pool_vtbl.marker(l,base+0x9c,0x29a5ee15,0,0);
    }
    RCK(SUCCEEDED(wait64_at(s,base,7,ref,1,0x2484b12)));pair_op(s,0x490);
    D3D12_TEXTURE_BARRIER b={.SyncBefore=D3D12_BARRIER_SYNC_NONE,.SyncAfter=D3D12_BARRIER_SYNC_RENDER_TARGET,
        .AccessBefore=D3D12_BARRIER_ACCESS_NO_ACCESS,.AccessAfter=D3D12_BARRIER_ACCESS_RENDER_TARGET,
        .LayoutBefore=(D3D12_BARRIER_LAYOUT)31,.LayoutAfter=D3D12_BARRIER_LAYOUT_RENDER_TARGET,.Subresources={UINT_MAX,0,0,0,0,0}};
    D3D12_BARRIER_GROUP g={.Type=D3D12_BARRIER_TYPE_TEXTURE,.NumBarriers=1,.pTextureBarriers=&b};
#define TRY() record_primary_barrier_at(s,1,&g,0x32113)
    b.pResource=(ID3D12Resource*)keys[1];RCK(FAILED(TRY()));
    b.pResource=old->primary;RCK(FAILED(TRY()));b.pResource=(ID3D12Resource*)keys[0];
    if(native){
        RCK(s->split_at==6 && s->post->parent==s->marker && s->post->counter && s->stamp->third_wb);
        s->split_at=5;RCK(FAILED(TRY()));s->split_at=6;
        s->marker->native_post=0;RCK(FAILED(TRY()));s->marker->native_post=0x29a5ee15;
    }
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
#ifdef TEST_SR_REUSE
    extern IUnknown *test_sr_image(IUnknown *,IUnknown *),*test_dsv_heap(IUnknown *,SIZE_T *);
    SIZE_T dslot=0;IUnknown *key=test_sr_image(owner,NULL),*depth=key?test_sr_image(owner,key):NULL,*prior=NULL;
    IUnknown *dh=test_dsv_heap(owner,&dslot);
    RCK(depth && dh && SUCCEEDED(xs_write_view(owner,0xa8,depth,(DWORD[]){20,3,0,0,0,0},dslot)));
    SIZE_T rt_slot=slot+64,empty=0;RCK(key && SUCCEEDED(xs_write_view(owner,0xa0,key,view,rt_slot)));
    /* The fixture builds a separate producer; the ordinary path uses pair_execute's end signal. */
    extern ID3D12Resource *test_sr_host(IUnknown *);
    ID3D12Resource *img=test_sr_host(key);
    ID3D12CommandAllocator *pa=NULL;ID3D12GraphicsCommandList7 *pl=NULL;
    RCK(SUCCEEDED(ID3D12Device_CreateCommandAllocator(d,D3D12_COMMAND_LIST_TYPE_DIRECT,&IID_ID3D12CommandAllocator,(void**)&pa)) &&
        SUCCEEDED(ID3D12Device_CreateCommandList(d,0,D3D12_COMMAND_LIST_TYPE_DIRECT,pa,NULL,&IID_ID3D12GraphicsCommandList7,(void**)&pl)) &&
        SUCCEEDED(xs_shared_fence(d,&prior)));
    D3D12_TEXTURE_BARRIER rev={.SyncBefore=0x40,.SyncAfter=0x1000,.AccessBefore=8,.AccessAfter=0x80,.LayoutBefore=2,.LayoutAfter=6,.pResource=img,.Subresources.IndexOrFirstMipLevel=UINT_MAX};
    g.pTextureBarriers=&rev;ID3D12GraphicsCommandList7_Barrier(pl,1,&g);RCK(SUCCEEDED(ID3D12GraphicsCommandList7_Close(pl)));
    ID3D12CommandList *batch=(ID3D12CommandList*)pl;
    ID3D12CommandQueue_ExecuteCommandLists(as_queue(q),1,&batch);
    RCK(SUCCEEDED(queue_signal(q,prior,1)));
    b=(D3D12_TEXTURE_BARRIER){.SyncBefore=0x1000,.SyncAfter=0x40,.AccessBefore=0x80,.AccessAfter=8,.LayoutBefore=6,.LayoutAfter=2,
        .pResource=(ID3D12Resource*)key,.Subresources.IndexOrFirstMipLevel=UINT_MAX};g.pTextureBarriers=&b;
    /* Only the consumer prefix's unrelated pipeline setters are synthesized. */
    memcpy(s->ops,"\x20\x47\x20\x48\x47\x4a\x92\x34\x14\x7e\x19\x22\x4b\x2f\x18\x8e\x17\x92",18);
    s->nops=18;s->phase=B_INELIGIBLE;s->pending_470=s->depth_zero_pending=TRUE;
    RCK(FAILED(TRY()));
    struct xs_attachment_pin written={key,img,rt_slot,*(UINT64*)rt_slot};
    struct xs_attachment_pin ds={depth,test_sr_host(depth),dslot,*(UINT64*)dslot};
    RCK(xs_sr_submitted(owner,&written,&ds,as_queue(q),as_fence(prior)));
    b.pResource=(ID3D12Resource*)keys[0];RCK(FAILED(TRY()));b.pResource=(ID3D12Resource*)key;
    ++*(UINT64*)rt_slot;RCK(FAILED(TRY()));--*(UINT64*)rt_slot;
    RCK(SUCCEEDED(TRY()) && s->sr.rt.host==img && img!=s->primary);pair_op(s,0x190);
    RCK(FAILED(record_om(s,1,&slot,FALSE,NULL)) && FAILED(record_om(s,1,&rt_slot,FALSE,&slot)) &&
        FAILED(record_om(s,1,&rt_slot,FALSE,&empty)));
    ++*(UINT64*)dslot;RCK(FAILED(record_om(s,1,&rt_slot,FALSE,&dslot)));--*(UINT64*)dslot;
    RCK(SUCCEEDED(record_om(s,1,&rt_slot,FALSE,&dslot)) && s->om.heap[0] && s->om.heap[1] &&
        FAILED(record_om(s,1,&rt_slot,FALSE,&dslot)));
    RCK(SUCCEEDED(record_bounds(s,0,1)) && SUCCEEDED(record_clear_raster(s,1,&clear_rect,FALSE)) &&
        SUCCEEDED(record_clear_raster(s,1,&clear_viewport,TRUE)));
    const FLOAT red[]={1,0,0,1};ID3D12GraphicsCommandList7_ClearRenderTargetView(s->host,s->om.cpu[0],red,0,NULL);
    ID3D12GraphicsCommandList7_ClearDepthStencilView(s->host,s->om.cpu[1],D3D12_CLEAR_FLAG_DEPTH|D3D12_CLEAR_FLAG_STENCIL,.375f,0x69,0,NULL);
    D3D12_RESOURCE_DESC dr;ds.host->lpVtbl->GetDesc(ds.host,&dr);
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp[2];UINT64 size;ID3D12Device_GetCopyableFootprints(d,&dr,0,2,0,fp,NULL,NULL,&size);
    D3D12_RESOURCE_DESC db=rd;db.Width=size;ID3D12Resource *dsrb=NULL;BYTE *raw=NULL;
    RCK(SUCCEEDED(ID3D12Device_CreateCommittedResource(d,&hp,0,&db,D3D12_RESOURCE_STATE_COPY_DEST,NULL,&IID_ID3D12Resource,(void**)&dsrb)) &&
        SUCCEEDED(ID3D12Resource_Map(dsrb,0,NULL,(void**)&raw)));
    *(float*)raw=-1;raw[fp[1].Offset]=0xcc;
    /* Fixture observation only, after the OM operation: copy the original planes and restore DEPTH_WRITE. */
    D3D12_RESOURCE_BARRIER observe={.Transition={ds.host,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_DEPTH_WRITE,D3D12_RESOURCE_STATE_COPY_SOURCE}};
    ID3D12GraphicsCommandList7_ResourceBarrier(s->host,1,&observe);
    for(UINT i=0;i<2;i++){
        D3D12_TEXTURE_COPY_LOCATION to={.pResource=dsrb,.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT,.PlacedFootprint=fp[i]};
        D3D12_TEXTURE_COPY_LOCATION from={.pResource=ds.host,.SubresourceIndex=i};
        ID3D12GraphicsCommandList7_CopyTextureRegion(s->host,&to,0,0,0,&from,NULL);
    }
    observe.Transition.StateBefore=D3D12_RESOURCE_STATE_COPY_SOURCE;observe.Transition.StateAfter=D3D12_RESOURCE_STATE_DEPTH_WRITE;
    ID3D12GraphicsCommandList7_ResourceBarrier(s->host,1,&observe);
    /* Put a second pixel next to the primary witness at an aligned footprint offset. */
    ID3D12Resource *color_rb=NULL;UINT *color_pixel=NULL;
    RCK(SUCCEEDED(ID3D12Device_CreateCommittedResource(d,&hp,0,&rd,D3D12_RESOURCE_STATE_COPY_DEST,NULL,&IID_ID3D12Resource,(void**)&color_rb)) &&
        SUCCEEDED(ID3D12Resource_Map(color_rb,0,NULL,(void**)&color_pixel)));*color_pixel=0xdeadbeef;
    copy.Transition=(D3D12_RESOURCE_TRANSITION_BARRIER){img,0,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE};
    ID3D12GraphicsCommandList7_ResourceBarrier(s->host,1,&copy);dst.pResource=color_rb;src.pResource=img;
    ID3D12GraphicsCommandList7_CopyTextureRegion(s->host,&dst,0,0,0,&src,&box);
    copy.Transition.StateBefore=D3D12_RESOURCE_STATE_COPY_SOURCE;copy.Transition.StateAfter=D3D12_RESOURCE_STATE_RENDER_TARGET;
    ID3D12GraphicsCommandList7_ResourceBarrier(s->host,1,&copy);
    rev.pResource=(ID3D12Resource*)key;g.pTextureBarriers=&rev;pair_op(s,0x490);RCK(SUCCEEDED(TRY()) && FAILED(TRY()));
    s->pending_470=FALSE; /* fixture contains no draw; allow submission of its standalone tail */
#endif
    RCK(SUCCEEDED(sl_close(l)) && FAILED(checked_startup_submit(foreign,1,&l,0,0)));
    UINT64 stale=s->reuse_epoch;
    RCK(FAILED(xs_reuse_common(owner,keys[0],old->primary,as_queue(foreign),&stale)));
    RCK(SUCCEEDED(checked_startup_submit(q,1,&l,0,0)) && SUCCEEDED(queue_signal(q,done,1)));
    RCK(FAILED(checked_startup_submit(q,1,&l,0,0)));
#ifdef TEST_SR_REUSE
    RCK(SUCCEEDED(xs_write_view(owner,0xa0,keys[1],view,rt_slot)));
    IUnknown_Release(dh);IUnknown_Release(depth);IUnknown_Release(key);IUnknown_Release(l);l=NULL;
#endif
    HANDLE event=CreateEventW(NULL,FALSE,FALSE,NULL);RCK(event && SUCCEEDED(xs_fence_event(done,1,event)) &&
        WaitForSingleObject(event,150)==WAIT_TIMEOUT && *pixel==0xdeadbeef);
#ifdef TEST_SR_REUSE
    RCK(*color_pixel==0xdeadbeef && *(float*)raw==-1 && raw[fp[1].Offset]==0xcc);
#endif
    if(native){
        ULONGLONG until=GetTickCount64()+10000;
        while(ctrl[0x9c/4]!=0x29a5ee15 && GetTickCount64()<until)Sleep(1);
        RCK(*tick && ctrl[0x98/4]==2 && ctrl[0x9c/4]==0x29a5ee15 && *pixel==0xdeadbeef &&
            WaitForSingleObject(event,0)==WAIT_TIMEOUT);
    }
    InterlockedExchange64((volatile LONG64*)ctrl,ref);
    RCK(WaitForSingleObject(event,10000)==WAIT_OBJECT_0 && *pixel==0 && ID3D12Device_GetDeviceRemovedReason(d)==S_OK);
#ifdef TEST_SR_REUSE
    RCK(*color_pixel==0xff0000ff);
    RCK(*(float*)raw==.375f && raw[fp[1].Offset]==0x69);
    ID3D12Resource_Unmap(dsrb,0,NULL);ID3D12Resource_Release(dsrb);
    puts("PASS carried OM190: original fenced depth .375/stencil 69, red RTV and black primary after list release");
    ID3D12Resource_Unmap(color_rb,0,NULL);ID3D12Resource_Release(color_rb);
    ID3D12GraphicsCommandList7_Release(pl);ID3D12CommandAllocator_Release(pa);IUnknown_Release(prior);
    puts("PASS offscreen SR reuse: same-queue producer, private RTV, reverse, red offscreen and black primary after fenced retirement");
#endif
    ID3D12Resource_Unmap(rb,0,NULL);ID3D12Resource_Release(rb);
    CloseHandle(event);IUnknown_Release(done);IUnknown_Release(foreign);if(l)IUnknown_Release(l);IUnknown_Release(a);
    if(native){
        puts("PASS RADV native +490: original +9c before held split-6 tail; returned COMMON primary cleared on late counter");
    }
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
