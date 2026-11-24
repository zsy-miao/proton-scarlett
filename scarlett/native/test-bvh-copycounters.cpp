#include <d3d12.h>
#include <cstdio>
#include <cstdint>
#include <wrl/client.h>
#include "bvh_record.h"
using Microsoft::WRL::ComPtr;
extern "C" HRESULT WINAPI D3D12XboxCreateDevice(void *,const void *,REFIID,void **);
extern "C" ID3D12RootSignature *xs_bvh_root_host(IUnknown *,IUnknown *);
#define SLOT(o,n,T) ((T)((*reinterpret_cast<void***>(o))[n/8]))
#define CK(x) do { if (!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); return 1; } } while(0)
#define HR(x) CK(SUCCEEDED(x))
int main()
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
    BYTE code[0x10e8]; constexpr SIZE_T size=sizeof code;
    FILE *f=fopen("private-copycounters.bin","rb"); CK(f);
    CK(fread(code,1,size,f)==size && fgetc(f)==EOF && !ferror(f)); fclose(f);
    struct Wire { IUnknown *root; const void *cs; SIZE_T bytes; BYTE tail[32]; } w={root.Get(),code,size,{}};
    using Factory=HRESULT(WINAPI *)(IUnknown *,const Wire *,UINT64,UINT64,REFIID,void **);
    auto make=[&](){return SLOT(dev.Get(),0x1f0,Factory)(dev.Get(),&w,0,0,IID_ID3D12PipelineState,(void **)ps.GetAddressOf());};
    ++code[size-1]; CK(make()==E_NOTIMPL && !ps); --code[size-1];
    --w.bytes; CK(make()==E_NOTIMPL && !ps); ++w.bytes;
    w.tail[31]=1; CK(make()==E_NOTIMPL && !ps); w.tail[31]=0;
    HR(make());
    using Name=HRESULT(WINAPI *)(IUnknown *,const WCHAR *);
    CK(SLOT(ps.Get(),0x30,Name)(ps.Get(),L"XDXR WriteOfflinePartition")==E_NOTIMPL);
    HR(SLOT(ps.Get(),0x30,Name)(ps.Get(),L"XDXR CopyCounters"));
    ComPtr<ID3D12Device> d; HR(xs_bvh_root_host(dev.Get(),root.Get())->GetDevice(IID_PPV_ARGS(&d)));
    UINT expected[192];
    for (UINT i=0;i<192;i++) { expected[i]=0x9da00000u+79*i; }
    auto buffer=[&](ID3D12Device *owner,D3D12_HEAP_TYPE type,D3D12_RESOURCE_STATES state,ID3D12Resource **out) {
        D3D12_HEAP_PROPERTIES hp={}; hp.Type=type; hp.CreationNodeMask=hp.VisibleNodeMask=1;
        D3D12_RESOURCE_DESC rd={}; rd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER; rd.Width=sizeof expected;
        rd.Height=rd.DepthOrArraySize=rd.MipLevels=rd.SampleDesc.Count=1; rd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if(type==D3D12_HEAP_TYPE_DEFAULT) rd.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        return owner->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,state,nullptr,IID_PPV_ARGS(out));
    };
    ComPtr<ID3D12Resource> seed,dst,rb;
    HR(buffer(d.Get(),D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ,&seed));
    HR(buffer(d.Get(),D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_COPY_DEST,&dst));
    HR(buffer(d.Get(),D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST,&rb));
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
    ComPtr<ID3D12Resource> src;
    HR(buffer(d.Get(),D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ,&src));
    UINT words[192]; for (UINT i=0;i<192;i++) words[i]=0xab234567+97*i;
    HR(src->Map(0,nullptr,(void **)&mapped)); memcpy(mapped,words,sizeof words); src->Unmap(0,nullptr);
    xs_bvh_range sr={src.Get(),0x778800000000ull,48,33*16};
    auto record=[&](UINT n,ID3D12GraphicsCommandList *cl) { return xs_record_bvh_copy_counters(ps.Get(),cl,&dr,&sr,n,&h); };
    auto reject=[&]() { h=(xs_bvh_hold *)1; return record(33,list.Get())==E_INVALIDARG && !h; };
    --sr.bytes; CK(reject()); ++sr.bytes;
    --dr.bytes; CK(reject()); ++dr.bytes;
    ++sr.resource_offset; CK(reject()); --sr.resource_offset;
    ++dr.resource_offset; CK(reject()); --dr.resource_offset;
    sr.resource_offset=UINT64_MAX; CK(reject()); sr.resource_offset=48;
    sr.guest_base=UINT64_MAX-3; CK(reject()); sr.guest_base=0x778800000000ull;
    sr.resource=dst.Get(); CK(reject()); sr.resource=src.Get();
    dr.resource=seed.Get(); CK(reject()); dr.resource=dst.Get();
    CK(record(0,list.Get())==E_INVALIDARG && !h);
    CK(record(32u*D3D12_CS_DISPATCH_MAX_THREAD_GROUPS_PER_DIMENSION+1,list.Get())==E_NOTIMPL && !h);
    CK(xs_record_bvh_reset(ps.Get(),list.Get(),&dr,33,0,&h)==E_INVALIDARG && !h);

    // Distinct device on the same adapter; the default factory would reuse the singleton.
    const GUID factoryClass={0x114863bf,0xc386,0x4aee,{0xb3,0x9d,0x8f,0xb,0xbb,0x06,0x29,0x55}};
    auto get=(decltype(&D3D12GetInterface))(void *)GetProcAddress(GetModuleHandleW(L"d3d12.dll"),"D3D12GetInterface");
    ComPtr<ID3D12DeviceFactory> factory; CK(get); HR(get(factoryClass,IID_PPV_ARGS(&factory)));
    HR(factory->SetFlags(D3D12_DEVICE_FACTORY_FLAG_DISALLOW_STORING_NEW_DEVICE_AS_SINGLETON));
    ComPtr<ID3D12Device> other; HR(factory->CreateDevice(nullptr,D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&other))); CK(other!=d);
    ComPtr<ID3D12Resource> foreign;
    HR(buffer(other.Get(),D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_COMMON,&foreign));
    sr.resource=foreign.Get(); CK(reject()); sr.resource=src.Get();
    dr.resource=foreign.Get(); CK(reject()); dr.resource=dst.Get();
    ComPtr<ID3D12CommandAllocator> fa; HR(other->CreateCommandAllocator(qd.Type,IID_PPV_ARGS(&fa)));
    ComPtr<ID3D12GraphicsCommandList> fl; HR(other->CreateCommandList(1,qd.Type,fa.Get(),nullptr,IID_PPV_ARGS(&fl)));
    CK(record(33,fl.Get())==E_INVALIDARG && !h); HR(fl->Close());

    HR(record(33,list.Get())); CK(h); src.Reset();
    memcpy(expected+8,words+12,33*16);
    ps.Reset(); root.Reset();
    transition(D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);
    list->CopyResource(rb.Get(),dst.Get()); dst.Reset(); HR(list->Close());
    ID3D12CommandList *lists[]={list.Get()}; queue->ExecuteCommandLists(1,lists);
    ComPtr<ID3D12Fence> fence; HR(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));
    HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr); CK(event);
    HR(queue->Signal(fence.Get(),1)); HR(fence->SetEventOnCompletion(1,event)); CK(WaitForSingleObject(event,10000)==WAIT_OBJECT_0);
    CloseHandle(event); CK(fence->GetCompletedValue()==1); xs_drop_bvh_hold(h);
    HR(rb->Map(0,nullptr,(void **)&mapped)); CK(!memcmp(mapped,expected,sizeof expected)); rb->Unmap(0,nullptr);
    puts("PASS +1f0 CopyCounters: 33 counters, guarded source and destination slices, foreign devices rejected, 192 fenced DWORDs");
}
