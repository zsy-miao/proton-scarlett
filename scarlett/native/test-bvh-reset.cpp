#include <d3d12.h>
#include <cstdio>
#include <cstdint>
#include <cstddef>
#include <wrl/client.h>
#include "bvh_record.h"
using Microsoft::WRL::ComPtr;
extern "C" HRESULT WINAPI D3D12XboxCreateDevice(void *,const void *,REFIID,void **);
extern "C" ID3D12RootSignature *xs_bvh_root_host(IUnknown *,IUnknown *);
#define SLOT(o,n,T) ((T)((*reinterpret_cast<void***>(o))[n/8]))
#define CK(x) do { if (!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); return 1; } } while(0)
#define HR(x) CK(SUCCEEDED(x))
static int run(bool index)
{
    DWORD params[20]={}; params[0]=0x20004; params[2]=0x100000; params[10]=1; params[15]=0x20;
    const IID boot={0x5405c344,0xd457,0x444e,{0xb4,0xdd,0x23,0x66,0xe4,0x5a,0xee,0x39}};
    ComPtr<IUnknown> dev,root,ps;
    HR(D3D12XboxCreateDevice(nullptr,params,boot,(void **)dev.GetAddressOf()));
    D3D12_ROOT_PARAMETER1 rp[3]={};
    rp[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV; rp[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV;
    rp[2].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS; rp[2].Constants.Num32BitValues=16;
    D3D12_VERSIONED_ROOT_SIGNATURE_DESC sig={}; sig.Version=D3D_ROOT_SIGNATURE_VERSION_1_1;
    sig.Desc_1_1={3,rp,0,nullptr,(D3D12_ROOT_SIGNATURE_FLAGS)0x03000000};
    ComPtr<ID3DBlob> blob; HR(D3D12SerializeVersionedRootSignature(&sig,&blob,nullptr));
    using Root=HRESULT(WINAPI *)(IUnknown *,UINT,const void *,SIZE_T,REFIID,void **);
    HR(SLOT(dev.Get(),0x80,Root)(dev.Get(),1,blob->GetBufferPointer(),blob->GetBufferSize(),IID_ID3D12RootSignature,(void **)root.GetAddressOf()));
    BYTE code[0x1004]; size_t bytes=index?sizeof code:0xfe8;
    FILE *f=fopen(index?"private-index.bin":"private-reset.bin","rb"); CK(f);
    CK(fread(code,1,bytes,f)==bytes && fgetc(f)==EOF && !ferror(f)); fclose(f);
    struct Wire { IUnknown *root; const void *cs; SIZE_T bytes; BYTE tail[32]; } w={root.Get(),code,bytes,{}};
    using Factory=HRESULT(WINAPI *)(IUnknown *,const Wire *,UINT64,UINT64,REFIID,void **);
    auto make=[&](){return SLOT(dev.Get(),0x1f0,Factory)(dev.Get(),&w,0,0,IID_ID3D12PipelineState,(void **)ps.GetAddressOf());};
    ++code[bytes-1]; CK(make()==E_NOTIMPL && !ps); --code[bytes-1];
    --w.bytes; CK(make()==E_NOTIMPL && !ps); ++w.bytes;
    w.tail[31]=1; CK(make()==E_NOTIMPL && !ps); w.tail[31]=0;
    HR(make());
    using Name=HRESULT(WINAPI *)(IUnknown *,const WCHAR *);
    CK(SLOT(ps.Get(),0x30,Name)(ps.Get(),L"XDXR WriteOfflinePartition")==E_NOTIMPL);
    HR(SLOT(ps.Get(),0x30,Name)(ps.Get(),index?L"XDXR ResetToIndexBuffer":L"XDXR ResetCounters"));
    ComPtr<ID3D12Device> d; HR(xs_bvh_root_host(dev.Get(),root.Get())->GetDevice(IID_PPV_ARGS(&d)));
    UINT expected[192];
    for (UINT i=0;i<192;i++) { expected[i]=0x9da00000u+79*i; }
    auto buffer=[&](D3D12_HEAP_TYPE type,D3D12_RESOURCE_STATES state,ID3D12Resource **out) {
        D3D12_HEAP_PROPERTIES hp={}; hp.Type=type; hp.CreationNodeMask=hp.VisibleNodeMask=1;
        D3D12_RESOURCE_DESC rd={}; rd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER; rd.Width=sizeof expected;
        rd.Height=rd.DepthOrArraySize=rd.MipLevels=rd.SampleDesc.Count=1; rd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if(type==D3D12_HEAP_TYPE_DEFAULT) rd.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        return d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,state,nullptr,IID_PPV_ARGS(out));
    };
    ComPtr<ID3D12Resource> seed,dst,rb;
    HR(buffer(D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ,&seed));
    HR(buffer(D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_COPY_DEST,&dst));
    HR(buffer(D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST,&rb));
    UINT *mapped; HR(seed->Map(0,nullptr,(void **)&mapped)); memcpy(mapped,expected,sizeof expected); seed->Unmap(0,nullptr);
    D3D12_COMMAND_QUEUE_DESC qd={}; qd.Type=D3D12_COMMAND_LIST_TYPE_COMPUTE;
    ComPtr<ID3D12CommandQueue> queue; HR(d->CreateCommandQueue(&qd,IID_PPV_ARGS(&queue)));
    ComPtr<ID3D12CommandAllocator> alloc; HR(d->CreateCommandAllocator(qd.Type,IID_PPV_ARGS(&alloc)));
    ComPtr<ID3D12GraphicsCommandList> list; HR(d->CreateCommandList(1,qd.Type,alloc.Get(),nullptr,IID_PPV_ARGS(&list)));
    list->CopyResource(dst.Get(),seed.Get());
    auto transition=[&](D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after) {
        D3D12_RESOURCE_BARRIER b={}; b.Transition={dst.Get(),0,before,after}; list->ResourceBarrier(1,&b);
    };
    transition(D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    xs_bvh_range dr={dst.Get(),0x551100000000ull,32,33*16};
    xs_bvh_hold *h=nullptr;
    --dr.bytes; CK(xs_record_bvh_reset(ps.Get(),list.Get(),&dr,33,0xab234567,&h)==E_INVALIDARG && !h); ++dr.bytes;
    const UINT first=0xffffffc0u;
    const UINT count=33;
    auto reset=[&](){return xs_record_bvh_reset(ps.Get(),list.Get(),&dr,count,first,&h);};
    if (index) HR(reset());
    else {
        // XboxBvhBuilder.dll +2093a..+20990: cursor < dispatch limit, four DWORDs, cursor += 16.
        UINT words[5]={};
        struct Cursor { BYTE pad[0x18]; UINT *write; UINT64 unused; UINT *limit; };
        static_assert(offsetof(Cursor,write)==0x18 && offsetof(Cursor,limit)==0x28);
        Cursor guest={{},words,0,words+1}; // last permitted packet start is words[0]
        CK(guest.write < guest.limit);
        const UINT direct[]={0x4002c400,2,1,1};
        memcpy(guest.write,direct,sizeof direct); guest.write+=4;
        auto record=[&]() -> HRESULT {
            // Read only owned storage, and only after qualifying the entire written span.
            if (guest.write!=words+4 || words[0]!=0x4002c400 ||
                    words[1]!=(count+31)/32 || words[2]!=1 || words[3]!=1) return E_INVALIDARG;
            return reset();
        };
        for (UINT i=0;i<4;++i) {
            words[i]=i ? direct[i]+1 : 0x4002e800;
            CK(record()==E_INVALIDARG && !h); words[i]=direct[i];
        }
        --guest.write; CK(record()==E_INVALIDARG && !h);
        guest.write+=2; CK(record()==E_INVALIDARG && !h); // valid prefix plus trailing word
        --guest.write; HR(record());
    }
    for(UINT i=8;i<8+33*4;i++) expected[i]=first+(index?i-8:0);
    ps.Reset(); root.Reset();
    transition(D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);
    list->CopyResource(rb.Get(),dst.Get()); dst.Reset(); HR(list->Close());
    ID3D12CommandList *lists[]={list.Get()}; queue->ExecuteCommandLists(1,lists);
    ComPtr<ID3D12Fence> fence; HR(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));
    HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr); CK(event);
    HR(queue->Signal(fence.Get(),1)); HR(fence->SetEventOnCompletion(1,event)); CK(WaitForSingleObject(event,10000)==WAIT_OBJECT_0);
    CloseHandle(event); CK(fence->GetCompletedValue()==1); xs_drop_bvh_hold(h);
    HR(rb->Map(0,nullptr,(void **)&mapped)); CK(!memcmp(mapped,expected,sizeof expected)); rb->Unmap(0,nullptr);
    printf("PASS synthetic +1f0 %s: unaltered bytecode, two workgroups, short range rejected, all 192 fenced DWORDs\n",
            index?"ResetToIndexBuffer":"ResetCounters via guest direct packet");
    return 0;
}
int main() { return run(false) || run(true); }
