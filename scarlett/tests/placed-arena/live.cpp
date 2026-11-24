#include "../../native/xmem/placed_table.h"
extern "C" {
__declspec(dllimport) BOOL WINAPI XMemAllocatePhysicalPages(ULONGLONG,PULONG_PTR,PULONG_PTR);
__declspec(dllimport) BOOL WINAPI XMemFreePhysicalPages(ULONG_PTR,PULONG_PTR);
__declspec(dllimport) void *WINAPI XMemMapPhysicalPages(void *,ULONG_PTR,PULONG_PTR);
__declspec(dllimport) void *WINAPI XMemVirtualAlloc(void *,SIZE_T,DWORD,ULONGLONG,DWORD);
}
#include <cstdio>
#include <cstdlib>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
#define HR(x) do { HRESULT h_=(x); if (FAILED(h_)) { std::fprintf(stderr,"%d: %s: %08lx\n",__LINE__,#x,(unsigned long)h_); std::exit(1); } } while(0)
static constexpr UINT A=27u<<20, B=74u<<20, E=174u<<20, reportBytes=E+24;

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
    ComPtr<ID3D12Device3> d; HR(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&d))); device=d.Get();

    auto rb=buffer(D3D12_HEAP_TYPE_READBACK,reportBytes,D3D12_RESOURCE_STATE_COPY_DEST);
    auto result=buffer(D3D12_HEAP_TYPE_DEFAULT,reportBytes,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,true);
    constexpr UINT pg=65536,n=2960;
    const UINT count[]={160,16,432,752,1600}, id[]={2352,2512,2528,0,752}, prot[]={0x1004,0x1404,0x8404,0x2404,0x2004};
    BYTE *at[5]; auto va=(BYTE *)XMemVirtualAlloc(nullptr,n*pg,0x42000,7,1);
    if (!va) return 1;
    for(unsigned i=0;i<5;i++) { at[i]=va; if(i<4&&!VirtualFree(va,count[i]*pg,0x8002)) return 1; va+=count[i]*pg; }
    for(unsigned i=0;i<5;i++) if(XMemVirtualAlloc(at[i],count[i]*pg,0x20406000,0x27,prot[i])!=at[i]) return 1;
    ULONG_PTR ids[2][n]; for(auto &run:ids) { ULONG_PTR size=n; if(!XMemAllocatePhysicalPages(0x280,&size,run)||size!=n)return 1; }
    auto map=[&](int gen){ for(unsigned i=2;i<5;i++) if(XMemMapPhysicalPages(at[i],count[i],gen<0?nullptr:ids[gen]+id[i])!=at[i]) std::exit(1); };
    auto guest=[&](UINT p)->volatile UINT& { return *(volatile UINT *)(at[p<A?2:p<B?3:4]+(p<A?p:p<B?p-A:p-B)); };
    map(0); XsPlacedCapture *cap; HR(ScarlettCapturePlacedV1(d.Get(),at[2],E,&cap));
    XsDataOperand bad={}; if(SUCCEEDED(ScarlettPlacedDataV1(cap,0,4,&bad))||bad.resource) return 1;
    for(UINT p=0;p<E;p+=4) guest(p)=seed(p);
    MemoryBarrier(); map(-1); map(1);
    for(UINT p=0;p<E;p+=4) guest(p)=~seed(p)^0x53000000;
    MemoryBarrier();
    XsPlacedTable *table=nullptr; HR(ScarlettCreatePlacedTableV1(cap,&table)); ScarlettReleasePlacedV1(cap);
    // Both the binding and the subsequent dispatch target generation zero.
    D3D12_DESCRIPTOR_RANGE ranges[]={{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,0,0,0},{D3D12_DESCRIPTOR_RANGE_TYPE_UAV,2,0,0,1}};
    D3D12_ROOT_PARAMETER rp[3]={}; rp[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; rp[0].DescriptorTable={2,ranges};
    rp[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS; rp[1].Constants={0,0,4};
    rp[2].ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV; rp[2].Descriptor.ShaderRegister=2;
    D3D12_ROOT_SIGNATURE_DESC rs={3,rp,0,nullptr,D3D12_ROOT_SIGNATURE_FLAG_NONE};
    ComPtr<ID3DBlob> blob; HR(D3D12SerializeRootSignature(&rs,D3D_ROOT_SIGNATURE_VERSION_1,&blob,nullptr));
    ComPtr<ID3D12RootSignature> sig; HR(d->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&sig)));
    BYTE code[16384]; FILE *f=std::fopen("logical.dxil","rb"); if(!f) return 1;
    size_t length=std::fread(code,1,sizeof code,f); if(!length || !std::feof(f)) return 1; std::fclose(f);
    D3D12_COMPUTE_PIPELINE_STATE_DESC pd={}; pd.pRootSignature=sig.Get(); pd.CS={code,length};
    ComPtr<ID3D12PipelineState> ps; HR(d->CreateComputePipelineState(&pd,IID_PPV_ARGS(&ps)));
    D3D12_COMMAND_QUEUE_DESC qd={}; qd.Type=D3D12_COMMAND_LIST_TYPE_COMPUTE;
    ComPtr<ID3D12CommandQueue> q; ComPtr<ID3D12CommandAllocator> a; ComPtr<ID3D12GraphicsCommandList> l;
    HR(d->CreateCommandQueue(&qd,IID_PPV_ARGS(&q))); HR(d->CreateCommandAllocator(qd.Type,IID_PPV_ARGS(&a)));
    HR(d->CreateCommandList(0,qd.Type,a.Get(),ps.Get(),IID_PPV_ARGS(&l)));
    auto transit=[&](ID3D12Resource *r,D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after) {
        D3D12_RESOURCE_BARRIER b={}; b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition={r,0,before,after}; l->ResourceBarrier(1,&b);
    };
    l->SetComputeRootSignature(sig.Get()); HR(ScarlettBindPlacedTableV1(table,l.Get()));
    l->SetComputeRootUnorderedAccessView(2,result->GetGPUVirtualAddress());
    const UINT params[]={0,A,B,E}; l->SetComputeRoot32BitConstants(1,4,params,0);
    static_assert((E/4)%(512*64)==0); l->Dispatch(512,(E/4)/(512*64),1);
    D3D12_RESOURCE_BARRIER sync={}; sync.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV; l->ResourceBarrier(1,&sync);
    l->SetComputeRoot32BitConstant(1,1,0); l->Dispatch(1,1,1);
    transit(result.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);
    l->CopyBufferRegion(rb.Get(),0,result.Get(),0,reportBytes); HR(l->Close());
    ID3D12CommandList *batch[]={l.Get()}; q->ExecuteCommandLists(1,batch);
    ComPtr<ID3D12Fence> fence; HR(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence))); HR(q->Signal(fence.Get(),1));
    HANDLE ev=CreateEventW(nullptr,FALSE,FALSE,nullptr); if(!ev)return 1;
    HR(fence->SetEventOnCompletion(1,ev)); if(WaitForSingleObject(ev,30000)!=WAIT_OBJECT_0)return 1; CloseHandle(ev);
    HR(d->GetDeviceRemovedReason()); ScarlettReleasePlacedTableV1(table); UINT *m; HR(rb->Map(0,nullptr,(void**)&m));
    for(UINT p=0;p<E;p+=4) check(p,guest(p),~seed(p)^0x53000000,"replacement");
    map(-1); map(0);
    for(UINT p=0;p<E;p+=4) {
        UINT v=seed(p);
        check(p,m[p/4],v^(p<A?0xb0000000u:0xa0000000u),"shader-read/write-status");
        check(p,guest(p),p==B-4?0xcafe0001:p==B?0xcafe0002:p<A?v:~v,"physical");
    }
    const UINT expected[]={seed(A-4),~seed(A),~seed(B-4),~seed(B),2,0};
    for(UINT i=0;i<6;++i) check(i*4,m[E/4+i],expected[i],"boundary report");
    D3D12_RANGE none={}; rb->Unmap(0,&none);
    map(-1); for (auto &run:ids) if(!XMemFreePhysicalPages(n,run))return 1;
    for(auto v:at) if(!VirtualFree(v,0,MEM_RELEASE))return 1;
    std::printf("PASS: %u dwords read, %u stores rejected, %u accepted; both cross-boundary pairs, end/overflow/alignment, original + replacement XMem generation checked after capture release\n",E/4,A/4,(E-A)/4);
}
