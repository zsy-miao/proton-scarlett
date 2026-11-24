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
    BYTE code[0x1118]; FILE *f=fopen("private-beginpass.bin","rb"); CK(f);
    CK(fread(code,1,sizeof code,f)==sizeof code && fgetc(f)==EOF && !ferror(f)); fclose(f);
    struct Wire { IUnknown *root; const void *cs; SIZE_T bytes; BYTE tail[32]; } w={root.Get(),code,sizeof code,{}};
    using Factory=HRESULT(WINAPI *)(IUnknown *,const Wire *,UINT64,UINT64,REFIID,void **);
    auto make=[&](){return SLOT(dev.Get(),0x1f0,Factory)(dev.Get(),&w,0,0,IID_ID3D12PipelineState,(void **)ps.GetAddressOf());};
    ++code[sizeof code-1]; CK(make()==E_NOTIMPL && !ps); --code[sizeof code-1];
    --w.bytes; CK(make()==E_NOTIMPL && !ps); ++w.bytes;
    w.tail[31]=1; CK(make()==E_NOTIMPL && !ps); w.tail[31]=0;
    if (!GetProcAddress(GetModuleHandleW(L"d3d12core.dll"),"ScarlettBoundedBeginPassV1")) {
        CK(make()==E_NOTIMPL && !ps);
        puts("PASS synthetic +1f0 BeginPass: unmatched core declined"); return 0;
    }
    HR(make());
    using Name=HRESULT(WINAPI *)(IUnknown *,const WCHAR *);
    CK(SLOT(ps.Get(),0x30,Name)(ps.Get(),L"XDXR WriteOfflinePartition")==E_NOTIMPL);
    HR(SLOT(ps.Get(),0x30,Name)(ps.Get(),L"XDXR BeginPass"));
    ComPtr<ID3D12Device> d; HR(xs_bvh_root_host(dev.Get(),root.Get())->GetDevice(IID_PPV_ARGS(&d)));
    UINT expected[2][96];
    for (UINT k=0;k<2;k++) for (UINT i=0;i<96;i++) expected[k][i]=0x9da00000u+79*i+k;
    auto buffer=[&](D3D12_HEAP_TYPE type,D3D12_RESOURCE_STATES state,ID3D12Resource **out) {
        D3D12_HEAP_PROPERTIES hp={}; hp.Type=type; hp.CreationNodeMask=hp.VisibleNodeMask=1;
        D3D12_RESOURCE_DESC rd={}; rd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER; rd.Width=sizeof expected[0];
        rd.Height=rd.DepthOrArraySize=rd.MipLevels=rd.SampleDesc.Count=1; rd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if(type==D3D12_HEAP_TYPE_DEFAULT) rd.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        return d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,state,nullptr,IID_PPV_ARGS(out));
    };
    ComPtr<ID3D12Resource> seed[2],dst[2],rb[2];
    UINT *mapped;
    for (UINT k=0;k<2;k++) {
        HR(buffer(D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ,&seed[k]));
        HR(buffer(D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_COPY_DEST,&dst[k]));
        HR(buffer(D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST,&rb[k]));
        HR(seed[k]->Map(0,nullptr,(void **)&mapped)); memcpy(mapped,expected[k],sizeof expected[k]); seed[k]->Unmap(0,nullptr);
    }
    D3D12_COMMAND_QUEUE_DESC qd={}; qd.Type=D3D12_COMMAND_LIST_TYPE_COMPUTE;
    ComPtr<ID3D12CommandQueue> queue; HR(d->CreateCommandQueue(&qd,IID_PPV_ARGS(&queue)));
    ComPtr<ID3D12CommandAllocator> alloc; HR(d->CreateCommandAllocator(qd.Type,IID_PPV_ARGS(&alloc)));
    ComPtr<ID3D12GraphicsCommandList> list; HR(d->CreateCommandList(1,qd.Type,alloc.Get(),nullptr,IID_PPV_ARGS(&list)));
    auto transition=[&](UINT k,D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after) {
        D3D12_RESOURCE_BARRIER b={}; b.Transition={dst[k].Get(),0,before,after}; list->ResourceBarrier(1,&b);
    };
    for (UINT k=0;k<2;k++) {
        list->CopyResource(dst[k].Get(),seed[k].Get());
        transition(k,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }
    xs_bvh_range t={dst[0].Get(),0x551100000000ull,32,56}, c={dst[1].Get(),0x8b2200000000ull,48,32};
    // Interior guest VAs, distinct from each other and from host resource offsets.
    UINT words[]={16,0x5511,8,0x8b22,0xfffffffeu};
    xs_bvh_hold *holds[2]={};
    auto record=[&](UINT i){ return xs_record_bvh_beginpass(ps.Get(),list.Get(),&t,&c,words,sizeof words,&holds[i]); };
    --c.bytes; CK(record(0)==E_INVALIDARG && !holds[0]); ++c.bytes;
    t.bytes-=4; CK(record(0)==E_INVALIDARG && !holds[0]); t.bytes+=4;
    HR(record(0)); // Global path has only 40 TLAS bytes from tlasVA; +64 is not readable.
    words[2]=44; c.bytes=68; words[4]=3; t.bytes=208;
    t.bytes-=4; CK(record(1)==E_INVALIDARG && !holds[1]); t.bytes+=4;
    words[4]=0x40000000; CK(record(1)==E_INVALIDARG && !holds[1]); words[4]=3;
    auto saved=c.guest_base; c.guest_base=t.guest_base; CK(record(1)==E_INVALIDARG && !holds[1]); c.guest_base=saved;
    HR(record(1)); memset(words,0,sizeof words);
    auto box=[&](UINT k,UINT at){ for (UINT i=0;i<6;i++) expected[k][at/4+i]=i<3?0x7f7fffffu:0xff7fffffu; };
    box(0,64); box(0,216); box(1,56); box(1,92);
    // Exercise the GPU checks independently of recorder preflight. Both second Store3s
    // straddle the ends of their 20-byte windows and must be wholly suppressed.
    UINT guard_words[]={16,0x5511,108,0x8b22,2};
    UINT guard_windows[]={80,0x5511,8,112, 156,0x5511,20,188, 108,0x8b22,20,156};
    list->SetComputeRoot32BitConstants(1,5,guard_words,0);
    list->SetComputeRoot32BitConstants(2,12,guard_windows,0);
    list->Dispatch(1,1,1);
    for (UINT i=0;i<3;i++) { expected[0][188/4+i]=0x7f7fffff; expected[1][156/4+i]=0x7f7fffff; }
    ps.Reset(); root.Reset();
    for (UINT k=0;k<2;k++) {
        transition(k,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);
        list->CopyResource(rb[k].Get(),dst[k].Get()); dst[k].Reset();
    }
    HR(list->Close());
    ID3D12CommandList *lists[]={list.Get()}; queue->ExecuteCommandLists(1,lists);
    ComPtr<ID3D12Fence> fence; HR(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));
    HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr); CK(event);
    HR(queue->Signal(fence.Get(),1)); HR(fence->SetEventOnCompletion(1,event)); CK(WaitForSingleObject(event,10000)==WAIT_OBJECT_0);
    CloseHandle(event); CK(fence->GetCompletedValue()==1);
    for (UINT k=0;k<2;k++) {
        xs_drop_bvh_hold(holds[k]); HR(rb[k]->Map(0,nullptr,(void **)&mapped));
        for (UINT i=0;i<96;i++) if (mapped[i]!=expected[k][i]) {
            fprintf(stderr,"buffer %u word %u: got %08x expected %08x\n",k,i,mapped[i],expected[k][i]); return 1;
        }
        rb[k]->Unmap(0,nullptr);
    }
    puts("PASS synthetic +1f0 BeginPass: original shader, global and partition 3, short Store3s suppressed, two original buffers, all 192 fenced DWORDs");
}
