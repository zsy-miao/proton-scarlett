#include <d3d12.h>
#include <wrl/client.h>
#include <cstdio>
#include <cstdlib>
using Microsoft::WRL::ComPtr;
#define CK(x) do { if (!(x)) {fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);} } while(0)
#define HR(x) CK(SUCCEEDED(x))
#define SLOT(o,n,T) ((T)((*reinterpret_cast<void***>(o))[n/8]))
extern "C" HRESULT WINAPI D3D12XboxCreateDevice(void *,const void *,REFIID,void **);
extern "C" HRESULT xs_resolve_sampler(IUnknown *,SIZE_T,ID3D12DescriptorHeap **,D3D12_GPU_DESCRIPTOR_HANDLE *);
extern "C" HRESULT xs_write_sampler(IUnknown *,const void *,SIZE_T);

int main() {
    DWORD params[20]={};params[0]=0x20004;params[2]=0x100000;params[10]=1;params[15]=0x20;
    const IID boot={0x5405c344,0xd457,0x444e,{0xb4,0xdd,0x23,0x66,0xe4,0x5a,0xee,0x39}};
    ComPtr<IUnknown> guest,cpu,visible;
    HR(D3D12XboxCreateDevice(nullptr,params,boot,(void**)guest.GetAddressOf()));
    using Make=HRESULT(WINAPI*)(IUnknown*,const void*,REFIID,void**);
    using Start=SIZE_T(WINAPI*)(IUnknown*);
    using Write=void(WINAPI*)(IUnknown*,const void*,SIZE_T);
    using Copy=void(WINAPI*)(IUnknown*,UINT,SIZE_T,SIZE_T,UINT);
    UINT shape[]={1,128,0,1};auto heap=SLOT(guest.Get(),0x70,Make);
    HR(heap(guest.Get(),shape,IID_ID3D12DescriptorHeap,(void**)cpu.GetAddressOf()));shape[2]=1;
    HR(heap(guest.Get(),shape,IID_ID3D12DescriptorHeap,(void**)visible.GetAddressOf()));
    SIZE_T src=SLOT(cpu.Get(),0x48,Start)(cpu.Get()),dst=SLOT(visible.Get(),0x48,Start)(visible.Get());
    // Captured slot order: linear wrap, point wrap, point clamp-W, linear clamp-W.
    DWORD words[23]={0,1,1,2,2,0,1,1,1,0,0x41400000,0,0,1,0,0,1};
    for(UINT i=0;i<4;i++) {
        bool point=i==1 || i==2;
        words[1]=words[2]=point?2:1;words[3]=words[4]=point?1:2;words[8]=i<2?1:3;
        SLOT(guest.Get(),0x1e8,Write)(guest.Get(),words,src+i*16);
        UINT64 tag[2];memcpy(tag,(void*)(src+i*16),sizeof tag);
        for(auto &w:words){w^=1;CK(xs_write_sampler(guest.Get(),words,src+i*16)==E_NOTIMPL);w^=1;}
        CK(!memcmp(tag,(void*)(src+i*16),sizeof tag));
        SLOT(guest.Get(),0xc0,Copy)(guest.Get(),1,dst+i*16,src+i*16,1);
    }
    cpu.Reset();
    ID3D12DescriptorHeap *sam;D3D12_GPU_DESCRIPTOR_HANDLE h;
    HR(xs_resolve_sampler(visible.Get(),dst,&sam,&h));
    ComPtr<ID3D12Device> d;HR(sam->GetDevice(IID_PPV_ARGS(&d)));
    ComPtr<ID3D12CommandQueue> q;D3D12_COMMAND_QUEUE_DESC qd={};HR(d->CreateCommandQueue(&qd,IID_PPV_ARGS(&q)));
    ComPtr<ID3D12CommandAllocator> a;ComPtr<ID3D12GraphicsCommandList> l;
    HR(d->CreateCommandAllocator(qd.Type,IID_PPV_ARGS(&a)));HR(d->CreateCommandList(0,qd.Type,a.Get(),nullptr,IID_PPV_ARGS(&l)));
    D3D12_RESOURCE_DESC r={};r.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE3D;r.Width=r.Height=r.DepthOrArraySize=2;
    r.Format=DXGI_FORMAT_R32_FLOAT;r.MipLevels=r.SampleDesc.Count=1;
    auto resource=[&](D3D12_HEAP_TYPE type,D3D12_RESOURCE_STATES state){
        D3D12_HEAP_PROPERTIES hp={};hp.Type=type;hp.CreationNodeMask=hp.VisibleNodeMask=1;ComPtr<ID3D12Resource> p;
        HR(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&r,state,nullptr,IID_PPV_ARGS(&p)));return p;
    };
    auto tex=resource(D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_COPY_DEST);
    r.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;r.Width=1024;r.Format=DXGI_FORMAT_UNKNOWN;r.Height=r.DepthOrArraySize=1;r.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    auto up=resource(D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);float *v;
    HR(up->Map(0,nullptr,(void**)&v));
    for(UINT z=0;z<2;z++)for(UINT y=0;y<2;y++)for(UINT x=0;x<2;x++)v[z*128+y*64+x]=1+x+2*y+4*z;
    up->Unmap(0,nullptr);
    D3D12_TEXTURE_COPY_LOCATION from={},to={};from.pResource=up.Get();from.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    from.PlacedFootprint.Footprint={DXGI_FORMAT_R32_FLOAT,2,2,2,256};to.pResource=tex.Get();l->CopyTextureRegion(&to,0,0,0,&from,nullptr);
    auto transition=[&](ID3D12Resource *p,D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after){
        D3D12_RESOURCE_BARRIER b={};b.Transition={p,0,before,after};l->ResourceBarrier(1,&b);
    };
    transition(tex.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    r.Width=48;auto read=resource(D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
    r.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;auto out=resource(D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    ComPtr<ID3D12DescriptorHeap> srv;D3D12_DESCRIPTOR_HEAP_DESC hd={D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,1,D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,1};
    HR(d->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&srv)));d->CreateShaderResourceView(tex.Get(),nullptr,srv->GetCPUDescriptorHandleForHeapStart());
    BYTE dxil[8192];auto f=fopen("test-w-clamp.dxil","rb");CK(f);
    size_t n=fread(dxil,1,sizeof dxil,f);CK(n && feof(f));fclose(f);
    ComPtr<ID3D12RootSignature> root;HR(d->CreateRootSignature(0,dxil,n,IID_PPV_ARGS(&root)));
    D3D12_COMPUTE_PIPELINE_STATE_DESC ps={};ps.pRootSignature=root.Get();ps.CS={dxil,n};ComPtr<ID3D12PipelineState> pso;
    HR(d->CreateComputePipelineState(&ps,IID_PPV_ARGS(&pso)));l->SetPipelineState(pso.Get());l->SetComputeRootSignature(root.Get());
    ID3D12DescriptorHeap *heaps[]={srv.Get(),sam};l->SetDescriptorHeaps(2,heaps);l->SetComputeRootDescriptorTable(0,srv->GetGPUDescriptorHandleForHeapStart());
    l->SetComputeRootDescriptorTable(1,h);l->SetComputeRootUnorderedAccessView(2,out->GetGPUVirtualAddress());l->Dispatch(1,1,1);
    transition(out.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);l->CopyResource(read.Get(),out.Get());
    HR(l->Close());ID3D12CommandList *batch[]={l.Get()};q->ExecuteCommandLists(1,batch);
    ComPtr<ID3D12Fence> fence;HR(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));HR(q->Signal(fence.Get(),1));
    HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);CK(event);HR(fence->SetEventOnCompletion(1,event));CK(WaitForSingleObject(event,10000)==WAIT_OBJECT_0);CloseHandle(event);
    HR(read->Map(0,nullptr,(void**)&v));
    for(UINT i=0;i<3;i++)printf("probe %u: point wrap/clampW %.2f/%.2f, linear wrap/clampW %.2f/%.2f\n",i,v[i*4],v[i*4+1],v[i*4+2],v[i*4+3]);
    const float expected[]={1,5,2,5, 8,4,8,4, 3,3,3.75f,3.75f};CK(!memcmp(v,expected,sizeof expected));read->Unmap(0,nullptr);
    puts("PASS +1e8/+c0 slots 2/3: point and linear W-clamp, wrap controls, rejected record mutations");
}
