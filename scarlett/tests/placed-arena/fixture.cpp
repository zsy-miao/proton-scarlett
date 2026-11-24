#include <d3d12.h>
#include <cstdio>
#include <cstdlib>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
#define HR(x) do { HRESULT h_=(x); if (FAILED(h_)) { std::fprintf(stderr,"%d: %s: %08lx\n",__LINE__,#x,(unsigned long)h_); std::exit(1); } } while(0)
static constexpr UINT A=27u<<20, B=74u<<20, E=174u<<20, reportBytes=E+24;
static constexpr UINT ends[]={0,A,B,E};
static ID3D12Device *device;
static ComPtr<ID3D12Resource> buffer(D3D12_HEAP_TYPE type, UINT bytes, D3D12_RESOURCE_STATES state, bool writable=false) {
    D3D12_HEAP_PROPERTIES hp={}; hp.Type=type; hp.CreationNodeMask=hp.VisibleNodeMask=1;
    D3D12_RESOURCE_DESC d={}; d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER; d.Width=bytes;
    d.Height=d.DepthOrArraySize=d.MipLevels=d.SampleDesc.Count=1; d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if(writable) d.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    ComPtr<ID3D12Resource> r; HR(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&d,state,nullptr,IID_PPV_ARGS(&r))); return r;
}
static void check(UINT at, UINT actual, UINT expect, const char *what) {
    if (actual!=expect) { std::fprintf(stderr,"%s byte %08x: %08x != %08x\n",what,at,actual,expect); std::exit(1); }
}
static UINT seed(UINT p) { return 0x10000000u | (p/4); }
int main() {
    ComPtr<ID3D12Device> d; HR(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&d))); device=d.Get();
    auto up=buffer(D3D12_HEAP_TYPE_UPLOAD,E,D3D12_RESOURCE_STATE_GENERIC_READ);
    auto rb=buffer(D3D12_HEAP_TYPE_READBACK,E+reportBytes,D3D12_RESOURCE_STATE_COPY_DEST);
    auto result=buffer(D3D12_HEAP_TYPE_DEFAULT,reportBytes,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,true);
    ComPtr<ID3D12Resource> parts[3];
    UINT *m; D3D12_RANGE none={0,0};
    HR(up->Map(0,&none,(void**)&m)); for(UINT p=0;p<E;p+=4) m[p/4]=seed(p); up->Unmap(0,nullptr);
    for(UINT i=0;i<3;++i) parts[i]=buffer(D3D12_HEAP_TYPE_DEFAULT,ends[i+1]-ends[i],D3D12_RESOURCE_STATE_COPY_DEST,i!=0);

    D3D12_DESCRIPTOR_RANGE ranges[]={{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,0,0,0},{D3D12_DESCRIPTOR_RANGE_TYPE_UAV,3,0,0,1}};
    D3D12_ROOT_PARAMETER rp[2]={}; rp[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; rp[0].DescriptorTable={2,ranges};
    rp[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS; rp[1].Constants={0,0,4};
    D3D12_ROOT_SIGNATURE_DESC rs={2,rp,0,nullptr,D3D12_ROOT_SIGNATURE_FLAG_NONE};
    ComPtr<ID3DBlob> blob; HR(D3D12SerializeRootSignature(&rs,D3D_ROOT_SIGNATURE_VERSION_1,&blob,nullptr));
    ComPtr<ID3D12RootSignature> sig; HR(d->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&sig)));
    BYTE code[16384]; FILE *f=std::fopen("logical.dxil","rb"); if(!f) return 1;
    size_t n=std::fread(code,1,sizeof code,f); if(!n || !std::feof(f)) return 1; std::fclose(f);
    D3D12_COMPUTE_PIPELINE_STATE_DESC pd={}; pd.pRootSignature=sig.Get(); pd.CS={code,n};
    ComPtr<ID3D12PipelineState> ps; HR(d->CreateComputePipelineState(&pd,IID_PPV_ARGS(&ps)));
    D3D12_DESCRIPTOR_HEAP_DESC hd={D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,4,D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,0};
    ComPtr<ID3D12DescriptorHeap> heap; HR(d->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&heap)));
    auto cpu=heap->GetCPUDescriptorHandleForHeapStart();
    D3D12_SHADER_RESOURCE_VIEW_DESC s={}; s.Format=DXGI_FORMAT_R32_TYPELESS; s.ViewDimension=D3D12_SRV_DIMENSION_BUFFER;
    s.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; s.Buffer.NumElements=A/4; s.Buffer.Flags=D3D12_BUFFER_SRV_FLAG_RAW;
    d->CreateShaderResourceView(parts[0].Get(),&s,cpu);
    for (UINT i=1;i<=3;++i) {
        auto r=i<3?parts[i].Get():result.Get();
        D3D12_UNORDERED_ACCESS_VIEW_DESC u={}; u.Format=DXGI_FORMAT_R32_TYPELESS; u.ViewDimension=D3D12_UAV_DIMENSION_BUFFER;
        u.Buffer.NumElements=UINT(r->GetDesc().Width/4); u.Buffer.Flags=D3D12_BUFFER_UAV_FLAG_RAW;
        cpu.ptr+=d->GetDescriptorHandleIncrementSize(hd.Type); d->CreateUnorderedAccessView(r,nullptr,&u,cpu);
    }

    D3D12_COMMAND_QUEUE_DESC qd={}; qd.Type=D3D12_COMMAND_LIST_TYPE_COMPUTE;
    ComPtr<ID3D12CommandQueue> q; ComPtr<ID3D12CommandAllocator> a; ComPtr<ID3D12GraphicsCommandList> l;
    HR(d->CreateCommandQueue(&qd,IID_PPV_ARGS(&q))); HR(d->CreateCommandAllocator(qd.Type,IID_PPV_ARGS(&a)));
    HR(d->CreateCommandList(0,qd.Type,a.Get(),ps.Get(),IID_PPV_ARGS(&l)));
    auto transit=[&](ID3D12Resource *r,D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after) {
        D3D12_RESOURCE_BARRIER b={}; b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition={r,0,before,after}; l->ResourceBarrier(1,&b);
    };
    auto state=[](UINT i){return i?D3D12_RESOURCE_STATE_UNORDERED_ACCESS:D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;};
    for(UINT i=0;i<3;++i) {
        l->CopyBufferRegion(parts[i].Get(),0,up.Get(),ends[i],ends[i+1]-ends[i]);
        transit(parts[i].Get(),D3D12_RESOURCE_STATE_COPY_DEST,state(i));
    }
    ID3D12DescriptorHeap *heaps[]={heap.Get()}; l->SetDescriptorHeaps(1,heaps); l->SetComputeRootSignature(sig.Get());
    l->SetComputeRootDescriptorTable(0,heap->GetGPUDescriptorHandleForHeapStart());
    const UINT params[]={0,A,B,E}; l->SetComputeRoot32BitConstants(1,4,params,0);
    static_assert((E/4)%(512*64)==0); l->Dispatch(512,(E/4)/(512*64),1);
    D3D12_RESOURCE_BARRIER sync={}; sync.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV; l->ResourceBarrier(1,&sync);
    l->SetComputeRoot32BitConstant(1,1,0); l->Dispatch(1,1,1);
    for(UINT i=0;i<3;++i) {
        transit(parts[i].Get(),state(i),D3D12_RESOURCE_STATE_COPY_SOURCE);
        l->CopyBufferRegion(rb.Get(),ends[i],parts[i].Get(),0,ends[i+1]-ends[i]);
    }
    transit(result.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);
    l->CopyBufferRegion(rb.Get(),E,result.Get(),0,reportBytes); HR(l->Close());
    ID3D12CommandList *batch[]={l.Get()}; q->ExecuteCommandLists(1,batch);
    ComPtr<ID3D12Fence> fence; HR(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence))); HR(q->Signal(fence.Get(),1));
    HANDLE ev=CreateEventW(nullptr,FALSE,FALSE,nullptr); if(!ev)return 1;
    HR(fence->SetEventOnCompletion(1,ev)); if(WaitForSingleObject(ev,30000)!=WAIT_OBJECT_0)return 1; CloseHandle(ev);
    HR(d->GetDeviceRemovedReason()); HR(rb->Map(0,nullptr,(void**)&m));
    for(UINT p=0;p<E;p+=4) {
        UINT v=seed(p);
        check(p,m[(E+p)/4],v^(p<A?0xb0000000u:0xa0000000u),"shader-read/write-status");
        check(p,m[p/4],p==B-4?0xcafe0001:p==B?0xcafe0002:p<A?v:~v,"physical");
    }
    const UINT expected[]={seed(A-4),~seed(A),~seed(B-4),~seed(B),2,0};
    for(UINT i=0;i<6;++i) check(i*4,m[(2*E)/4+i],expected[i],"boundary report");
    rb->Unmap(0,&none);
    std::printf("PASS: %u dwords read, %u stores rejected, %u accepted; both cross-boundary pairs, end/overflow/alignment, independent physical readback\n",E/4,A/4,(E-A)/4);
}
