#include "gpu_buffer.hpp"
#include "placed_capture.h"
#include <wrl/client.h>
#include <dxgi1_4.h>
#include <cstdio>
#include <cstdlib>
using Microsoft::WRL::ComPtr;
extern "C" {
__declspec(dllimport) BOOL WINAPI XMemAllocatePhysicalPages(ULONGLONG,PULONG_PTR,PULONG_PTR);
__declspec(dllimport) BOOL WINAPI XMemFreePhysicalPages(ULONG_PTR,PULONG_PTR);
__declspec(dllimport) void *WINAPI XMemMapPhysicalPages(void *,ULONG_PTR,PULONG_PTR);
__declspec(dllimport) void *WINAPI XMemVirtualAlloc(void *,SIZE_T,DWORD,ULONGLONG,DWORD);
}
#define CK(x) do { if (!(x)) { std::fprintf(stderr,"%d: %s error %lu\n",__LINE__,#x,GetLastError()); std::exit(1); } } while(0)
#define HR(x) do { HRESULT h_=(x); if (FAILED(h_)) { std::fprintf(stderr,"%d: %s HRESULT %08lx\n",__LINE__,#x,(unsigned long)h_); std::exit(1); } } while(0)
constexpr UINT MiB=1u<<20, A=27*MiB, B=74*MiB, E=174*MiB, pg=65536;
constexpr UINT pages[]={160,16,432,752,1600}, policy[]={0x1004,0x1404,0x8404,0x2404,0x2004}, first[]={2352,2512,2528,0,752};
static UINT seed(UINT p,UINT pass) { return (0x30000000*pass)^(p/4); }
static void check(UINT p,UINT value,UINT want) {
    if (value!=want) { std::fprintf(stderr,"logical %08x: got %08x expected %08x\n",p,value,want); std::exit(1); }
}
int main() {
    ComPtr<ID3D12Device3> d; HR(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&d)));
    ComPtr<IDXGIFactory4> fac; ComPtr<IDXGIAdapter1> adapter;
    HR(CreateDXGIFactory1(IID_PPV_ARGS(&fac))); HR(fac->EnumAdapterByLuid(d->GetAdapterLuid(),IID_PPV_ARGS(&adapter)));
    DXGI_ADAPTER_DESC1 ad={}; HR(adapter->GetDesc1(&ad)); CK(ad.VendorId==0x1002 && ad.DeviceId==0x164e);
    BYTE *base=(BYTE *)XMemVirtualAlloc(nullptr,2960*pg,0x42000,7,1), *at[5]; CK(base);
    for(UINT i=0,off=0;i<5;++i) {
        at[i]=base+off; off+=pages[i]*pg;
        if(i<4) CK(VirtualFree(at[i],pages[i]*pg,MEM_RELEASE|MEM_PRESERVE_PLACEHOLDER));
    }
    for(UINT i=0;i<5;++i) CK(XMemVirtualAlloc(at[i],pages[i]*pg,0x20406000,0x27,policy[i])==at[i]);
    ULONG_PTR ids[2960],count=2960; CK(XMemAllocatePhysicalPages(0x280,&count,ids) && count==2960);
    for(UINT i=0;i<5;++i) CK(XMemMapPhysicalPages(at[i],pages[i],ids+first[i])==at[i]);

    XsPlacedCapture *capture; HR(ScarlettCapturePlacedV1(d.Get(),at[2],E,&capture));
    ID3D12Resource *part[2]; ImportedBuffer witness[2];
    for(UINT i=0;i<2;++i) {
        auto va=at[i+3]; auto bytes=pages[i+3]*pg;
        XsDataOperand op;
        HR(ScarlettPlacedDataV1(capture,i?B:A,bytes,&op));
        CK(!op.offset && op.bytes==bytes); part[i]=op.resource;
        SIZE_T off; HR(witness[i].acquire(d.Get(),va,bytes,&off)); CK(!off);
        D3D12_HEAP_PROPERTIES h; D3D12_HEAP_FLAGS flags; HR(part[i]->GetHeapProperties(&h,&flags));
        auto r=part[i]->GetDesc(); CK(r.Width==bytes && r.Flags==D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        std::printf("GPU %04x:%04x policy %x: %u bytes heap type=%u page=%u pool=%u flags=%x resource flags=%x\n",
                ad.VendorId,ad.DeviceId,policy[i+3],bytes,h.Type,h.CPUPageProperty,h.MemoryPoolPreference,flags,r.Flags);
    }
    scarlett_section_info denied;
    CK(FAILED(scarlett_query_section(0x53430005,at[2],4,&denied)) &&
       FAILED(scarlett_query_section(0x5343000c,at[2],4,&denied)));
    auto marker=(volatile UINT *)(at[2]+A-4); *marker=0x8404da7a; MemoryBarrier();

    ComPtr<ID3D12Resource> result,rb;
    D3D12_RESOURCE_DESC rd={}; rd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER; rd.Width=20;
    rd.Height=rd.DepthOrArraySize=rd.MipLevels=rd.SampleDesc.Count=1; rd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    D3D12_HEAP_PROPERTIES hp={}; hp.CreationNodeMask=hp.VisibleNodeMask=1;
    hp.Type=D3D12_HEAP_TYPE_READBACK;
    HR(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&rb)));
    hp.Type=D3D12_HEAP_TYPE_DEFAULT; rd.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    HR(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&result)));
    D3D12_DESCRIPTOR_RANGE range={D3D12_DESCRIPTOR_RANGE_TYPE_UAV,3,0,0,0};
    D3D12_ROOT_PARAMETER rp[2]={}; rp[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; rp[0].DescriptorTable={1,&range};
    rp[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS; rp[1].Constants={0,0,2};
    D3D12_ROOT_SIGNATURE_DESC rs={2,rp,0,nullptr,D3D12_ROOT_SIGNATURE_FLAG_NONE};
    ComPtr<ID3DBlob> blob; HR(D3D12SerializeRootSignature(&rs,D3D_ROOT_SIGNATURE_VERSION_1,&blob,nullptr));
    ComPtr<ID3D12RootSignature> sig; HR(d->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&sig)));
    BYTE code[16384]; FILE *f=std::fopen("arena-data.dxil","rb"); CK(f);
    size_t n=std::fread(code,1,sizeof code,f); CK(n && std::feof(f)); std::fclose(f);
    D3D12_COMPUTE_PIPELINE_STATE_DESC pd={}; pd.pRootSignature=sig.Get(); pd.CS={code,n};
    ComPtr<ID3D12PipelineState> ps; HR(d->CreateComputePipelineState(&pd,IID_PPV_ARGS(&ps)));
    D3D12_DESCRIPTOR_HEAP_DESC hd={D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,3,D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,0};
    ComPtr<ID3D12DescriptorHeap> heap; HR(d->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&heap)));
    auto cpu=heap->GetCPUDescriptorHandleForHeapStart();
    for(UINT i=0;i<3;++i) {
        auto r=i<2?part[i]:result.Get();
        D3D12_UNORDERED_ACCESS_VIEW_DESC u={}; u.Format=DXGI_FORMAT_R32_TYPELESS; u.ViewDimension=D3D12_UAV_DIMENSION_BUFFER;
        u.Buffer.NumElements=UINT(r->GetDesc().Width/4); u.Buffer.Flags=D3D12_BUFFER_UAV_FLAG_RAW;
        d->CreateUnorderedAccessView(r,nullptr,&u,cpu); cpu.ptr+=d->GetDescriptorHandleIncrementSize(hd.Type);
    }
    D3D12_COMMAND_QUEUE_DESC qd={}; qd.Type=D3D12_COMMAND_LIST_TYPE_COMPUTE;
    ComPtr<ID3D12CommandQueue> q; ComPtr<ID3D12CommandAllocator> a; ComPtr<ID3D12GraphicsCommandList> l;
    HR(d->CreateCommandQueue(&qd,IID_PPV_ARGS(&q))); HR(d->CreateCommandAllocator(qd.Type,IID_PPV_ARGS(&a)));
    HR(d->CreateCommandList(0,qd.Type,a.Get(),ps.Get(),IID_PPV_ARGS(&l)));
    ComPtr<ID3D12Fence> fence; HR(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));
    HANDLE ev=CreateEventW(nullptr,FALSE,FALSE,nullptr); CK(ev);
    for(UINT pass=1;pass<=2;++pass) {
        UINT salt=0xa5041100+pass;
        for(UINT i=0;i<2;++i) { auto v=(UINT *)at[i+3]; UINT off=i?B:A;
            for(UINT j=0;j<pages[i+3]*pg/4;++j) v[j]=seed(off+4*j,pass); }
        MemoryBarrier();
        if(pass==2) { HR(a->Reset()); HR(l->Reset(a.Get(),ps.Get())); }
        ID3D12DescriptorHeap *heaps[]={heap.Get()}; l->SetDescriptorHeaps(1,heaps); l->SetComputeRootSignature(sig.Get());
        l->SetComputeRootDescriptorTable(0,heap->GetGPUDescriptorHandleForHeapStart());
        const UINT params[]={0,salt}; l->SetComputeRoot32BitConstants(1,2,params,0);
        static_assert(((E-A)/4)%(512*64)==0); l->Dispatch(512,(E-A)/4/(512*64),1);
        D3D12_RESOURCE_BARRIER b={}; b.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV; l->ResourceBarrier(1,&b);
        l->SetComputeRoot32BitConstant(1,1,0); l->Dispatch(1,1,1);
        b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition={result.Get(),0,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE}; l->ResourceBarrier(1,&b);
        l->CopyBufferRegion(rb.Get(),0,result.Get(),0,20);
        b.Transition.StateBefore=D3D12_RESOURCE_STATE_COPY_SOURCE; b.Transition.StateAfter=D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        l->ResourceBarrier(1,&b); HR(l->Close());
        if(pass==2) {
            for(UINT i=0;i<5;++i) {
                CK(XMemMapPhysicalPages(at[i],pages[i],nullptr)==at[i] && VirtualFree(at[i],0,MEM_RELEASE));
                if(i>=3) CK(VirtualAlloc(at[i],pages[i]*pg,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE)==at[i]);
            }
            CK(XMemFreePhysicalPages(2960,ids));
        }
        ID3D12CommandList *batch[]={l.Get()}; q->ExecuteCommandLists(1,batch); HR(q->Signal(fence.Get(),pass));
        HR(fence->SetEventOnCompletion(pass,ev)); CK(WaitForSingleObject(ev,30000)==WAIT_OBJECT_0); HR(d->GetDeviceRemovedReason());
        UINT *log; HR(rb->Map(0,nullptr,(void**)&log)); UINT positions[]={A,B-4,B,E-4};
        for(UINT i=0;i<4;++i) check(positions[i],log[i],seed(positions[i],pass)^salt);
        check(E,log[4],6); D3D12_RANGE none={}; rb->Unmap(0,&none);
        if(pass==1) check(A-4,*marker,0x8404da7a);
        for(UINT i=0;i<2;++i) {
            auto title=(volatile UINT *)at[i+3], held=(volatile UINT *)witness[i].cpu();
            for(UINT j=0;j<pages[i+3]*pg/4;++j) {
                UINT p=(i?B:A)+j*4, want=p==B-4?0xcafe0001:p==B?0xcafe0002:seed(p,pass)^salt;
                check(p,held[j],want); check(p,title[j],pass==1?want:0);
            }
        }
        std::printf("PASS %u: 38535168 DWORDs checked in both CPU mappings; GPU boundary reads and RAW stores; %s\n",
            pass,pass==1?"live title aliases":"old pages after original run free; both reused guest VAs unchanged");
    }
    for(UINT i=3;i<5;++i) CK(VirtualFree(at[i],0,MEM_RELEASE));
    ScarlettReleasePlacedV1(capture);
    CloseHandle(ev);
}
