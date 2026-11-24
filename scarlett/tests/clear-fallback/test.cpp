#include "../../native/test-clear-common.hpp"
#include "emitter.hpp"
#include "recorder.hpp"
extern "C" {
HRESULT clear_test_write_rtv(IUnknown*,ID3D12Resource*,SIZE_T);
HRESULT xs_resolve_color_rtv(IUnknown*,SIZE_T,D3D12_CPU_DESCRIPTOR_HANDLE*,ID3D12Resource**);
HRESULT xs_bind_view_buffer(IUnknown*,IUnknown*,ID3D12Resource*,void**);
void xs_unbind_view_buffer(void*);
HRESULT xs_allocator(ID3D12Device*,D3D12_COMMAND_LIST_TYPE,IUnknown**);
HRESULT xs_startup_list(ID3D12Device*,D3D12_COMMAND_LIST_TYPE,IUnknown*,BOOL,IUnknown**);
ID3D12GraphicsCommandList7 *xs_list_host(IUnknown*);
HRESULT xs_direct_queue(ID3D12Device*,IUnknown**);
HRESULT xs_shared_fence(ID3D12Device*,IUnknown**);
HRESULT xs_queue_wait(IUnknown*,IUnknown*,UINT64);
HRESULT xs_cpu_signal(IUnknown*,UINT64);
HRESULT xs_fence_event(IUnknown*,UINT64,HANDLE);
}
template<class F> static F slot(IUnknown *i,size_t byte){return (F)(*(void***)i)[byte/8];}
static ComPtr<IUnknown> pso(IUnknown*d,IUnknown*r){
    UINT_PTR stream[0x2b0/8];BYTE vs[992],ps[964];load("stream.bin",stream,sizeof stream);load("vs.bin",vs,sizeof vs);load("ps.bin",ps,sizeof ps);
    stream[1]=(SIZE_T)r;stream[3]=(SIZE_T)vs;stream[6]=(SIZE_T)ps; D3D12_PIPELINE_STATE_STREAM_DESC sd={sizeof stream,stream};
    ComPtr<IUnknown> p; HR(slot<HRESULT(*)(IUnknown*,const void*,REFIID,void**)>(d,0x2b0)(d,&sd,IID_ID3D12PipelineState,(void**)p.GetAddressOf()));return p;
}
static ComPtr<ID3D12Resource> resource(ID3D12Device*d,D3D12_HEAP_TYPE t){
    D3D12_HEAP_PROPERTIES hp={};hp.Type=t;hp.CreationNodeMask=hp.VisibleNodeMask=1;
    D3D12_RESOURCE_DESC rd={};rd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;rd.Width=t==D3D12_HEAP_TYPE_READBACK?32768:256;
    rd.Height=rd.DepthOrArraySize=rd.MipLevels=rd.SampleDesc.Count=1;rd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    auto state=t==D3D12_HEAP_TYPE_UPLOAD?D3D12_RESOURCE_STATE_GENERIC_READ:D3D12_RESOURCE_STATE_COPY_DEST;
    if(t==D3D12_HEAP_TYPE_DEFAULT){rd.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;rd.Width=rd.Height=40;rd.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        rd.Format=DXGI_FORMAT_R8G8B8A8_UNORM;rd.Layout=D3D12_TEXTURE_LAYOUT_UNKNOWN;state=D3D12_RESOURCE_STATE_RENDER_TARGET;}
    ComPtr<ID3D12Resource> o;HR(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,state,nullptr,IID_PPV_ARGS(&o)));return o;
}
int main(){
    Emitter writer;ClearBindings b{};b.device=device();b.root=root(b.device.Get());b.pso=pso(b.device.Get(),b.root.Get());
    ComPtr<ID3D12Device> d;HR(clear_test_host(b.pso.Get())->GetDevice(IID_PPV_ARGS(&d)));
    auto foreign=device(),otherRoot=root(foreign.Get()),otherPSO=pso(foreign.Get(),otherRoot.Get());
    auto sameRoot=root(b.device.Get());auto d2=resource(d.Get(),D3D12_HEAP_TYPE_DEFAULT);auto readback=resource(d.Get(),D3D12_HEAP_TYPE_READBACK);
    b.target=resource(d.Get(),D3D12_HEAP_TYPE_DEFAULT);b.source=resource(d.Get(),D3D12_HEAP_TYPE_UPLOAD);b.generation=b.source.Get();
    b.alias=0x1234567000ull;b.offset=144; void *binding=nullptr;HR(xs_bind_view_buffer(b.device.Get(),b.generation.Get(),b.source.Get(),&binding));
    using Heap=HRESULT(*)(IUnknown*,const void*,REFIID,void**); DWORD desc[]={2,4096,0,1};
    HR(slot<Heap>(b.device.Get(),0x70)(b.device.Get(),desc,IID_ID3D12DescriptorHeap,(void**)b.heap.GetAddressOf()));
    b.slot=slot<SIZE_T(*)(IUnknown*)>(b.heap.Get(),0x48)(b.heap.Get())+32;
    HR(clear_test_write_rtv(b.device.Get(),b.target.Get(),b.slot));b.serial=*(UINT64*)b.slot;
    ComPtr<IUnknown> alloc,list,queue,gate,done;
    HR(xs_allocator(d.Get(),D3D12_COMMAND_LIST_TYPE_DIRECT,alloc.GetAddressOf()));HR(xs_startup_list(d.Get(),D3D12_COMMAND_LIST_TYPE_DIRECT,alloc.Get(),TRUE,list.GetAddressOf()));
    auto l=xs_list_host(list.Get());CK(l);HR(xs_direct_queue(d.Get(),queue.GetAddressOf()));HR(xs_shared_fence(d.Get(),gate.GetAddressOf()));HR(xs_shared_fence(d.Get(),done.GetAddressOf()));
    auto clearTarget=[&](SIZE_T s,const float (&v)[4]){D3D12_CPU_DESCRIPTOR_HANDLE h;ComPtr<ID3D12Resource> rt;HR(xs_resolve_color_rtv(b.heap.Get(),s,&h,rt.GetAddressOf()));l->ClearRenderTargetView(h,v,0,nullptr);};
    clearTarget(b.slot,{1,0,1,1});HR(clear_test_write_rtv(b.device.Get(),d2.Get(),b.slot+32));clearTarget(b.slot+32,{0,1,0,1});
    BYTE *arena; D3D12_RANGE none={}; HR(b.source->Map(0,&none,(void**)&arena));
    float rgba[]={.125f,.375f,.625f,.875f};auto run=[&](ClearRecorder& rec,IUnknown*p,IUnknown*r,UINT64 alias){CK(rec.guest);writer.run(rec.guest,p,r,b.slot,arena,alias,rgba);};
    unsigned rejects=0;auto bad=[&](ClearBindings seed,IUnknown*p,IUnknown*r,UINT64 alias){ClearRecorder rec(seed,l);run(rec,p,r,alias);CK(FAILED(rec.status)&&!rec.snapshot&&!rec.privateRTV);++rejects;};
    bad(b,otherPSO.Get(),b.root.Get(),b.alias);bad(b,b.pso.Get(),sameRoot.Get(),b.alias);bad(b,b.pso.Get(),otherRoot.Get(),b.alias);
    bad(b,b.pso.Get(),b.root.Get(),b.alias+16);
    {auto s=b;s.device=foreign;bad(s,b.pso.Get(),b.root.Get(),b.alias);s=b;s.serial++;bad(s,b.pso.Get(),b.root.Get(),b.alias);s=b;s.pso=otherPSO;bad(s,otherPSO.Get(),b.root.Get(),b.alias);}
    HR(clear_test_write_rtv(b.device.Get(),d2.Get(),b.slot));bad(b,b.pso.Get(),b.root.Get(),b.alias);
    HR(clear_test_write_rtv(b.device.Get(),b.target.Get(),b.slot));b.serial=*(UINT64*)b.slot;*(UINT64*)b.slot^=1;bad(b,b.pso.Get(),b.root.Get(),b.alias);*(UINT64*)b.slot^=1;
    xs_unbind_view_buffer(binding);bad(b,b.pso.Get(),b.root.Get(),b.alias);HR(xs_bind_view_buffer(b.device.Get(),b.generation.Get(),b.source.Get(),&binding));
    {ClearRecorder rec(b,l);ClearRecorder::draw(rec.guest,3,1,0,0);CK(FAILED(rec.status));++rejects;}
    rgba[1]=-1;bad(b,b.pso.Get(),b.root.Get(),b.alias);rgba[1]=.375f;
    ClearRecorder retained(b,l);auto keep=&retained;run(*keep,b.pso.Get(),b.root.Get(),b.alias);HR(keep->status);CK(keep->step==9 && keep->snapshot);
    // Independently verify the immutable GPU binding contains the producer's exact 100 bytes.
    BYTE *snapshot;HR(keep->snapshot->Map(0,nullptr,(void**)&snapshot));CK(!memcmp(snapshot,arena+144,100));keep->snapshot->Unmap(0,&none);
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprints[2]; ID3D12Resource *targets[]={b.target.Get(),d2.Get()};
    for(int i=0;i<2;i++) {auto rt=targets[i];D3D12_RESOURCE_BARRIER barrier={};barrier.Transition={rt,0,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE};l->ResourceBarrier(1,&barrier);
        auto rd=rt->GetDesc();d->GetCopyableFootprints(&rd,0,1,i*16384,&footprints[i],nullptr,nullptr,nullptr);
        D3D12_TEXTURE_COPY_LOCATION dst={},from={};dst.pResource=readback.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=footprints[i];from.pResource=rt;l->CopyTextureRegion(&dst,0,0,0,&from,nullptr);}
    HR(slot<HRESULT(*)(IUnknown*)>(list.Get(),0x48)(list.Get()));HR(xs_queue_wait(queue.Get(),gate.Get(),1));
    IUnknown *one=list.Get();slot<void(*)(IUnknown*,UINT,IUnknown*const*,UINT64,UINT)>(queue.Get(),0x118)(queue.Get(),1,&one,0,0);
    HR(slot<HRESULT(*)(IUnknown*,IUnknown*,UINT64)>(queue.Get(),0x80)(queue.Get(),done.Get(),1));HANDLE ev=CreateEventW(nullptr,FALSE,FALSE,nullptr);CK(ev);HR(xs_fence_event(done.Get(),1,ev));CK(WaitForSingleObject(ev,0)==WAIT_TIMEOUT);
    // Replay address & descriptor with different owners while the GPU is blocked.
    memset(arena+144,0xff,100);b.source->Unmap(0,nullptr);xs_unbind_view_buffer(binding);
    auto replacement=resource(d.Get(),D3D12_HEAP_TYPE_UPLOAD);HR(xs_bind_view_buffer(b.device.Get(),replacement.Get(),replacement.Get(),&binding));
    auto savedPSO=b.pso.Get(),savedRoot=b.root.Get();auto savedRT=b.target.Get(),savedSrc=b.source.Get();
    BYTE expectedRecord[100]; HR(keep->snapshot->Map(0,nullptr,(void**)&snapshot));memcpy(expectedRecord,snapshot,100);keep->snapshot->Unmap(0,&none);
    HR(clear_test_write_rtv(b.device.Get(),d2.Get(),b.slot));b={};
    CK(keep->b.pso && keep->b.root && keep->b.target && keep->b.source);HR(xs_cpu_signal(gate.Get(),1));CK(WaitForSingleObject(ev,10000)==WAIT_OBJECT_0);CloseHandle(ev);
    BYTE *data;HR(readback->Map(0,nullptr,(void**)&data));unsigned hits=0;
    for(unsigned i=0;i<2;i++)for(unsigned y=0;y<40;y++)for(unsigned x=0;x<40;x++){
        bool hit=!i && x>=3 && x<19 && y>=4 && y<20;DWORD expected=i?0xff00ff00:hit?0xdf9f6020:0xffff00ff;
        DWORD got;memcpy(&got,data+footprints[i].Offset+footprints[i].Footprint.RowPitch*y+4*x,4);
        if(got!=expected){fprintf(stderr,"pixel %u %u,%u = %08lx vs %08lx\n",i,x,y,got,expected);return 1;}hits+=hit;}
    readback->Unmap(0,&none);CK(hits==256);
    // The GPU has retired. Probe identities, then release the adapter's last guest references.
    ComPtr<IUnknown> ident;HR(keep->b.pso->QueryInterface(IID_ID3D12PipelineState,(void**)ident.GetAddressOf()));CK(ident.Get()==keep->b.pso.Get());ident.Reset();
    CK(keep->b.pso.Get()==savedPSO && keep->b.root.Get()==savedRoot && keep->b.source.Get()==savedSrc && keep->b.target.Get()==savedRT);
    CK(clear_test_host(savedPSO) && xs_bvh_root_host(keep->b.device.Get(),savedRoot));
    HR(keep->snapshot->Map(0,nullptr,(void**)&snapshot)); CK(!memcmp(snapshot,expectedRecord,sizeof expectedRecord));keep->snapshot->Unmap(0,&none);
    keep->b={};xs_unbind_view_buffer(binding);
    printf("PASS original 122630, nine typed calls, 100 title-written bytes; %u rejection controls; gated flags=0 submit, %u clear pixels, 1344 guards, 1600 alternate target pixels\n",rejects,hits);
}
