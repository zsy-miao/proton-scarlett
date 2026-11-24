/* Included in the display allocation fixture: use its real primary, descriptor
 * serial and retained backing. Additional copies are instrumented producer work. */
#include <initializer_list>
#ifdef TEST_ORDERED_PRESENT
static constexpr UINT ow=3840,oh=2160;
#else
static constexpr UINT ow=2,oh=1;
#endif
extern "C" {
HRESULT xs_ordered_record(IUnknown *,HRESULT (*)(IUnknown *,void *),void *,IUnknown *,IUnknown *,IUnknown **);
HRESULT xs_ordered_submit(IUnknown *,UINT,IUnknown *const *,UINT64,UINT,ID3D12Fence **);
HRESULT xs_frame_submission_query(IUnknown *,UINT64,ID3D12CommandQueue *,BOOL);
HRESULT test_ordered_ineligible(IUnknown *,void *);
HRESULT test_tagged(IUnknown *,UINT,IUnknown *const *,UINT64,UINT);
ID3D12GraphicsCommandList7 *test_clear_recorder(IUnknown *);
UINT test_ordered_host_submissions();
extern const IID xs_sync_list_id;
#ifdef TEST_SAME_BATCH_RELEASE
HRESULT test_export_rt(IUnknown *,UINT64 *);
HRESULT test_record_successor(IUnknown *,IUnknown *,IUnknown *,SIZE_T,UINT64,const D3D12_TEXTURE_BARRIER *,IUnknown **);
HRESULT test_intervening_write(IUnknown *,IUnknown *,SIZE_T);
#endif
}

struct OrderedChunk final : IUnknown {
    LONG refs=1;
    HANDLE retired;
    ComPtr<ID3D12Resource> upload,intermediate,read,texture;
    ComPtr<IUnknown> views;
    IUnknown *key;
    SIZE_T slot;
#ifdef TEST_PAIR_SPLIT
    IUnknown **heaps;
    BYTE *head;
    volatile UINT *ctrl;
#endif
#ifdef TEST_SAME_BATCH_RELEASE
    UINT64 serial;
#endif
    HRESULT WINAPI QueryInterface(REFIID iid,void **out) override {
        if (!out) return E_POINTER;
        *out=nullptr;if(iid!=IID_IUnknown)return E_NOINTERFACE;
        AddRef();*out=this;return S_OK;
    }
    ULONG WINAPI AddRef() override {return InterlockedIncrement(&refs);}
    ULONG WINAPI Release() override {
        LONG n=InterlockedDecrement(&refs);
        if (!n) {HANDLE event=retired;delete this;SetEvent(event);}
        return n;
    }
};

static HRESULT ordered_copy(IUnknown *list,void *ctx,bool last)
{
    auto &c=*static_cast<OrderedChunk*>(ctx);
    auto *l=test_clear_recorder(list); // private fixture instrumentation, never a guest borrow
    auto transition=[&](ID3D12Resource *r,D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after){
        D3D12_RESOURCE_BARRIER b={};b.Transition={r,0,before,after};l->ResourceBarrier(1,&b);
    };
    if (!last) {
        transition(c.intermediate.Get(),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_DEST);
        l->CopyBufferRegion(c.intermediate.Get(),0,c.upload.Get(),0,ow*oh*4);
        transition(c.intermediate.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COMMON);
        return S_OK;
    }
    using Barrier=void(WINAPI*)(IUnknown *,UINT,const D3D12_BARRIER_GROUP *);
    using Clear=void(WINAPI*)(IUnknown *,SIZE_T,const FLOAT *,UINT,const RECT *);
    D3D12_TEXTURE_BARRIER b={D3D12_BARRIER_SYNC_ALL_SHADING,D3D12_BARRIER_SYNC_RENDER_TARGET,
        D3D12_BARRIER_ACCESS_UNORDERED_ACCESS,D3D12_BARRIER_ACCESS_RENDER_TARGET,
        D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS,D3D12_BARRIER_LAYOUT_RENDER_TARGET,
        (ID3D12Resource*)c.key,{UINT_MAX,0,0,0,0,0},D3D12_TEXTURE_BARRIER_FLAG_NONE};
    D3D12_BARRIER_GROUP g={};g.Type=D3D12_BARRIER_TYPE_TEXTURE;g.NumBarriers=1;g.pTextureBarriers=&b;
    SLOT(list,0x490,Barrier)(list,1,&g);
    FLOAT zero[4]={};SLOT(list,0x1a0,Clear)(list,c.slot,zero,0,nullptr);
    D3D12_TEXTURE_COPY_LOCATION image={},src={},read={};
    image.pResource=c.texture.Get();image.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    src.pResource=c.intermediate.Get();src.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint.Footprint={DXGI_FORMAT_R8G8B8A8_UNORM,ow,oh,1,(ow*4+255)&~255u};
    read=src;read.pResource=c.read.Get();read.PlacedFootprint.Footprint={DXGI_FORMAT_R8G8B8A8_UNORM,2,1,1,256};
    transition(c.texture.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_DEST);
    transition(c.intermediate.Get(),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_SOURCE);
    l->CopyTextureRegion(&image,0,0,0,&src,nullptr);
    transition(c.intermediate.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_COMMON);
    transition(c.texture.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_BOX pixels={0,0,0,2,1,1};l->CopyTextureRegion(&read,0,0,0,&image,&pixels);
    transition(c.texture.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_RENDER_TARGET);
#ifdef TEST_SAME_BATCH_RELEASE
    return test_export_rt(list,&c.serial);
#else
    b.SyncBefore=D3D12_BARRIER_SYNC_RENDER_TARGET;b.SyncAfter=D3D12_BARRIER_SYNC_NONE;
    b.AccessBefore=D3D12_BARRIER_ACCESS_RENDER_TARGET;b.AccessAfter=D3D12_BARRIER_ACCESS_NO_ACCESS;
    b.LayoutBefore=D3D12_BARRIER_LAYOUT_RENDER_TARGET;b.LayoutAfter=(D3D12_BARRIER_LAYOUT)0x1f;
    SLOT(list,0x490,Barrier)(list,1,&g);return S_OK;
#endif
}

#ifndef TEST_ORDERED_PRESENT
static int ordered_fixture(ID3D12Device *d,IUnknown *owner,IUnknown *surface,IUnknown *other,IUnknown *views,SIZE_T slot)
{
    auto *c=new OrderedChunk;
    HANDLE retired=c->retired=CreateEventW(nullptr,TRUE,FALSE,nullptr);CK(retired);
    c->views=views;c->slot=slot;c->key=surface;
    D3D12_CPU_DESCRIPTOR_HANDLE cpu;HR(xs_resolve_color_rtv(views,slot,&cpu,c->texture.GetAddressOf()));
    auto buffer=[d](D3D12_HEAP_TYPE type,D3D12_RESOURCE_STATES state,ID3D12Resource **out){
        D3D12_HEAP_PROPERTIES hp={};hp.Type=type;hp.CreationNodeMask=hp.VisibleNodeMask=1;
        D3D12_RESOURCE_DESC rd={};rd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;rd.Width=512;
        rd.Height=rd.MipLevels=rd.SampleDesc.Count=rd.DepthOrArraySize=1;rd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        return d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,state,nullptr,IID_PPV_ARGS(out));
    };
    HR(buffer(D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ,c->upload.GetAddressOf()));
    HR(buffer(D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_COMMON,c->intermediate.GetAddressOf()));
    HR(buffer(D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST,c->read.GetAddressOf()));
    UINT *words;HR(c->upload->Map(0,nullptr,(void**)&words));words[0]=0xff0000ff;words[1]=0xff00ff00;c->upload->Unmap(0,nullptr);
    using Queue=HRESULT(WINAPI*)(IUnknown*,const D3D12_COMMAND_QUEUE_DESC*,REFIID,void**);
    using Interval=HRESULT(WINAPI*)(IUnknown*,IDXGIOutput*,UINT,UINT,UINT);
    using Schedule=HRESULT(WINAPI*)(IUnknown*,UINT,UINT,const void*,UINT);
    using Wait=HRESULT(WINAPI*)(IUnknown*,UINT,DWORD,const void*,UINT,UINT64*);
    using Alloc=HRESULT(WINAPI*)(IUnknown *,UINT,REFIID,void **);
    using List=HRESULT(WINAPI*)(IUnknown *,const void *,IUnknown *,IUnknown *,REFIID,void **);
    using Close=HRESULT(WINAPI*)(IUnknown *);
    using Reset=HRESULT(WINAPI*)(IUnknown*,IUnknown*,IUnknown*,UINT);
    ComPtr<IUnknown> q,wrong,gate,sync,alloc;
    D3D12_COMMAND_QUEUE_DESC qd={};qd.NodeMask=1;
    for (auto target:{q.GetAddressOf(),wrong.GetAddressOf()})
        HR(SLOT(owner,0x40,Queue)(owner,&qd,IID_ID3D12CommandQueue,(void**)target));
    HR(SLOT(owner,0x270,Interval)(owner,nullptr,16667,1,0));
    for(UINT type:{0u,2u}) HR(SLOT(owner,0x278,Schedule)(owner,type,0,nullptr,0));
    auto wait=SLOT(owner,0x280,Wait);UINT64 token;
    HR(wait(owner,0,1000,nullptr,0,&token));HR(wait(owner,0,1000,nullptr,0,&token));CK(token==2);
    ComPtr<IUnknown> first,last,foreign,junk;
    auto producer=[](IUnknown *l,void *p){return ordered_copy(l,p,false);};
    auto consumer=[](IUnknown *l,void *p){return ordered_copy(l,p,true);};
    for(UINT i=0;i<4;i++)
        CK(FAILED(xs_ordered_record(q.Get(),test_ordered_ineligible,&i,c,nullptr,junk.GetAddressOf())) && !junk);
    HR(xs_ordered_record(q.Get(),producer,c,c,nullptr,first.GetAddressOf()));
    HR(xs_ordered_record(wrong.Get(),producer,c,c,nullptr,foreign.GetAddressOf()));
    HR(xs_ordered_record(q.Get(),consumer,c,c,surface,last.GetAddressOf()));
    IUnknown *ids[]={first.Get(),last.Get()};
    auto submit=[&](ID3D12Fence **end){return xs_ordered_submit(q.Get(),2,ids,token,3,end);};
    CK(submit(nullptr)==DXGI_ERROR_INVALID_CALL); // lacks same-token sync
    HR(SLOT(owner,0x48,Alloc)(owner,0,IID_ID3D12CommandAllocator,(void**)alloc.GetAddressOf()));
    UINT ld[]={0,2,1,0};HR(SLOT(owner,0x248,List)(owner,ld,alloc.Get(),nullptr,xs_sync_list_id,(void**)sync.GetAddressOf()));
    HR(SLOT(sync.Get(),0x48,Close)(sync.Get()));IUnknown *one=sync.Get();HR(test_tagged(q.Get(),1,&one,token,0));
    CK(!xs_list_host(first.Get()) && SLOT(first.Get(),0x458,Reset)(first.Get(),alloc.Get(),nullptr,0)==E_ACCESSDENIED);
    for(auto bad:{foreign.Get(),first.Get(),sync.Get(),(IUnknown*)1}) {
        ids[1]=bad;CK(FAILED(submit(nullptr)) && test_ordered_host_submissions()==0);
    }
    auto *host=test_host_queue(q.Get());
    ids[0]=last.Get();ids[1]=first.Get();CK(FAILED(submit(nullptr)) && test_ordered_host_submissions()==0);
    ids[0]=first.Get();ids[1]=last.Get();
    CK(xs_frame_submission_query(owner,token,host,TRUE)==S_FALSE);
    foreign.Reset();
    HR(xs_shared_fence(d,gate.GetAddressOf()));HR(xs_queue_wait(q.Get(),gate.Get(),1));
    ComPtr<ID3D12Fence> end;HR(submit(end.GetAddressOf()));CK(test_ordered_host_submissions()==1);
    CK(FAILED(submit(nullptr)) && test_ordered_host_submissions()==1);
    ComPtr<ID3D12Resource> read=c->read;
    first.Reset();last.Reset();c->Release();
    CK(other->Release()==0 && surface->Release()>0 && given==1);
    CK(!end->GetCompletedValue() && WaitForSingleObject(retired,0)==WAIT_TIMEOUT);
    CK(xs_frame_submission_query(owner,token,host,TRUE)==E_PENDING);
    HR(xs_cpu_signal(gate.Get(),1));CK(WaitForSingleObject(retired,10000)==WAIT_OBJECT_0);
    CK(given==2 && end->GetCompletedValue()==1 && xs_frame_submission_query(owner,token,host,TRUE)==S_OK);
    CK(wait(owner,2,0,nullptr,0,&token)==DXGI_ERROR_WAIT_TIMEOUT);
    HR(read->Map(0,nullptr,(void**)&words));CK(words[0]==0xff0000ff && words[1]==0xff00ff00);read->Unmap(0,nullptr);
    CloseHandle(retired);puts("PASS ordered token 2: red/green primary pixels, atomic rejection, chunk retired, no flip");return 0;
}
#endif
