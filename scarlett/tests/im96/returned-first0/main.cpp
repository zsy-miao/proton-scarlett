#include "test-clear-common.hpp"
#include "test-colour-root.hpp"
#include <vector>
static std::vector<BYTE> file(const char *name,size_t n){std::vector<BYTE>b(n);load(name,b.data(),n);return b;}
#include "used-table.hpp"
using Create=HRESULT(WINAPI*)(IUnknown*,const D3D12_PIPELINE_STATE_STREAM_DESC*,REFIID,void**);
extern "C" {
ID3D12GraphicsCommandList7 *begin(IUnknown*,ID3D12Device*);
ID3D12GraphicsCommandList7 *returned(used_proof*,IUnknown*,ID3D12Resource*,SIZE_T,SIZE_T,SIZE_T,
        ID3D12CommandQueue*,ID3D12Fence*,ID3D12Fence*,IUnknown*,IUnknown*,IUnknown*);
IUnknown *test_dsv_heap(IUnknown*,SIZE_T*);
ID3D12Resource *oracle(void);
void record96(used_proof*,used_binding*,IUnknown*,IUnknown*),finish_record(void),retire(void),table_emitted(void);
BOOL submit96(ID3D12CommandQueue*);
}
static UsedTable *lease;
extern "C" HRESULT materialize_original(const used_proof *p,ID3D12GraphicsCommandList7 *l){
    if(!lease || p!=&lease->u || !test_used_current(p))return E_INVALIDARG;
    ComPtr<ID3D12Device> d;HR(l->GetDevice(IID_PPV_ARGS(&d)));
    lease->materialize(d.Get(),l);HR(lease->consume(l));table_emitted();return S_OK;
}
static Create create;
static IUnknown *owner;
static UINT_PTR q[86];
static HRESULT publish(IUnknown **p){
    D3D12_PIPELINE_STATE_STREAM_DESC sd={sizeof q,q};
    return create(owner,&sd,IID_ID3D12PipelineState,(void**)p);
}
int main(){
    setvbuf(stdout,nullptr,_IONBF,0);puts("RETURNED96 first-vertex-0 synthetic fixture");
    auto d=device(),r=root(d.Get()),alias=colour_root(d.Get());
    owner=d.Get();create=(Create)(*(void***)owner)[0x2b0/8];
    auto host=xs_bvh_root_host(owner,r.Get());CK(host);
    ComPtr<ID3D12Device> hd;HR(host->GetDevice(IID_PPV_ARGS(&hd)));
    BYTE v[1712],p[1588];load("stream.bin",q,sizeof q);load("vs.bin",v,992);load("ps.bin",p,964);
    q[1]=(UINT_PTR)r.Get();q[3]=(UINT_PTR)v;q[6]=(UINT_PTR)p;q[85]=((UINT64)20<<32)|16;
    ComPtr<IUnknown> clear;HR(publish(clear.GetAddressOf()));CK(clear.Get());
    D3D12_INPUT_ELEMENT_DESC ia[]={{"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
        {"COLOR",0,DXGI_FORMAT_R8G8B8A8_UNORM,0,12,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
        {"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,16,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0}};
    load("fixed96.bin",q,sizeof q);load("vs96.bin",v,sizeof v);load("ps96.bin",p,sizeof p);
    q[1]=(UINT_PTR)alias.Get();q[3]=(UINT_PTR)v;q[6]=(UINT_PTR)p;q[77]=(UINT_PTR)ia;
    IUnknown *bad=r.Get();q[61]^=1;CK(publish(&bad)==E_NOTIMPL && !bad);q[61]^=1;
    q[1]=(UINT_PTR)r.Get();CK(publish(&bad)==E_INVALIDARG && !bad);q[1]=(UINT_PTR)alias.Get();
    auto l=begin(owner,hd.Get());
    UsedTable used(owner);lease=&used;used.setup(hd.Get(),l);
    ComPtr<IUnknown> heap;UINT desc[]={2,4096,0,1};
    HR(SLOT(owner,0x70,Make)(owner,desc,IID_ID3D12DescriptorHeap,(void**)heap.GetAddressOf()));
    SIZE_T rtv=SLOT(heap.Get(),0x48,Start)(heap.Get())+32;
    const DWORD wire[]={28,4,0,0},dw[]={20,3,0,0,0,0};HR(xs_write_view(owner,0xa0,used.texture_key.Get(),wire,rtv));
    SIZE_T dv=0;ComPtr<IUnknown> ds_heap,depth;ds_heap.Attach(test_dsv_heap(owner,&dv));CK(ds_heap);
    depth.Attach(test_sr_image(owner,used.texture_key.Get()));CK(depth);HR(xs_write_view(owner,0xa8,depth.Get(),dw,dv));
    D3D12_COMMAND_QUEUE_DESC qd={};ComPtr<ID3D12CommandQueue> queue;HR(hd->CreateCommandQueue(&qd,IID_PPV_ARGS(&queue)));
    ComPtr<ID3D12Fence> gate,end,done;
    for(auto f:{gate.GetAddressOf(),end.GetAddressOf(),done.GetAddressOf()})HR(hd->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(f)));
    memset((void*)(used.u.base+0x3df10),0xab,4080);MemoryBarrier();
    l=returned(&used.u,depth.Get(),test_sr_host(depth.Get()),rtv,dv,rtv+32,queue.Get(),gate.Get(),end.Get(),clear.Get(),r.Get(),alias.Get());
    ComPtr<IUnknown> wrapper;HR(publish(wrapper.GetAddressOf()));used.produce();
    record96(&used.u,&used.binding,wrapper.Get(),clear.Get());used.attached();wrapper.Reset();
    ID3D12Resource *output=oracle();CK(output && output!=used.texture.Get());
    D3D12_RESOURCE_DESC rd=output->GetDesc();CK(rd.Width==3840 && rd.Height==2160 && rd.Format==DXGI_FORMAT_R8G8B8A8_UNORM);
    UINT64 bytes;D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp;hd->GetCopyableFootprints(&rd,0,1,0,&fp,nullptr,nullptr,&bytes);
    D3D12_HEAP_PROPERTIES hp={};hp.Type=D3D12_HEAP_TYPE_READBACK;hp.CreationNodeMask=hp.VisibleNodeMask=1;
    UINT64 second=(bytes+511)&~511ull;
    rd={};rd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;rd.Width=second+bytes;rd.Height=rd.MipLevels=rd.DepthOrArraySize=rd.SampleDesc.Count=1;rd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> rb;HR(hd->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&rb)));
    D3D12_RESOURCE_BARRIER b={};b.Transition={output,0,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE};l->ResourceBarrier(1,&b);
    D3D12_TEXTURE_COPY_LOCATION to={},from={};to.pResource=rb.Get();to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;to.PlacedFootprint=fp;
    from.pResource=output;from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;l->CopyTextureRegion(&to,0,0,0,&from,nullptr);
    D3D12_TEXTURE_BARRIER src={};src.pResource=used.texture.Get();src.Subresources.IndexOrFirstMipLevel=UINT_MAX;
    src.SyncBefore=D3D12_BARRIER_SYNC_ALL_SHADING;src.AccessBefore=D3D12_BARRIER_ACCESS_SHADER_RESOURCE;src.LayoutBefore=D3D12_BARRIER_LAYOUT_SHADER_RESOURCE;
    src.SyncAfter=D3D12_BARRIER_SYNC_COPY;src.AccessAfter=D3D12_BARRIER_ACCESS_COPY_SOURCE;src.LayoutAfter=D3D12_BARRIER_LAYOUT_COPY_SOURCE;
    D3D12_BARRIER_GROUP group={};group.Type=D3D12_BARRIER_TYPE_TEXTURE;group.NumBarriers=1;group.pTextureBarriers=&src;l->Barrier(1,&group);
    from.pResource=used.texture.Get();to.PlacedFootprint.Offset=second;l->CopyTextureRegion(&to,0,0,0,&from,nullptr);
    used.copy_checks(l);finish_record();
    CK(end->GetCompletedValue()==0);++used.binding.epoch;CK(!submit96(queue.Get()));--used.binding.epoch;
    *(UINT64*)used.u.cb_slot^=16;CK(!submit96(queue.Get()));*(UINT64*)used.u.cb_slot^=16;
    CK(submit96(queue.Get()) && !submit96(queue.Get()));HR(queue->Signal(done.Get(),1));
    HANDLE ev=CreateEventW(nullptr,FALSE,FALSE,nullptr);CK(ev);HR(done->SetEventOnCompletion(1,ev));CK(WaitForSingleObject(ev,0)==WAIT_TIMEOUT);
    struct Vertex{float x,y,z;DWORD rgba;float u,v;};static_assert(sizeof(Vertex)==24);
    const Vertex vb[]={{1,0,0,0xffffffff,.75f,.5f},{1,3,0,0xffffffff,.75f,.5f},{4,3,0,0xffffffff,.75f,.5f}};
    memcpy((void*)(used.u.base+0x3df10),vb,sizeof vb);MemoryBarrier();used.replace();memset((void*)(used.u.base+0x3df10),0x5e,4080);
    HR(gate->Signal(1));CK(WaitForSingleObject(ev,10000)==WAIT_OBJECT_0 && done->GetCompletedValue()==1);CloseHandle(ev);HR(hd->GetDeviceRemovedReason());
    used.verify();BYTE *pixels;HR(rb->Map(0,nullptr,(void**)&pixels));
    CK(end->GetCompletedValue()==1);
    for(UINT surface=0;surface<2;surface++)for(UINT y=0;y<2160;y++)for(UINT x=0;x<3840;x++){
        const BYTE green[]={0,255,0,255},magenta[]={255,0,255,255},black[]={0,0,0,0};
        const BYTE *want=surface?(x<1920?magenta:green):((x>=6 && x<12 && y>=4 && y<10)?green:black);
        const BYTE *got=pixels+(surface?second:fp.Offset)+y*fp.Footprint.RowPitch+4*x;
        if(memcmp(got,want,4)){fprintf(stderr,"FAIL returned96 surface=%u (%u,%u): %u %u %u %u\n",surface,x,y,got[0],got[1],got[2],got[3]);return 1;}
    }
    rb->Unmap(0,nullptr);retire();lease=nullptr;
    puts("PASS returned96 fenced: 36 green target pixels, 8294364 transparent guards, both source halves unchanged");return 0;
}
