#include <d3d12.h>
#include <cstdio>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
extern "C" HRESULT WINAPI D3D12XboxCreateDevice(void *,const void *,REFIID,void **);
extern "C" ID3D12RootSignature *xs_bvh_root_host(IUnknown *,IUnknown *);
#define SLOT(o,n,T) ((T)((*reinterpret_cast<void***>(o))[n/8]))
#define CK(x) do { if (!(x)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); return 1; } } while(0)
#define HR(x) CK(SUCCEEDED(x))

int main()
{
    DWORD params[20]={}; params[0]=0x20004; params[2]=0x100000; params[10]=1; params[15]=0x20;
    const IID boot={0x5405c344,0xd457,0x444e,{0xb4,0xdd,0x23,0x66,0xe4,0x5a,0xee,0x39}};
    ComPtr<IUnknown> dev,root;
    HR(D3D12XboxCreateDevice(nullptr,params,boot,(void **)dev.GetAddressOf()));
    D3D12_ROOT_PARAMETER1 p[3]={};
    p[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV; p[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV;
    p[2].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS; p[2].Constants.Num32BitValues=16;
    D3D12_VERSIONED_ROOT_SIGNATURE_DESC sig={}; sig.Version=D3D_ROOT_SIGNATURE_VERSION_1_1;
    sig.Desc_1_1={3,p,0,nullptr,(D3D12_ROOT_SIGNATURE_FLAGS)0x03000000};
    ComPtr<ID3DBlob> blob;
    HR(D3D12SerializeVersionedRootSignature(&sig,&blob,nullptr));
    BYTE wire[140]; CK(blob->GetBufferSize()==sizeof wire);
    memcpy(wire,blob->GetBufferPointer(),sizeof wire); blob.Reset();
    if (FILE *f=fopen("private-root.bin","rb")) {
        BYTE captured[sizeof wire];
        CK(fread(captured,1,sizeof captured,f)==sizeof captured && fgetc(f)==EOF && !ferror(f));
        fclose(f); CK(!memcmp(captured,wire,sizeof wire));
        puts("PASS independent serialization matches private capture byte-for-byte");
    }
    ComPtr<ID3D12VersionedRootSignatureDeserializer> parsed;
    HR(D3D12CreateVersionedRootSignatureDeserializer(wire,sizeof wire,IID_PPV_ARGS(&parsed)));
    auto v=parsed->GetUnconvertedRootSignatureDesc(); CK(v && v->Version==sig.Version);
    auto &s=v->Desc_1_1;
    CK(s.Flags==sig.Desc_1_1.Flags && !s.NumStaticSamplers && s.NumParameters==3);
    for (UINT i=0;i<3;i++) {
        auto &q=s.pParameters[i]; CK(q.ParameterType==p[i].ParameterType && q.ShaderVisibility==D3D12_SHADER_VISIBILITY_ALL);
        if (i==2) CK(q.Constants.Num32BitValues==16 && !q.Constants.ShaderRegister && !q.Constants.RegisterSpace);
        else CK(!q.Descriptor.ShaderRegister && !q.Descriptor.RegisterSpace && !q.Descriptor.Flags);
    }
    using Create=HRESULT(WINAPI *)(IUnknown *,UINT,const void *,SIZE_T,REFIID,void **);
    auto make=SLOT(dev.Get(),0x80,Create);
    void *bad=dev.Get();
    CK(make(dev.Get(),1,wire,sizeof wire,IID_IUnknown,&bad)==E_NOINTERFACE && !bad);
    CK(make(dev.Get(),0,wire,sizeof wire,IID_ID3D12RootSignature,&bad)==E_NOTIMPL && !bad);
    CK(make(dev.Get(),1,wire,sizeof wire-1,IID_ID3D12RootSignature,&bad)==E_NOTIMPL && !bad);
    CK(make(dev.Get(),1,wire,sizeof wire,IID_ID3D12RootSignature,nullptr)==E_POINTER);
    wire[139]^=1; CK(make(dev.Get(),1,wire,sizeof wire,IID_ID3D12RootSignature,&bad)==E_NOTIMPL && !bad); wire[139]^=1;
    HR(make(dev.Get(),1,wire,sizeof wire,IID_ID3D12RootSignature,(void **)root.GetAddressOf()));
    memset(wire,0,sizeof wire); parsed.Reset();
    IUnknown *qi=nullptr;
    HR(root->QueryInterface(IID_ID3D12RootSignature,(void **)&qi)); CK(qi==root.Get() && qi->Release()==1);
    HR(root->QueryInterface(IID_IUnknown,(void **)&qi)); CK(qi==root.Get() && qi->Release()==1);
    CK(root->QueryInterface(IID_ID3D12Device,&bad)==E_NOINTERFACE && !bad);
    using Name=HRESULT(WINAPI *)(IUnknown *,const WCHAR *);
    HR(SLOT(root.Get(),0x30,Name)(root.Get(),L"XDXR UtilityShaders"));
    auto host=xs_bvh_root_host(dev.Get(),root.Get());
    CK(host && (IUnknown *)host!=root.Get() && !xs_bvh_root_host(root.Get(),root.Get()) && !xs_bvh_root_host(dev.Get(),dev.Get()));
    ComPtr<ID3D12Device> d; HR(host->GetDevice(IID_PPV_ARGS(&d)));
    CK(dev.Reset()==1); // root owns the remaining device reference
    BYTE code[8192]; FILE *f=fopen("test-bvh-root.dxil","rb"); CK(f);
    size_t size=fread(code,1,sizeof code,f); CK(size && size<sizeof code && feof(f)); fclose(f);
    ComPtr<ID3D12PipelineState> ps;
    D3D12_COMPUTE_PIPELINE_STATE_DESC pd={}; pd.pRootSignature=host; pd.CS={code,size};
    HR(d->CreateComputePipelineState(&pd,IID_PPV_ARGS(&ps)));
    UINT input[16],words[16];
    for (UINT i=0;i<16;i++) { input[i]=0x41000001u+0x3123*i; words[i]=0x380001u*(i+1); }
    auto buffer=[&](D3D12_HEAP_TYPE type,D3D12_RESOURCE_STATES state,ID3D12Resource **out) {
        D3D12_HEAP_PROPERTIES hp={}; hp.Type=type; hp.CreationNodeMask=hp.VisibleNodeMask=1;
        D3D12_RESOURCE_DESC rd={}; rd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER; rd.Width=sizeof input;
        rd.Height=rd.DepthOrArraySize=rd.MipLevels=rd.SampleDesc.Count=1; rd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if (type==D3D12_HEAP_TYPE_DEFAULT) rd.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        return d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,state,nullptr,IID_PPV_ARGS(out));
    };
    ComPtr<ID3D12Resource> src,dst,readback;
    HR(buffer(D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ,&src));
    HR(buffer(D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,&dst));
    HR(buffer(D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST,&readback));
    UINT *mapped; HR(src->Map(0,nullptr,(void **)&mapped)); memcpy(mapped,input,sizeof input); src->Unmap(0,nullptr);
    D3D12_COMMAND_QUEUE_DESC qd={}; ComPtr<ID3D12CommandQueue> queue; HR(d->CreateCommandQueue(&qd,IID_PPV_ARGS(&queue)));
    ComPtr<ID3D12CommandAllocator> alloc; HR(d->CreateCommandAllocator(qd.Type,IID_PPV_ARGS(&alloc)));
    ComPtr<ID3D12GraphicsCommandList> list; HR(d->CreateCommandList(1,qd.Type,alloc.Get(),ps.Get(),IID_PPV_ARGS(&list)));
    list->SetComputeRootSignature(host);
    list->SetComputeRootShaderResourceView(0,src->GetGPUVirtualAddress());
    list->SetComputeRootUnorderedAccessView(1,dst->GetGPUVirtualAddress());
    list->SetComputeRoot32BitConstants(2,16,words,0);
    list->Dispatch(1,1,1);
    D3D12_RESOURCE_BARRIER b={}; b.Transition={dst.Get(),0,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE};
    list->ResourceBarrier(1,&b); list->CopyResource(readback.Get(),dst.Get()); HR(list->Close());
    ID3D12CommandList *lists[]={list.Get()}; queue->ExecuteCommandLists(1,lists);
    ComPtr<ID3D12Fence> fence; HR(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));
    HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr); CK(event);
    HR(queue->Signal(fence.Get(),1)); HR(fence->SetEventOnCompletion(1,event)); CK(WaitForSingleObject(event,10000)==WAIT_OBJECT_0);
    CloseHandle(event); CK(fence->GetCompletedValue()==1);
    HR(readback->Map(0,nullptr,(void **)&mapped)); for (UINT i=0;i<16;i++) CK(mapped[i]==(input[15-i]^words[i]));
    readback->Unmap(0,nullptr); CK(!root.Reset());
    puts("PASS public +80: checked wire, wrapper QI/name/lifetime; fenced t0/u0/b0 dispatch verified 16 distinct words");
    return 0;
}
