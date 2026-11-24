#include <d3d12.h>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
extern "C" HRESULT WINAPI D3D12XboxCreateDevice(void *,const void *,REFIID,void **);
extern "C" ID3D12RootSignature *xs_bvh_root_host(IUnknown *,IUnknown *);
extern "C" ID3D12PipelineState *test_bvh_copy_compact_host(IUnknown *,ID3D12RootSignature **);
#define SLOT(o,n,T) ((T)((*reinterpret_cast<void***>((o).Get()))[n/8]))
#define CK(x) do {if (!(x)) {fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); return 1;}} while(0)
#define HR(x) CK(SUCCEEDED(x))

int main()
{
    const IID boot={0x5405c344,0xd457,0x444e,{0xb4,0xdd,0x23,0x66,0xe4,0x5a,0xee,0x39}};
    DWORD params[20]={}; params[0]=0x20004; params[2]=0x100000; params[10]=1; params[15]=0x20;
    ComPtr<IUnknown> dev,foreign,copy,prefix,ps;
    HR(D3D12XboxCreateDevice(nullptr,params,boot,(void **)dev.GetAddressOf()));
    HR(D3D12XboxCreateDevice(nullptr,params,boot,(void **)foreign.GetAddressOf()));
    for (UINT constants:{16u,1u}) {
        D3D12_ROOT_PARAMETER1 rp[3]={};
        rp[0].ParameterType=rp[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV; rp[1].Descriptor.ShaderRegister=1;
        rp[2].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS; rp[2].Constants.Num32BitValues=constants;
        D3D12_VERSIONED_ROOT_SIGNATURE_DESC sig={}; sig.Version=D3D_ROOT_SIGNATURE_VERSION_1_1;
        sig.Desc_1_1={3,rp,0,nullptr,(D3D12_ROOT_SIGNATURE_FLAGS)0x03000000};
        ComPtr<ID3DBlob> blob; HR(D3D12SerializeVersionedRootSignature(&sig,&blob,nullptr)); CK(blob->GetBufferSize()==140);
        using Root=HRESULT(WINAPI *)(IUnknown *,UINT,const void *,SIZE_T,REFIID,void **);
        HR(SLOT(dev,0x80,Root)(dev.Get(),1,blob->GetBufferPointer(),blob->GetBufferSize(),IID_ID3D12RootSignature,
                (void **)(constants==16?copy:prefix).GetAddressOf()));
    }
    BYTE code[0x1134]; FILE *f=fopen("private-copy-compact.bin","rb"); CK(f);
    CK(fread(code,1,sizeof code,f)==sizeof code && fgetc(f)==EOF && !ferror(f)); fclose(f);
    struct Wire { IUnknown *root; const void *cs; SIZE_T bytes; BYTE tail[32]; } w={copy.Get(),code,sizeof code,{}};
    using Factory=HRESULT(WINAPI *)(IUnknown *,const Wire *,UINT64,UINT64,REFIID,void **);
    auto make=[&](const ComPtr<IUnknown> &d){return SLOT(d,0x1f0,Factory)(d.Get(),&w,0,0,IID_ID3D12PipelineState,(void **)ps.GetAddressOf());};
    code[sizeof code-1]^=1; CK(make(dev)==E_NOTIMPL && !ps); code[sizeof code-1]^=1;
    CK(make(foreign)==E_INVALIDARG && !ps);
    w.root=prefix.Get(); CK(make(dev)==E_INVALIDARG && !ps); w.root=copy.Get();
    HR(make(dev));
    using Name=HRESULT(WINAPI *)(IUnknown *,const WCHAR *);
    HR(SLOT(ps,0x30,Name)(ps.Get(),L"XDXR CopyAndCompact"));
    ID3D12RootSignature *rs=nullptr; auto host=test_bvh_copy_compact_host(ps.Get(),&rs);
    CK(host && rs && rs==xs_bvh_root_host(dev.Get(),copy.Get()));
    copy.Reset(); ComPtr<ID3D12Device> d; HR(host->GetDevice(IID_PPV_ARGS(&d)));

    // Separate source/destination regions per dispatch, surrounded by distinct canaries.
    constexpr UINT span=640, words=6*span;
    const UINT blocks[]={1,35,35}, groups[]={1,2,1}, stride[]={32,64,64};
    UINT expected[words]; for (UINT i=0;i<words;i++) expected[i]=0x8e100007u+307*i;
    for (UINT t=0;t<3;t++) expected[t*2*span+16+1]=blocks[t]-1;
    auto buffer=[&](D3D12_HEAP_TYPE type,ID3D12Resource **out) {
        D3D12_HEAP_PROPERTIES hp={}; hp.Type=type; hp.CreationNodeMask=hp.VisibleNodeMask=1;
        D3D12_RESOURCE_DESC rd={}; rd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER; rd.Width=sizeof expected;
        rd.Height=rd.DepthOrArraySize=rd.MipLevels=rd.SampleDesc.Count=1; rd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if(type==D3D12_HEAP_TYPE_DEFAULT) rd.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        return d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,type==D3D12_HEAP_TYPE_UPLOAD?
                D3D12_RESOURCE_STATE_GENERIC_READ:D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(out));
    };
    ComPtr<ID3D12Resource> seed,arena,rb;
    HR(buffer(D3D12_HEAP_TYPE_UPLOAD,&seed)); HR(buffer(D3D12_HEAP_TYPE_DEFAULT,&arena)); HR(buffer(D3D12_HEAP_TYPE_READBACK,&rb));
    void *mapped; HR(seed->Map(0,nullptr,&mapped)); memcpy(mapped,expected,sizeof expected); seed->Unmap(0,nullptr);
    D3D12_COMMAND_QUEUE_DESC qd={}; qd.Type=D3D12_COMMAND_LIST_TYPE_COMPUTE;
    ComPtr<ID3D12CommandQueue> q; HR(d->CreateCommandQueue(&qd,IID_PPV_ARGS(&q)));
    ComPtr<ID3D12CommandAllocator> a; HR(d->CreateCommandAllocator(qd.Type,IID_PPV_ARGS(&a)));
    ComPtr<ID3D12GraphicsCommandList> l; HR(d->CreateCommandList(1,qd.Type,a.Get(),host,IID_PPV_ARGS(&l)));
    auto transition=[&](D3D12_RESOURCE_STATES from,D3D12_RESOURCE_STATES to) {
        D3D12_RESOURCE_BARRIER b={}; b.Transition={arena.Get(),0,from,to}; l->ResourceBarrier(1,&b);
    };
    l->CopyResource(arena.Get(),seed.Get()); transition(D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    l->SetComputeRootSignature(rs);
    for (UINT t=0;t<3;t++) {
        const UINT src=t*2*span+16,dst=src+span;
        l->SetComputeRootUnorderedAccessView(0,arena->GetGPUVirtualAddress()+4*src);
        l->SetComputeRootUnorderedAccessView(1,arena->GetGPUVirtualAddress()+4*dst);
        l->SetComputeRoot32BitConstant(2,stride[t],0); l->Dispatch(groups[t],1,1);
        // The under-dispatch deliberately leaves every other 512-byte stripe alone.
        for (UINT i=0;i<16*blocks[t];i++) if (t!=2 || (i/128)%2==0) expected[dst+i]=expected[src+i];
    }
    transition(D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE); l->CopyResource(rb.Get(),arena.Get());
    HR(l->Close()); ID3D12CommandList *lists[]={l.Get()}; q->ExecuteCommandLists(1,lists);
    ComPtr<ID3D12Fence> fence; HR(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));
    HANDLE ev=CreateEventW(nullptr,FALSE,FALSE,nullptr); CK(ev); HR(q->Signal(fence.Get(),1)); HR(fence->SetEventOnCompletion(1,ev));
    CK(WaitForSingleObject(ev,10000)==WAIT_OBJECT_0 && fence->GetCompletedValue()==1); CloseHandle(ev);
    HR(rb->Map(0,nullptr,&mapped)); for(UINT i=0;i<words;i++) if (((UINT*)mapped)[i]!=expected[i]) {
        fprintf(stderr,"FAIL word %u: got %08x expected %08x\n",i,((UINT*)mapped)[i],expected[i]); return 1;
    }
    rb->Unmap(0,nullptr);
    puts("PASS CopyAndCompact: exact PSO/root, 64 and 2240 bytes, partial tail, sparse stride, input and guards");
}
