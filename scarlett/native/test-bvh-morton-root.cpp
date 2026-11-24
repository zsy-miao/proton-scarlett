#include <d3d12.h>
#include <cstdio>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
extern "C" HRESULT WINAPI D3D12XboxCreateDevice(void *,const void *,REFIID,void **);
extern "C" ID3D12RootSignature *xs_bvh_root_host(IUnknown *,IUnknown *);
#define SLOT(o,n,T) ((T)((*reinterpret_cast<void***>(o))[n/8]))
#define CK(x) do { if (!(x)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); return 1; } } while(0)
#define HR(x) CK(SUCCEEDED(x))

static size_t load(const char *name,BYTE *data,size_t capacity)
{
    FILE *f=fopen(name,"rb"); if (!f) return 0;
    size_t n=fread(data,1,capacity,f);
    bool ok=fgetc(f)==EOF && !ferror(f); fclose(f);
    return ok?n:0;
}
int main()
{
    DWORD params[20]={}; params[0]=0x20004; params[2]=0x100000; params[10]=1; params[15]=0x20;
    const IID boot={0x5405c344,0xd457,0x444e,{0xb4,0xdd,0x23,0x66,0xe4,0x5a,0xee,0x39}};
    ComPtr<IUnknown> dev,morton,utility;
    HR(D3D12XboxCreateDevice(nullptr,params,boot,(void **)dev.GetAddressOf()));
    D3D12_ROOT_PARAMETER1 p[6]={};
    p[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS; p[0].Constants.Num32BitValues=8;
    for (UINT i=1;i<6;i++) { p[i].ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV; p[i].Descriptor.ShaderRegister=i-1; }
    D3D12_VERSIONED_ROOT_SIGNATURE_DESC sig={}; sig.Version=D3D_ROOT_SIGNATURE_VERSION_1_1;
    sig.Desc_1_1={6,p,0,nullptr,(D3D12_ROOT_SIGNATURE_FLAGS)0x03000000};
    ComPtr<ID3DBlob> blob;
    HR(D3D12SerializeVersionedRootSignature(&sig,&blob,nullptr)); CK(blob->GetBufferSize()==212);
    BYTE wire[212]; memcpy(wire,blob->GetBufferPointer(),sizeof wire); blob.Reset();
    CK(!memcmp(wire+36,"RTS0\xa8\0\0\0",8));
    using Root=HRESULT(WINAPI *)(IUnknown *,UINT,const void *,SIZE_T,REFIID,void **);
    auto root=[&](const void *s,SIZE_T n,void **o) {
        return SLOT(dev.Get(),0x80,Root)(dev.Get(),1,s,n,IID_ID3D12RootSignature,o);
    };
    void *bad=dev.Get();
    wire[211]^=1; CK(root(wire,sizeof wire,&bad)==E_NOTIMPL && !bad); wire[211]^=1;
    HR(root(wire,sizeof wire,(void **)morton.GetAddressOf()));
    memset(p,0,sizeof p); sig.Desc_1_1.NumParameters=3;
    p[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV; p[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV;
    p[2].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS; p[2].Constants.Num32BitValues=16;
    HR(D3D12SerializeVersionedRootSignature(&sig,&blob,nullptr));
    HR(root(blob->GetBufferPointer(),blob->GetBufferSize(),(void **)utility.GetAddressOf()));
    using Name=HRESULT(WINAPI *)(IUnknown *,const WCHAR *);
    static const WCHAR mn[]=L"XDXR MortonCodesCalculator",un[sizeof mn/sizeof *mn]=L"XDXR UtilityShaders";
    HR(SLOT(morton.Get(),0x30,Name)(morton.Get(),mn));
    CK(SLOT(morton.Get(),0x30,Name)(morton.Get(),un)==E_NOTIMPL);
    HR(SLOT(utility.Get(),0x30,Name)(utility.Get(),un));
    CK(SLOT(utility.Get(),0x30,Name)(utility.Get(),mn)==E_NOTIMPL);
    BYTE cs[0x33fc];
    struct Wire { IUnknown *root; const void *cs; SIZE_T n; BYTE tail[32]; };
    using Factory=HRESULT(WINAPI *)(IUnknown *,const Wire *,UINT64,UINT64,REFIID,void **);
    auto make=[&](IUnknown *r,SIZE_T n) {
        Wire w={r,cs,n,{}}; bad=dev.Get();
        return SLOT(dev.Get(),0x1f0,Factory)(dev.Get(),&w,0,0,IID_ID3D12PipelineState,&bad);
    };
    for (int i=0;i<2;i++) {
        size_t n=load(i?"morton64.bin":"morton32.bin",cs,sizeof cs); CK(n==(i?0x33fcu:0x33a0u));
        // Both hash-pinned containers have their 168-byte RTS0 at chunk offset 0x144.
        CK(!memcmp(cs+0x144,wire+36,176));
        CK(make(utility.Get(),n)==E_NOTIMPL && !bad);
        CK(make(morton.Get(),n)==E_NOTIMPL && !bad); // Morton PSOs not implemented yet.
    }
    size_t n=load("reset.bin",cs,sizeof cs); CK(n==0xfe8);
    CK(make(morton.Get(),n)==E_INVALIDARG && !bad);
    HR(make(utility.Get(),n)); CK(bad); ((IUnknown *)bad)->Release();

    auto host=xs_bvh_root_host(dev.Get(),morton.Get()); CK(host);
    ComPtr<ID3D12Device> d; HR(host->GetDevice(IID_PPV_ARGS(&d)));
    n=load("morton-test.dxil",cs,sizeof cs); CK(n);
    D3D12_COMPUTE_PIPELINE_STATE_DESC pd={}; pd.pRootSignature=host; pd.CS={cs,n};
    ComPtr<ID3D12PipelineState> ps; HR(d->CreateComputePipelineState(&pd,IID_PPV_ARGS(&ps)));
    auto buffer=[&](D3D12_HEAP_TYPE type,ID3D12Resource **out) {
        D3D12_HEAP_PROPERTIES hp={}; hp.Type=type; hp.CreationNodeMask=hp.VisibleNodeMask=1;
        D3D12_RESOURCE_DESC rd={}; rd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER; rd.Width=160;
        rd.Height=rd.DepthOrArraySize=rd.MipLevels=rd.SampleDesc.Count=1; rd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        auto state=D3D12_RESOURCE_STATE_COPY_DEST;
        if (type==D3D12_HEAP_TYPE_DEFAULT) { rd.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS; state=D3D12_RESOURCE_STATE_UNORDERED_ACCESS; }
        return d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,state,nullptr,IID_PPV_ARGS(out));
    };
    ComPtr<ID3D12Resource> dst,rb;
    HR(buffer(D3D12_HEAP_TYPE_DEFAULT,&dst)); HR(buffer(D3D12_HEAP_TYPE_READBACK,&rb));
    D3D12_COMMAND_QUEUE_DESC qd={}; qd.Type=D3D12_COMMAND_LIST_TYPE_COMPUTE;
    ComPtr<ID3D12CommandQueue> queue; HR(d->CreateCommandQueue(&qd,IID_PPV_ARGS(&queue)));
    ComPtr<ID3D12CommandAllocator> alloc; HR(d->CreateCommandAllocator(qd.Type,IID_PPV_ARGS(&alloc)));
    ComPtr<ID3D12GraphicsCommandList> list; HR(d->CreateCommandList(1,qd.Type,alloc.Get(),ps.Get(),IID_PPV_ARGS(&list)));
    UINT k[8]; for (UINT i=0;i<8;i++) k[i]=0x72000201u*(i+1);
    list->SetComputeRootSignature(host); list->SetComputeRoot32BitConstants(0,8,k,0);
    for (UINT i=0;i<5;i++) list->SetComputeRootUnorderedAccessView(i+1,dst->GetGPUVirtualAddress()+32*i);
    list->Dispatch(1,1,1);
    D3D12_RESOURCE_BARRIER b={}; b.Transition={dst.Get(),0,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE};
    list->ResourceBarrier(1,&b); list->CopyResource(rb.Get(),dst.Get()); HR(list->Close());
    ID3D12CommandList *lists[]={list.Get()}; queue->ExecuteCommandLists(1,lists);
    ComPtr<ID3D12Fence> fence; HR(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));
    HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr); CK(event);
    HR(queue->Signal(fence.Get(),1)); HR(fence->SetEventOnCompletion(1,event)); CK(WaitForSingleObject(event,10000)==WAIT_OBJECT_0);
    CloseHandle(event); CK(fence->GetCompletedValue()==1);
    UINT *mapped; HR(rb->Map(0,nullptr,(void **)&mapped));
    for (UINT i=0;i<40;i++) CK(mapped[i]==k[i%8]+1+i/8);
    rb->Unmap(0,nullptr);
    puts("PASS Morton root: both embedded RTS0 match, PSO/root pairing checked, 40 fenced words across b0 and u0-u4");
}
