#include <dxgi1_4.h>
#ifdef TEST_ORDERED_PRESENT
#include "test-ordered-batch.hpp"
extern "C" HRESULT test_replace_end(IUnknown *, ID3D12Fence *, ID3D12Fence *);
extern "C" UINT test_ring_index(IUnknown *);
extern "C" HRESULT test_displayed_count(IUnknown *,UINT *);
#endif
extern "C" ID3D12CommandQueue *test_host_queue(IUnknown*);
extern "C" HRESULT test_bridge_count(IUnknown *,UINT *);
extern "C" ULONG_PTR test_generation(IUnknown*);
extern "C" HANDLE test_pending_present;
#ifdef TEST_REACQUIRE
extern "C" HRESULT test_reacquire(IUnknown*,IUnknown*,IUnknown**,SIZE_T,volatile UINT*,BOOL);
#endif
struct BridgeGUI {
    IUnknown *dev,*queue=nullptr;
    HWND hwnd=nullptr,replacement=nullptr;
    HANDLE ready=CreateEventW(nullptr,TRUE,FALSE,nullptr),changed=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    HANDLE destroy=CreateEventW(nullptr,FALSE,FALSE,nullptr),quit=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    DWORD tid=0;
};
static DWORD WINAPI bridge_gui(void *ctx)
{
    auto g=(BridgeGUI*)ctx;g->tid=GetCurrentThreadId();
    WNDCLASSA cls={};cls.lpfnWndProc=DefWindowProcA;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName="sgaWindowClass";
    CK(RegisterClassA(&cls));
    auto window=[&](){return CreateWindowExA(0,cls.lpszClassName,"Bridge fixture",WS_OVERLAPPEDWINDOW,0,0,800,600,nullptr,nullptr,cls.hInstance,nullptr);};
    CK((g->hwnd=window()));ShowWindow(g->hwnd,SW_SHOW);UpdateWindow(g->hwnd);
    using Queue=HRESULT(WINAPI*)(IUnknown*,const D3D12_COMMAND_QUEUE_DESC*,REFIID,void**);
    D3D12_COMMAND_QUEUE_DESC qd={};qd.NodeMask=1;
    HR(SLOT(g->dev,0x40,Queue)(g->dev,&qd,IID_ID3D12CommandQueue,(void**)&g->queue));
#ifdef TEST_ORDERED_PRESENT
    CK(SignalObjectAndWait(g->ready,g->quit,INFINITE,FALSE)==WAIT_OBJECT_0);
    CK(DestroyWindow(g->hwnd));return 0;
#else
    CK(SignalObjectAndWait(g->ready,g->destroy,INFINITE,FALSE)==WAIT_OBJECT_0);
    CK(!DestroyWindow(g->hwnd) && IsWindow(g->hwnd));CK(SetEvent(g->changed));
    CK(WaitForSingleObject(test_pending_present,5000)==WAIT_OBJECT_0 && DestroyWindow(g->hwnd));
    CK((g->replacement=window()));ShowWindow(g->replacement,SW_SHOW);
    CK(SignalObjectAndWait(g->changed,g->quit,INFINITE,FALSE)==WAIT_OBJECT_0);
    CK(DestroyWindow(g->replacement));return 0;
#endif
}
#ifdef TEST_ORDERED_PRESENT
template<class Recorder>
#endif
static int bridge_fixture(ID3D12Device *d,IUnknown *dev,IUnknown **keys,IUnknown **batches,
#ifdef TEST_PAIR_SPLIT
        IUnknown **heaps,BYTE *head,
#endif
#ifdef TEST_ORDERED_PRESENT
        ID3D12Resource *upload_buffer,IUnknown *views,Recorder &record_recycled,ComPtr<IUnknown> &recycled,
#endif
        ID3D12Resource **sources,SIZE_T dest,UINT *upload,volatile UINT *ctrl,volatile UINT64 *ticks)
{
    BridgeGUI gui={dev};DWORD tid;HANDLE thread=CreateThread(nullptr,0,bridge_gui,&gui,0,&tid);CK(thread);
    CK(WaitForSingleObject(gui.ready,10000)==WAIT_OBJECT_0 && gui.tid!=GetCurrentThreadId());
    ComPtr<IUnknown> q;q.Attach(gui.queue);

#ifndef TEST_ORDERED_PRESENT
    using Queue=HRESULT(WINAPI*)(IUnknown*,const D3D12_COMMAND_QUEUE_DESC*,REFIID,void**);
#endif
    using Interval=HRESULT(WINAPI*)(IUnknown*,IDXGIOutput*,UINT,UINT,UINT);
    using Schedule=HRESULT(WINAPI*)(IUnknown*,UINT,UINT,const void*,UINT);
    using Wait=HRESULT(WINAPI*)(IUnknown*,UINT,DWORD,const void*,UINT,UINT64*);
    using Close=HRESULT(WINAPI*)(IUnknown*);using Alloc=HRESULT(WINAPI*)(IUnknown*,UINT,REFIID,void**);
    using List=HRESULT(WINAPI*)(IUnknown*,const void*,IUnknown*,IUnknown*,REFIID,void**);
    using Present=HRESULT(WINAPI*)(IUnknown*,UINT,const void*,const void*);
    auto fw=SLOT(dev,0x280,Wait);auto set=SLOT(dev,0x270,Interval);
#ifndef TEST_ORDERED_PRESENT
    HR(set(dev,nullptr,33333,3,0));
    HR(SLOT(dev,0x278,Schedule)(dev,0,0,nullptr,0));HR(SLOT(dev,0x278,Schedule)(dev,2,0,nullptr,0));
#endif
    ComPtr<IUnknown> sync,alloc,foreign;
#ifndef TEST_ORDERED_PRESENT
    auto sched=SLOT(dev,0x278,Schedule);
    D3D12_COMMAND_QUEUE_DESC qd={};qd.NodeMask=1;
    HR(SLOT(dev,0x40,Queue)(dev,&qd,IID_ID3D12CommandQueue,(void**)foreign.GetAddressOf()));
#endif
    HR(SLOT(dev,0x48,Alloc)(dev,0,IID_ID3D12CommandAllocator,(void**)alloc.GetAddressOf()));
    UINT sd[]={0,2,1,0};HR(SLOT(dev,0x248,List)(dev,sd,alloc.Get(),nullptr,xs_sync_list_id,(void**)sync.GetAddressOf()));HR(SLOT(sync.Get(),0x48,Close)(sync.Get()));
    auto present=[&](IUnknown *queue,UINT64 tok,IUnknown *key){
        RECT rect={0,0,3840,2160};float dst[]={0,0,1,1};UINT64 plane[]={tok,1,(UINT_PTR)&key,(UINT_PTR)&rect,(UINT_PTR)dst,0,0,0,0},opts[4]={};
        return SLOT(queue,0x100,Present)(queue,1,plane,opts);};
    UINT count;HR(test_bridge_count(q.Get(),&count));UINT before=count;CK(before==0);
#ifdef TEST_ORDERED_PRESENT
    const auto generation=test_generation(q.Get());CK(generation);
    const UINT ring=test_ring_index(q.Get());CK(ring<2);
    UINT64 token=0;IUnknown *sync_id=sync.Get();
    // Open a 95 ms early-poll window after all slow setup. Use the token for a real flip.
    HR(set(dev,nullptr,33333,3,0));
    HR(SLOT(dev,0x278,Schedule)(dev,2,0,nullptr,0));
    HR(SLOT(dev,0x278,Schedule)(dev,0,95000,nullptr,0));
    CK(fw(dev,0,0,nullptr,0,&token)==DXGI_ERROR_WAIT_TIMEOUT && token==0);
    CK(fw(dev,0,0,nullptr,2,&token)==S_FALSE && token==1);
    const UINT64 first_token=token;
    printf("PASS early origin poll flags=2 hr=S_FALSE token=1\n");
    auto displayed=[&](UINT wanted) {
        UINT id=0;
        ULONGLONG deadline=GetTickCount64()+5000;
        do {
            HRESULT hr=test_displayed_count(q.Get(),&id);
            if(hr!=S_OK || id>wanted) {
                fprintf(stderr,"Frame feedback expected=%u got=%u hr=%08lx\n",wanted,id,(unsigned long)hr);
                return false;
            }
            if(id==wanted)return true;
            Sleep(1);
        } while(GetTickCount64()<deadline);
        fprintf(stderr,"Frame feedback timeout expected=%u got=%u\n",wanted,id);
        return false;
    };
    for(UINT y=0;y<oh;y++)for(UINT x=0;x<ow;x++)upload[y*ow+x]=x<ow/2?0xff0000ff:0xffff0000;
    InterlockedExchange64((volatile LONG64*)ctrl,0);ctrl[2]=ctrl[3]=~0u;ticks[0]=ticks[1]=0;MemoryBarrier();
    HR(test_tagged(q.Get(),1,&sync_id,token,0));HR(test_tagged(q.Get(),1,batches,token,3));
    const ULONGLONG head_deadline=GetTickCount64()+5000;
    while(ctrl[3]!=0x29a5ee15 && GetTickCount64()<head_deadline)Sleep(1);
    CK(ctrl[2]==0 && ctrl[3]==0x29a5ee15 && ticks[1] && !ticks[0]);
    CK(present(q.Get(),token,keys[0])==DXGI_ERROR_WAS_STILL_DRAWING && !ticks[0]);
#ifdef TEST_REACQUIRE
    HR(test_reacquire(q.Get(),batches[0],keys,dest,ctrl,FALSE));
#endif
    InterlockedExchange64((volatile LONG64*)ctrl,1);
    auto flip=[&](IUnknown *key){
        HRESULT hr;ULONGLONG deadline=GetTickCount64()+10000;
        do {hr=present(q.Get(),token,key);if(hr==DXGI_ERROR_WAS_STILL_DRAWING)Sleep(1);}
        while(hr==DXGI_ERROR_WAS_STILL_DRAWING && GetTickCount64()<deadline);
        return hr;
    };
    CK(flip(keys[0])==S_OK && displayed(1));
    HR(test_bridge_count(q.Get(),&count));CK(count==1 && ticks[0]>ticks[1]);
    CK(test_ring_index(q.Get())==1-ring);
#ifdef TEST_REACQUIRE
    HR(test_reacquire(q.Get(),batches[0],keys,dest,ctrl,TRUE));
    CK(SetEvent(gui.quit) && WaitForSingleObject(thread,5000)==WAIT_OBJECT_0);
    CloseHandle(thread);CloseHandle(gui.changed);CloseHandle(gui.ready);CloseHandle(gui.destroy);CloseHandle(gui.quit);
    puts("PASS primary reacquire: fenced original COMMON to RT after ring flip; stale, foreign, alias and invalid rejected");
    return 0;
#endif
    printf("PASS singleton token=1 count=1 generation=%zu\n",generation);Sleep(1600);
    HR(fw(dev,0,1000,nullptr,0,&token));CK(token==2);
    const UINT64 second_token=token;
    HR(test_tagged(q.Get(),1,&sync_id,token,0));
    for(UINT y=0;y<oh;y++)for(UINT x=0;x<ow;x++)upload[y*ow+x]=x<ow/2?0xff00ff00:0xffff00ff;
    MemoryBarrier();
    auto *c=new OrderedChunk;
    HANDLE retired=c->retired=CreateEventW(nullptr,TRUE,FALSE,nullptr);CK(retired);
    c->upload=upload_buffer;c->views=views;c->texture=sources[1];c->key=keys[1];c->slot=dest+4095*32;
    auto rd=upload_buffer->GetDesc();D3D12_HEAP_PROPERTIES hp={};hp.CreationNodeMask=hp.VisibleNodeMask=1;
    hp.Type=D3D12_HEAP_TYPE_DEFAULT;
    HR(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&c->intermediate)));
    hp.Type=D3D12_HEAP_TYPE_READBACK;rd.Width=256;
    HR(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&c->read)));
#ifdef TEST_PAIR_SPLIT
    c->heaps=heaps;c->ctrl=ctrl;c->head=head;
    ComPtr<ID3D12Resource> split_read=c->read;
    volatile UINT *sample=nullptr;HR(split_read->Map(0,nullptr,(void**)&sample));
    sample[0]=sample[1]=0xabababab;ctrl[2]=ctrl[3]=~0u;ticks[0]=ticks[1]=0;
    InterlockedExchange64((volatile LONG64*)ctrl,0);MemoryBarrier();
#endif
    ComPtr<IUnknown> first,last,gate;
#ifdef TEST_SAME_BATCH_RELEASE
    auto make_rt=[&](IUnknown **out){
        return xs_ordered_record(q.Get(),[](IUnknown*l,void*p){
#ifdef TEST_PAIR_SPLIT
            HRESULT hr;
            auto &c=*(OrderedChunk*)p;
            using Mark=void(WINAPI*)(IUnknown*,const volatile void*,UINT,UINT64,UINT);
            using Stamp=void(WINAPI*)(IUnknown*,const void*,UINT64,UINT64,UINT);
            if((hr=xs_bind_pool_heaps(l,2,c.heaps))!=S_OK)return hr;
            SLOT(l,0x238,Mark)(l,c.ctrl+2,0,0,0);
            if((hr=xs_bind_pool_heaps(l,2,c.heaps))!=S_OK)return hr;
            if((hr=ordered_copy(l,p,false))!=S_OK)return hr;
            SLOT(l,0x240,Stamp)(l,c.head,0,0,1);
            SLOT(l,0x238,Mark)(l,c.ctrl+3,0x29a5ee15,0,0);
            SLOT(l,0x250,Mark)(l,c.ctrl,7,1,1);
#else
            HRESULT hr=ordered_copy(l,p,false);
#endif
            return hr==S_OK?ordered_copy(l,p,true):hr;
        },c,c,c->key,out);
    };
    D3D12_TEXTURE_BARRIER release={D3D12_BARRIER_SYNC_RENDER_TARGET,D3D12_BARRIER_SYNC_NONE,
        D3D12_BARRIER_ACCESS_RENDER_TARGET,D3D12_BARRIER_ACCESS_NO_ACCESS,D3D12_BARRIER_LAYOUT_RENDER_TARGET,
        (D3D12_BARRIER_LAYOUT)0x1f,(ID3D12Resource*)c->key,{UINT_MAX,0,0,0,0,0},D3D12_TEXTURE_BARRIER_FLAG_NONE};
    auto pair=[&](){
        HRESULT hr=make_rt(first.ReleaseAndGetAddressOf());
        return hr!=S_OK?hr:test_record_successor(q.Get(),first.Get(),c->key,c->slot,c->serial,&release,last.ReleaseAndGetAddressOf());
    };
    auto reject=[&](IUnknown *a,IUnknown *b){
        IUnknown *v[]={a,b};ComPtr<ID3D12Fence> f;
        return FAILED(xs_ordered_submit(q.Get(),2,v,token,3,f.GetAddressOf())) && !f && !test_ordered_host_submissions() &&
            xs_frame_submission_query(dev,token,test_host_queue(q.Get()),TRUE)==S_FALSE;
    };
    HR(pair());
    CK(reject(last.Get(),first.Get()) && reject(first.Get(),first.Get()));
    ComPtr<IUnknown> other_rt,other_release,wrongq;
    HR(make_rt(other_rt.GetAddressOf()));CK(reject(other_rt.Get(),last.Get()));
    using Q=HRESULT(WINAPI*)(IUnknown*,const D3D12_COMMAND_QUEUE_DESC*,REFIID,void**);
    D3D12_COMMAND_QUEUE_DESC qdesc={};qdesc.NodeMask=1;
    HR(SLOT(dev,0x40,Q)(dev,&qdesc,IID_ID3D12CommandQueue,(void**)wrongq.GetAddressOf()));
    CK(FAILED(test_record_successor(wrongq.Get(),first.Get(),c->key,c->slot,c->serial,&release,other_release.GetAddressOf())));
    release.pResource=(ID3D12Resource*)keys[0];
    CK(FAILED(test_record_successor(q.Get(),first.Get(),c->key,c->slot,c->serial,&release,other_release.GetAddressOf())));
    // A genuine registered sibling and its current serial still cannot consume B's RT.
    UINT64 sibling_serial;memcpy(&sibling_serial,(void*)dest,8);
    HR(test_record_successor(q.Get(),first.Get(),keys[0],dest,sibling_serial,&release,other_release.GetAddressOf()));
    CK(reject(first.Get(),other_release.Get()));other_release.Reset();
    release.pResource=(ID3D12Resource*)c->key;
    release.Subresources.IndexOrFirstMipLevel=0;
    CK(FAILED(test_record_successor(q.Get(),first.Get(),c->key,c->slot,c->serial,&release,other_release.GetAddressOf())));
    release.Subresources.IndexOrFirstMipLevel=UINT_MAX;
    IUnknown *gap[]={first.Get(),other_rt.Get(),last.Get()};
    CK(FAILED(xs_ordered_submit(q.Get(),3,gap,token,3,nullptr)) && !test_ordered_host_submissions());
    // Even rewriting the SAME key into the SAME slot invalidates this closed pair.
    DWORD rtv[]={28,4,0,0,0,0};HR(xs_write_view(dev,0xa0,c->key,rtv,c->slot));
    CK(reject(first.Get(),last.Get()));HR(pair());
    HR(test_intervening_write(q.Get(),c->key,c->slot));CK(reject(first.Get(),last.Get()));
    other_rt.Reset();HR(pair());
    puts("PASS same-batch admission: stale slot/writer, sibling, order, gap, predecessor, queue rejected before Execute");
#else
    HR(xs_ordered_record(q.Get(),[](IUnknown*l,void*p){return ordered_copy(l,p,false);},c,c,nullptr,first.GetAddressOf()));
    HR(xs_ordered_record(q.Get(),[](IUnknown*l,void*p){return ordered_copy(l,p,true);},c,c,keys[1],last.GetAddressOf()));
#endif
    IUnknown *ids[]={last.Get(),first.Get()};
    CK(FAILED(xs_ordered_submit(q.Get(),2,ids,token,3,nullptr)) && !test_ordered_host_submissions());
    ids[0]=first.Get();ids[1]=last.Get();
#ifdef TEST_PAIR_SPLIT
    // Drain the actual queue after all invalid arrays. A leaked prefix/head is observable here.
    ComPtr<ID3D12Fence> drain;HR(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&drain)));
    HR(test_host_queue(q.Get())->Signal(drain.Get(),1));
    HANDLE e=CreateEventW(nullptr,FALSE,FALSE,nullptr);CK(e);HR(drain->SetEventOnCompletion(1,e));
    CK(WaitForSingleObject(e,5000)==WAIT_OBJECT_0);CloseHandle(e);
    CK(ctrl[2]==~0u && ctrl[3]==~0u && !ticks[1] && sample[0]==0xabababab &&
       xs_frame_submission_query(dev,token,test_host_queue(q.Get()),FALSE)==S_OK);
#endif
    HR(xs_shared_fence(d,gate.GetAddressOf()));HR(xs_queue_wait(q.Get(),gate.Get(),1));
    ComPtr<ID3D12Fence> end,impostor;HR(d->CreateFence(1,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&impostor)));
    CK(xs_ordered_submit(q.Get(),2,ids,token,3,end.GetAddressOf())==S_OK && test_ordered_host_submissions()==1);
    first.Reset();last.Reset();c->Release();
    CK(present(q.Get(),token,keys[0])==DXGI_ERROR_INVALID_CALL);
    CK(present(q.Get(),token,keys[1])==DXGI_ERROR_WAS_STILL_DRAWING);
    CK(!end->GetCompletedValue() && WaitForSingleObject(retired,0)==WAIT_TIMEOUT);
    HR(test_replace_end(q.Get(),end.Get(),impostor.Get()));
    CK(present(q.Get(),token,keys[1])==DXGI_ERROR_INVALID_CALL);
    HR(test_replace_end(q.Get(),impostor.Get(),end.Get()));
    HR(test_bridge_count(q.Get(),&count));CK(count==1 && fw(dev,2,0,nullptr,0,&token)==DXGI_ERROR_WAIT_TIMEOUT);
    HR(xs_cpu_signal(gate.Get(),1));
#ifdef TEST_PAIR_SPLIT
    ULONGLONG deadline=GetTickCount64()+5000;
    while((!ticks[1] || ctrl[3]!=0x29a5ee15) && GetTickCount64()<deadline)Sleep(1);
    CK(ctrl[2]==0 && ctrl[3]==0x29a5ee15 && ticks[1] && !end->GetCompletedValue() &&
       sample[0]==0xabababab && sample[1]==0xabababab && WaitForSingleObject(retired,0)==WAIT_TIMEOUT);
    CK(xs_frame_submission_query(dev,token,test_host_queue(q.Get()),TRUE)==E_PENDING &&
       present(q.Get(),token,keys[1])==DXGI_ERROR_WAS_STILL_DRAWING);
    InterlockedExchange64((volatile LONG64*)ctrl,1);
#endif
    CK(WaitForSingleObject(retired,10000)==WAIT_OBJECT_0 && end->GetCompletedValue()==1);
#ifdef TEST_PAIR_SPLIT
    CK(sample[0]==0xff00ff00 && sample[1]==0xff00ff00 && fw(dev,2,0,nullptr,0,&token)==DXGI_ERROR_WAIT_TIMEOUT);
    split_read->Unmap(0,nullptr);
    puts("PASS split flags-3: rejection drained clean; sync done; prefix, head timestamp, post visible with tail blocked; tail pixels and one batch end before flip");
#endif
    CK(test_generation(q.Get())==generation && flip(keys[1])==S_OK);
    CK(displayed(2) && present(q.Get(),token,keys[1])==DXGI_ERROR_INVALID_CALL);
    HR(test_bridge_count(q.Get(),&count));CK(count==2);CloseHandle(retired);
    printf("PASS ordered token=2 count=2 producer_end=%p generation=%zu wrong-key/end rejected\n",end.Get(),generation);
    Sleep(1600);CK(test_ring_index(q.Get())==ring);
    HR(fw(dev,0,1000,nullptr,0,&token));CK(token==3);
    HR(test_tagged(q.Get(),1,&sync_id,token,0));
    CK(present(q.Get(),token,keys[0])==DXGI_ERROR_INVALID_CALL); // sync alone cannot re-arm A
    CK(!record_recycled() && recycled && recycled.Get()!=batches[0]);
    for(UINT y=0;y<oh;y++)for(UINT x=0;x<ow;x++)upload[y*ow+x]=x<ow/2?0xff00ffff:0xffffff00;
    InterlockedExchange64((volatile LONG64*)ctrl,0);ctrl[2]=ctrl[3]=~0u;ticks[0]=ticks[1]=0;MemoryBarrier();
    IUnknown *third=recycled.Get();HR(test_tagged(q.Get(),1,&third,token,3));
    CK(test_ordered_host_submissions()==1 && present(q.Get(),1,keys[0])==DXGI_ERROR_INVALID_CALL);
    CK(present(q.Get(),token,keys[1])==DXGI_ERROR_INVALID_CALL);
    CK(present(q.Get(),token,keys[0])==DXGI_ERROR_WAS_STILL_DRAWING);
    ULONGLONG until=GetTickCount64()+5000;while(ctrl[3]!=0x29a5ee15 && GetTickCount64()<until)Sleep(1);
    CK(ctrl[2]==0 && ctrl[3]==0x29a5ee15 && ticks[1] && !ticks[0]);
    HR(test_bridge_count(q.Get(),&count));CK(count==2 && fw(dev,2,0,nullptr,0,&token)==DXGI_ERROR_WAIT_TIMEOUT);
    UINT64 old=first_token;
    CK(fw(dev,2,0,nullptr,0,&old)==S_OK && old==first_token);
    printf("PASS older wait token=1 raw_displayed=2 before_flip=3 generation=%zu\n",generation);
    InterlockedExchange64((volatile LONG64*)ctrl,1);
    CK(test_generation(q.Get())==generation && flip(keys[0])==S_OK && displayed(3));
    old=second_token;
    CK(fw(dev,2,0,nullptr,0,&old)==S_OK && old==second_token);
    printf("PASS older wait token=2 raw_displayed=3 after_flip=3 generation=%zu\n",generation);
    CK(fw(dev,2,0,nullptr,0,&token)==S_OK);
    CK(ticks[0]>ticks[1] && present(q.Get(),token,keys[0])==DXGI_ERROR_INVALID_CALL);
    HR(test_bridge_count(q.Get(),&count));CK(count==3 && test_ring_index(q.Get())==1-ring);
    printf("PASS recycled singleton token=3 count=3 key=%p ring=%u generation=%zu\n",keys[0],ring,generation);
    Sleep(1600);CK(SetEvent(gui.quit) && WaitForSingleObject(thread,5000)==WAIT_OBJECT_0);
#else
    // Fixture producer restores its UAV precondition only after the preceding Present returns.
    auto rq=test_host_queue(q.Get());ComPtr<ID3D12CommandAllocator> ra;ComPtr<ID3D12Fence> rf;
    HR(d->CreateCommandAllocator(qd.Type,IID_PPV_ARGS(&ra)));HR(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&rf)));
    // Existing 16-entry history still evicts after a duplicate setter.
    UINT64 oldest,probe;HR(fw(dev,0,1000,nullptr,0,&oldest));
    for(unsigned j=0;j<15;j++)HR(fw(dev,0,1000,nullptr,0,&probe));
    HR(set(dev,nullptr,33333,3,0));CK(fw(dev,2,0,nullptr,0,&oldest)==DXGI_ERROR_WAIT_TIMEOUT);
    HR(fw(dev,0,1000,nullptr,0,&probe));CK(fw(dev,2,0,nullptr,0,&oldest)==DXGI_ERROR_INVALID_CALL);
    puts("PASS 16-entry history boundary across duplicate");
    for(UINT frame=0;frame<5;frame++) {
        unsigned i=frame%2;auto surface=keys[i],sibling=keys[1-i],batch=batches[i];
        if(frame>=2){ComPtr<ID3D12GraphicsCommandList7> restore;HR(d->CreateCommandList(1,qd.Type,ra.Get(),nullptr,IID_PPV_ARGS(&restore)));
        D3D12_TEXTURE_BARRIER t={D3D12_BARRIER_SYNC_NONE,D3D12_BARRIER_SYNC_COMPUTE_SHADING,D3D12_BARRIER_ACCESS_NO_ACCESS,D3D12_BARRIER_ACCESS_UNORDERED_ACCESS,D3D12_BARRIER_LAYOUT_COMMON,D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS,sources[i],{UINT_MAX,0,0,0,0,0},D3D12_TEXTURE_BARRIER_FLAG_NONE};
        D3D12_BARRIER_GROUP g={};g.Type=D3D12_BARRIER_TYPE_TEXTURE;g.NumBarriers=1;g.pTextureBarriers=&t;restore->Barrier(1,&g);HR(restore->Close());ID3D12CommandList *cmd[]={restore.Get()};rq->ExecuteCommandLists(1,cmd);HR(rq->Signal(rf.Get(),frame));HR(rf->SetEventOnCompletion(frame,nullptr));}
        UINT64 token;HR(fw(dev,0,1000,nullptr,0,&token));
        for(UINT y=0;y<2160;y++) for(UINT x=0;x<3840;x++)
            upload[y*3840+x]=frame?(x<1920?0xff00ff00:0xffff00ff):(x<1920?0xff0000ff:0xffff0000);
        InterlockedExchange64((volatile LONG64*)ctrl,0);ctrl[2]=ctrl[3]=~0u;ticks[0]=ticks[1]=0;MemoryBarrier();
        IUnknown *sl[]={sync.Get()},*bl[]={batch};HR(test_tagged(q.Get(),1,sl,token,0));HR(test_tagged(q.Get(),1,bl,token,3));
        ULONGLONG end=GetTickCount64()+5000;while(ctrl[3]!=0x29a5ee15 && GetTickCount64()<end) Sleep(1);
        HR(set(dev,nullptr,33333,3,0));
        CK(set(dev,nullptr,33333,3,1)==E_NOTIMPL && set(dev,(IDXGIOutput*)dev,33333,3,0)==E_NOTIMPL);
        CK(ctrl[2]==0 && ctrl[3]==0x29a5ee15 && ticks[1] && !ticks[0]);
        CK(present(q.Get(),token,sibling)==DXGI_ERROR_INVALID_CALL && present(q.Get(),token+1,surface)==DXGI_ERROR_INVALID_CALL);
        CK(FAILED(present(foreign.Get(),token,surface)));
        CK(present(q.Get(),token,surface)==DXGI_ERROR_WAS_STILL_DRAWING);
        HR(test_bridge_count(q.Get(),&count));CK(count==before+(frame<2?frame:2) && fw(dev,2,0,nullptr,0,&token)==DXGI_ERROR_WAIT_TIMEOUT);
        if(frame==2){
            HR(set(dev,nullptr,33333,2,0));HR(sched(dev,2,0,nullptr,0));
            CK(fw(dev,2,0,nullptr,0,&token)==DXGI_ERROR_INVALID_CALL);
            HR(set(dev,nullptr,16667,2,0));HR(sched(dev,0,0,nullptr,0));HR(fw(dev,0,1000,nullptr,0,&probe));
            HR(set(dev,nullptr,33333,2,0));HR(sched(dev,2,0,nullptr,0));CK(fw(dev,2,0,nullptr,0,&probe)==DXGI_ERROR_INVALID_CALL);
            HR(set(dev,nullptr,33333,3,0));HR(sched(dev,0,0,nullptr,0));HR(sched(dev,2,0,nullptr,0));
        }
        if(frame==3){DWORD view[]={28,4,0,0,0,0};HR(xs_write_view(dev,0xa0,sibling,view,dest+4095*32));HR(xs_write_view(dev,0xa0,surface,view,dest+4095*32));}
        if(frame==4){
            using Query=ULONG_PTR(WINAPI*)(HWND,ULONG_PTR,DWORD);
            auto query=(Query)(void*)GetProcAddress(GetModuleHandleW(L"win32u.dll"),"NtUserCallHwndParam");
            auto gen=test_generation(q.Get());CK(query && !query(gui.hwnd,gen+1,0x53435703) && query(gui.hwnd,gen,0x53435703)==1);
            test_pending_present=CreateEventW(nullptr,TRUE,FALSE,nullptr);CK(test_pending_present);
            CK(SetEvent(gui.destroy) && WaitForSingleObject(gui.changed,10000)==WAIT_OBJECT_0 && IsWindow(gui.hwnd));
            CK(ResetEvent(gui.changed) && query(gui.hwnd,gen,0x53435704)==1);
            // Destruction is driven by a pending +100, while the producer is still gated.
            ULONGLONG start=GetTickCount64();
            CK(present(q.Get(),token,surface)==DXGI_ERROR_WAS_STILL_DRAWING && GetTickCount64()-start<1000);
            CK(WaitForSingleObject(gui.changed,1000)==WAIT_OBJECT_0 && !ticks[0]);
            CK(SetEvent(gui.quit) && WaitForSingleObject(thread,1000)==WAIT_OBJECT_0 && !ticks[0]);
            CloseHandle(test_pending_present);test_pending_present=nullptr;
            puts("PASS stalled producer: bounded return and creator destruction/replacement/exit before gate opened");
            printf("PASS active lease vetoed creator destruction; replacement rejected after release\n");
        }
        InterlockedExchange64((volatile LONG64*)ctrl,1);
        end=GetTickCount64()+10000;HRESULT hr;
        do{hr=present(q.Get(),token,surface);if(hr==DXGI_ERROR_WAS_STILL_DRAWING)Sleep(1);}while(hr==DXGI_ERROR_WAS_STILL_DRAWING && GetTickCount64()<end);
        if(frame<2) {
            CK(hr==S_OK);HR(set(dev,nullptr,33333,3,0));HR(fw(dev,2,5000,nullptr,0,&token));HR(test_bridge_count(q.Get(),&count));CK(count==before+frame+1);
            CK(present(q.Get(),token,surface)==DXGI_ERROR_INVALID_CALL);
            UINT64 foreignToken;HR(fw(dev,0,1000,nullptr,0,&foreignToken));HR(test_tagged(foreign.Get(),1,sl,foreignToken,0));
            CK(test_tagged(foreign.Get(),1,bl,foreignToken,3)==E_ACCESSDENIED);
            printf("PASS cross-queue writer rejected primary=%p foreignToken=%llu\n",surface,foreignToken);
            printf("PASS private Present frame=%u creator=%lu submitter=%lu hwnd=%p token=%llu count=%u\n",frame,tid,GetCurrentThreadId(),gui.hwnd,token,count);Sleep(1600);
        } else {
            CK(hr==DXGI_ERROR_INVALID_CALL && fw(dev,2,0,nullptr,0,&token)==(frame==2?DXGI_ERROR_INVALID_CALL:DXGI_ERROR_WAIT_TIMEOUT));
            if(frame==2)puts("PASS changed period and changed interval invalidate; retired old batch cannot present after restore");
            if(frame==3){printf("PASS RTV recycled to sibling and back; rejected old recorded serial\n");}
            if(frame==4) CK(xs_display_queue(q.Get()) && test_tagged(q.Get(),1,sl,token,0)==E_ACCESSDENIED && present(q.Get(),token,surface)==E_ACCESSDENIED);
            ComPtr<IUnknown> done;HR(xs_shared_fence(d,done.GetAddressOf()));
            using Signal=HRESULT(WINAPI*)(IUnknown*,IUnknown*,UINT64);HR(SLOT(q.Get(),0x80,Signal)(q.Get(),done.Get(),1));
            HANDLE e=CreateEventW(nullptr,FALSE,FALSE,nullptr);CK(e);HR(xs_fence_event(done.Get(),1,e));CK(WaitForSingleObject(e,10000)==WAIT_OBJECT_0 && ticks[0]>ticks[1]);CloseHandle(e);
        }
    }
#endif
    DWORD code;CK(GetExitCodeThread(thread,&code) && !code);
    CloseHandle(thread);CloseHandle(gui.changed);CloseHandle(gui.ready);CloseHandle(gui.destroy);CloseHandle(gui.quit);
#ifndef TEST_ORDERED_PRESENT
    puts("PASS scoped private WSI: gated batch; stale window, foreign queue, wrong token/primary and genuine raw exposure rejected");
#endif
    return 0;
}
