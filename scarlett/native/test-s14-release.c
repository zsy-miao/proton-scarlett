/* Offscreen GPU qualification, not a publisher for guest leases. All lists, query
 * destinations and fence lifetimes here belong to this executable. */
#include "test-clear-device.c"
#include "queue/s14-wire.h"
#define CHECK(x) do{if(!(x)){fprintf(stderr,"S14 FAIL %u: %s\n",__LINE__,#x);exit(1);}}while(0)
#define OK(x) do{HRESULT hr_=(x);if(hr_!=S_OK){fprintf(stderr,"S14 HRESULT %08lx: %s\n",(DWORD)hr_,#x);exit(1);}}while(0)
struct job14 {ID3D12CommandQueue *q;ID3D12Fence *end;UINT n,calls;ID3D12CommandList *ls[2];
    IUnknown *owner;const struct xs_attachment_pin *target;UINT64 reuse;};
static HRESULT enqueue14(void *ctx)
{
    struct job14 *j=ctx;
    if(j->target){
        const struct xs_attachment_pin *p=j->target;
        if(!xs_live_output(j->owner,p->identity,p->host,p->slot,p->serial) ||
           FAILED(xs_reuse_common(j->owner,p->identity,p->host,j->q,&j->reuse)))return E_ACCESSDENIED;
    }
    ++j->calls;
    ID3D12CommandQueue_ExecuteCommandLists(j->q,j->n,j->ls);
    HRESULT hr=ID3D12CommandQueue_Signal(j->q,j->end,1);
    CHECK(hr==S_OK); /* execution is irreversible */
    return hr;
}
static void wait14(ID3D12Fence *end)
{
    HANDLE ev=CreateEventW(NULL,FALSE,FALSE,NULL);
    CHECK(ev);OK(ID3D12Fence_SetEventOnCompletion(end,1,ev));
    CHECK(WaitForSingleObject(ev,10000)==WAIT_OBJECT_0 && ID3D12Fence_GetCompletedValue(end)==1);
    CloseHandle(ev);
}
static void edge14(ID3D12GraphicsCommandList7 *l,ID3D12Resource *r,D3D12_RESOURCE_STATES from,D3D12_RESOURCE_STATES to)
{
    D3D12_RESOURCE_BARRIER b={.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,from,to}};
    ID3D12GraphicsCommandList7_ResourceBarrier(l,1,&b);
}
int main(void)
{
    const DWORD params[20]={[0]=0x20004,[2]=0x100000,[10]=1,[15]=0x20};
    IUnknown *owner;OK(D3D12XboxCreateDevice(NULL,params,&bootstrap_iid,(void**)&owner));
    struct xbox_device *d=CONTAINING_RECORD(owner,struct xbox_device,iface);
    CHECK(xs_is_rtx3070(d->host));
    IUnknown *views,*key[2];D3D12_DESCRIPTOR_HEAP_DESC hd={D3D12_DESCRIPTOR_HEAP_TYPE_RTV,4096,0,1};
    OK(descriptor_heap(owner,&hd,&IID_ID3D12DescriptorHeap,(void**)&views));
    struct xs_attachment_pin pin[2];
    D3D12_CPU_DESCRIPTOR_HANDLE cpu[2];
    for(UINT i=0;i<2;i++){
        key[i]=test_returned_image(owner,d->host);CHECK(key[i]);
        SIZE_T slot=desc_cpu(views)+i*descriptor_stride(owner,hd.Type);
        for(UINT k=i?15:1;k<=(i?17:14);k++)OK(xs_write_view(owner,RTV,key[i],(DWORD[]){28,4,0,0},slot));
        OK(descriptor_slot(views,slot,cpu+i,NULL));
        pin[i]=(struct xs_attachment_pin){key[i],xs_retain_primary(owner,key[i]),slot,*(UINT64*)slot};
        CHECK(pin[i].host && pin[i].serial==(i?17u:14u));
    }
    CHECK(pin[0].host!=pin[1].host);
    ID3D12Device *dev=d->host;ID3D12CommandQueue *q,*foreign;
    D3D12_COMMAND_QUEUE_DESC qd={0};
    OK(ID3D12Device_CreateCommandQueue(dev,&qd,&IID_ID3D12CommandQueue,(void**)&q));
    OK(ID3D12Device_CreateCommandQueue(dev,&qd,&IID_ID3D12CommandQueue,(void**)&foreign));
    /* Keep the independent readback outside the S14 batch. */
    ID3D12CommandAllocator *a[6];ID3D12GraphicsCommandList7 *l[6];
    for(UINT i=0;i<6;i++){
        OK(ID3D12Device_CreateCommandAllocator(dev,0,&IID_ID3D12CommandAllocator,(void**)&a[i]));
        OK(ID3D12Device_CreateCommandList(dev,1,0,a[i],NULL,&IID_ID3D12GraphicsCommandList7,(void**)&l[i]));
    }
    OK(ID3D12GraphicsCommandList7_Close(l[0]));OK(ID3D12GraphicsCommandList7_Close(l[2]));
    ID3D12Fence *f[6];
    for(UINT i=0;i<6;i++)OK(ID3D12Device_CreateFence(dev,0,0,&IID_ID3D12Fence,(void**)&f[i]));
    CHECK(f[3]!=f[1] && f[3]!=f[2] && l[1]!=l[4]);
    ID3D12QueryHeap *query;D3D12_QUERY_HEAP_DESC queries={D3D12_QUERY_HEAP_TYPE_TIMESTAMP,3,1};
    OK(ID3D12Device_CreateQueryHeap(dev,&queries,&IID_ID3D12QueryHeap,(void**)&query));
    ID3D12Resource *rb;D3D12_HEAP_PROPERTIES hp={.Type=D3D12_HEAP_TYPE_READBACK,.VisibleNodeMask=1,.CreationNodeMask=1};
    D3D12_RESOURCE_DESC rd={.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER,.Width=4096,.Height=1,
        .DepthOrArraySize=1,.MipLevels=1,.SampleDesc.Count=1,.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR};
    OK(ID3D12Device_CreateCommittedResource(dev,&hp,0,&rd,D3D12_RESOURCE_STATE_COPY_DEST,NULL,&IID_ID3D12Resource,(void**)&rb));
    BYTE *map;OK(ID3D12Resource_Map(rb,0,NULL,(void**)&map));memset(map,0xa5,4096);ID3D12Resource_Unmap(rb,0,NULL);
    for(UINT k=0;k<2;k++){
        UINT i=1-k;ID3D12GraphicsCommandList7 *p=l[k?3:1];
        edge14(p,pin[i].host,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_RENDER_TARGET);
        const FLOAT fill[4]={(FLOAT)!k,(FLOAT)k,0,1};
        ID3D12GraphicsCommandList7_ClearRenderTargetView(p,cpu[i],fill,0,NULL);
        if(!k)edge14(p,pin[i].host,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COMMON);
        ID3D12GraphicsCommandList7_EndQuery(p,query,D3D12_QUERY_TYPE_TIMESTAMP,k);
        ID3D12GraphicsCommandList7_ResolveQueryData(p,query,D3D12_QUERY_TYPE_TIMESTAMP,k,1,rb,k?80:32);
        OK(ID3D12GraphicsCommandList7_Close(p));
    }
    D3D12_TEXTURE_BARRIER wire={.SyncBefore=D3D12_BARRIER_SYNC_RENDER_TARGET,.AccessBefore=D3D12_BARRIER_ACCESS_RENDER_TARGET,
        .AccessAfter=D3D12_BARRIER_ACCESS_NO_ACCESS,.LayoutBefore=D3D12_BARRIER_LAYOUT_RENDER_TARGET,
        .LayoutAfter=(D3D12_BARRIER_LAYOUT)31,.Subresources={.IndexOrFirstMipLevel=UINT_MAX},.pResource=(ID3D12Resource*)key[1]};
    CHECK(FAILED(s14_emit(owner,pin+1,l[4],wire)) && FAILED(s14_emit(owner,pin,l[4],wire)));
    wire.pResource=(ID3D12Resource*)key[0];wire.Flags=1;CHECK(FAILED(s14_emit(owner,pin,l[4],wire)));wire.Flags=0;
    OK(s14_emit(owner,pin,l[4],wire));
    ID3D12GraphicsCommandList7_EndQuery(l[4],query,D3D12_QUERY_TYPE_TIMESTAMP,2);
    ID3D12GraphicsCommandList7_ResolveQueryData(l[4],query,D3D12_QUERY_TYPE_TIMESTAMP,2,1,rb,72);
    OK(ID3D12GraphicsCommandList7_Close(l[4]));
    for(UINT i=0;i<2;i++){
        D3D12_TEXTURE_COPY_LOCATION src={.pResource=pin[i].host},dst={.pResource=rb,.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT,
            .PlacedFootprint={512+512*i,{DXGI_FORMAT_R8G8B8A8_UNORM,1,1,1,256}}};
        edge14(l[5],pin[i].host,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_SOURCE);
        const D3D12_BOX pixel={0,0,0,1,1,1};
        ID3D12GraphicsCommandList7_CopyTextureRegion(l[5],&dst,0,0,0,&src,&pixel);
        edge14(l[5],pin[i].host,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_COMMON);
    }
    OK(ID3D12GraphicsCommandList7_Close(l[5]));
    struct job14 jobs[]={{q,f[0],1,0,{(void*)l[0]},NULL,NULL,0},{q,f[1],1,0,{(void*)l[1]},NULL,NULL,0},
        {q,f[2],1,0,{(void*)l[2]},NULL,NULL,0},{q,f[3],2,0,{(void*)l[3],(void*)l[4]},owner,pin,0}};
    /* A synthetic display return of the untouched COMMON image, on its real writer. */
    OK(xs_primary_writer(owner,pin[0].host,q));
    IUnknown *flip=xs_claim_view(owner,key[0],pin[0].host,q,pin[0].slot,14,TRUE);
    CHECK(flip==key[0]);xs_returned_common(flip,pin[0].slot,14);xs_finish_flip(flip);IUnknown_Release(flip);
    OK(xs_reuse_common(owner,key[0],pin[0].host,NULL,&jobs[3].reuse));
    CHECK(jobs[3].reuse==1);
    OK(bootstrap_frame_interval(owner,NULL,16667,1,0));OK(bootstrap_frame_event(owner,0,0,NULL,0));
    UINT64 token;UINT epoch=d->clock.epoch;
    for(UINT i=1;i<=2;i++){OK(bootstrap_frame_wait(owner,0,1000,NULL,0,&token));CHECK(token==i);}
    OK(xs_pair_frame_submit(owner,2,q,FALSE,f[0],enqueue14,jobs,&epoch));
    OK(xs_pair_frame_submit(owner,2,q,TRUE,f[1],enqueue14,jobs+1,&epoch));wait14(f[1]);
    OK(bootstrap_frame_wait(owner,0,1000,NULL,0,&token));CHECK(token==3);
    CHECK(FAILED(xs_pair_frame_submit(owner,3,q,TRUE,f[3],enqueue14,jobs+3,&epoch)));
    OK(xs_pair_frame_submit(owner,3,q,FALSE,f[2],enqueue14,jobs+2,&epoch));wait14(f[2]);
    ++jobs[3].reuse;CHECK(FAILED(xs_pair_frame_submit(owner,3,q,TRUE,f[3],enqueue14,jobs+3,&epoch)));--jobs[3].reuse;
    CHECK(FAILED(xs_pair_frame_submit(owner,2,q,TRUE,f[3],enqueue14,jobs+3,&epoch)) &&
        FAILED(xs_pair_frame_submit(owner,3,foreign,TRUE,f[3],enqueue14,jobs+3,&epoch)) && !jobs[3].calls);
    OK(ID3D12CommandQueue_Wait(q,f[4],1));
    OK(xs_pair_frame_submit(owner,3,q,TRUE,f[3],enqueue14,jobs+3,&epoch));
    CHECK(FAILED(xs_pair_frame_submit(owner,3,q,TRUE,f[3],enqueue14,jobs+3,&epoch)) && jobs[3].calls==1 &&
        frame_find(&d->clock,2)->batch_end==f[1] && frame_find(&d->clock,3)->batch_end==f[3] &&
        xs_frame_submission_query(owner,3,q,TRUE)==E_PENDING && ID3D12Fence_GetCompletedValue(f[3])==0);
    OK(ID3D12Fence_Signal(f[4],1));wait14(f[3]);OK(xs_frame_submission_query(owner,3,q,TRUE));
    ID3D12CommandList *oracle=(ID3D12CommandList*)l[5];
    ID3D12CommandQueue_ExecuteCommandLists(q,1,&oracle);OK(ID3D12CommandQueue_Signal(q,f[5],1));wait14(f[5]);
    OK(ID3D12Resource_Map(rb,0,NULL,(void**)&map));
    UINT64 old,head,tail,sentinel=0xa5a5a5a5a5a5a5a5ull;
    memcpy(&old,map+32,8);memcpy(&head,map+80,8);memcpy(&tail,map+72,8);
    CHECK(old && old!=sentinel && head!=sentinel && tail!=sentinel && old<=head && head<=tail);
    CHECK(!memcmp(map+512,"\0\xff\0\xff",4) && !memcmp(map+1024,"\xff\0\0\xff",4));
    for(UINT i=0;i<4096;i++)if(!(i>=32 && i<40) && !(i>=72 && i<88) &&
        !(i>=512 && i<516) && !(i>=1024 && i<1028))CHECK(map[i]==0xa5);
    ID3D12Resource_Unmap(rb,0,NULL);OK(ID3D12Device_GetDeviceRemovedReason(dev));
    printf("PASS S14 fresh submit token=3: ordered native pair, batchEnd=%p old17End=%p; queries %llu <= %llu <= %llu, green14/red17 and guards\n",
        f[3],f[1],old,head,tail);
    OK(bootstrap_frame_interval(owner,NULL,33333,1,0));
    for(UINT i=0;i<6;i++){ID3D12GraphicsCommandList7_Release(l[i]);ID3D12CommandAllocator_Release(a[i]);ID3D12Fence_Release(f[i]);}
    ID3D12QueryHeap_Release(query);ID3D12Resource_Release(rb);ID3D12CommandQueue_Release(q);ID3D12CommandQueue_Release(foreign);
    for(UINT i=0;i<2;i++){IUnknown_Release(key[i]);ID3D12Resource_Release(pin[i].host);}
    IUnknown_Release(views);IUnknown_Release(owner);return 0;
}
