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
    BYTE code[0x126c]; FILE *f=fopen("private-copy-update.bin","rb"); CK(f);
    CK(fread(code,1,sizeof code,f)==sizeof code && fgetc(f)==EOF && !ferror(f)); fclose(f);
    struct Wire { IUnknown *root; const void *cs; SIZE_T bytes; BYTE tail[32]; } w={root.Get(),code,sizeof code,{}};
    using Factory=HRESULT(WINAPI *)(IUnknown *,const Wire *,UINT64,UINT64,REFIID,void **);
    auto make=[&](){return SLOT(dev.Get(),0x1f0,Factory)(dev.Get(),&w,0,0,IID_ID3D12PipelineState,(void **)ps.GetAddressOf());};
    ++code[sizeof code-1]; CK(make()==E_NOTIMPL && !ps); --code[sizeof code-1];
    --w.bytes; CK(make()==E_NOTIMPL && !ps); ++w.bytes;
    w.tail[31]=1; CK(make()==E_NOTIMPL && !ps); w.tail[31]=0;
    HR(make());
    using Name=HRESULT(WINAPI *)(IUnknown *,const WCHAR *);
    CK(SLOT(ps.Get(),0x30,Name)(ps.Get(),L"XDXR WriteOfflinePartition")==E_NOTIMPL);
    HR(SLOT(ps.Get(),0x30,Name)(ps.Get(),L"XDXR CopyForUpdate"));
    ComPtr<ID3D12Device> d; HR(xs_bvh_root_host(dev.Get(),root.Get())->GetDevice(IID_PPV_ARGS(&d)));
    UINT expected[384];
    for (UINT i=0;i<384;i++) { expected[i]=0x9da00000u+79*i; }
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
    UINT src[160]; // 40 chunks: eight lanes must take a second iteration.
    xs_bvh_range dr={dst.Get(),0x551100000020ull,32,sizeof src};
    xs_bvh_hold *h[3]={};
    auto record=[&](const void *s,SIZE_T bytes,UINT reset,xs_bvh_hold **hold) {
        return xs_record_bvh_copy_update(ps.Get(),list.Get(),&dr,s,bytes,reset,hold);
    };
    src[1]=0x04000009; // Would wrap to nine in the shader's 32-bit shift.
    CK(record(src,sizeof src,0,h)==E_INVALIDARG && !*h);
    src[1]=9;
    CK(record((void *)1,sizeof src,0,h)==E_POINTER && !*h);
    --dr.bytes; CK(record(src,sizeof src,1,h)==E_INVALIDARG && !*h); ++dr.bytes;
    CK(record(src,sizeof src,2,h)==E_INVALIDARG && !*h);
    for (UINT i=0;i<3;i++) {
        for (UINT j=0;j<160;j++) src[j]=0x27100000+(i<<16)+43*j;
        const UINT n=i==2?64:sizeof src;
        src[1]=n/64-1;
        HR(record(src,n,i==1,&h[i])); CK(h[i]);
        UINT *out=expected+dr.resource_offset/4;
        memcpy(out,src,n);
        if (i==1) {
            for (UINT j=4;j<7;j++) out[j]=0x7f7fffff;
            for (UINT j=7;j<10;j++) out[j]=0xff7fffff;
        }
        dr.guest_base+=672;
        dr.resource_offset+=672;
        dr.bytes=i==1?64:sizeof src;
    }
    memset(src,0xff,sizeof src);
    ps.Reset(); root.Reset();
    transition(D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);
    list->CopyResource(rb.Get(),dst.Get()); dst.Reset(); HR(list->Close());
    ID3D12CommandList *lists[]={list.Get()}; queue->ExecuteCommandLists(1,lists);
    ComPtr<ID3D12Fence> fence; HR(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));
    HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr); CK(event);
    HR(queue->Signal(fence.Get(),1)); HR(fence->SetEventOnCompletion(1,event)); CK(WaitForSingleObject(event,10000)==WAIT_OBJECT_0);
    CloseHandle(event); CK(fence->GetCompletedValue()==1); for(auto hold:h) xs_drop_bvh_hold(hold);
    HR(rb->Map(0,nullptr,(void **)&mapped));
    for (UINT i=0;i<384;i++) if (expected[i]!=mapped[i]) {
        fprintf(stderr,"FAIL word %u: expected %08x got %08x\n",i,expected[i],mapped[i]); return 1;
    }
    rb->Unmap(0,nullptr);
    puts("PASS +1f0 CopyForUpdate: 640-byte copies with and without bounds reset, header-only copy, guards, 384 fenced words");
}
