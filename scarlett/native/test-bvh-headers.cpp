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
    BYTE code[0x1488]; FILE *f=fopen("private-headers.bin","rb"); CK(f);
    CK(fread(code,1,sizeof code,f)==sizeof code && fgetc(f)==EOF && !ferror(f)); fclose(f);
    struct Wire { IUnknown *root; const void *cs; SIZE_T bytes; BYTE tail[32]; } w={root.Get(),code,sizeof code,{}};
    using Factory=HRESULT(WINAPI *)(IUnknown *,const Wire *,UINT64,UINT64,REFIID,void **);
    auto make=[&](){return SLOT(dev.Get(),0x1f0,Factory)(dev.Get(),&w,0,0,IID_ID3D12PipelineState,(void **)ps.GetAddressOf());};
    ++code[sizeof code-1]; CK(make()==E_NOTIMPL && !ps); --code[sizeof code-1];
    --w.bytes; CK(make()==E_NOTIMPL && !ps); ++w.bytes;
    w.tail[31]=1; CK(make()==E_NOTIMPL && !ps); w.tail[31]=0;
#ifdef TEST_NO_HEADERS_COMPILER
    CK(make()==E_NOTIMPL && !ps); puts("PASS unavailable compiler declined"); return 0;
#endif
    HR(make());
    using Name=HRESULT(WINAPI *)(IUnknown *,const WCHAR *);
    CK(SLOT(ps.Get(),0x30,Name)(ps.Get(),L"XDXR InitPartitionedHeaders")==E_NOTIMPL);
    HR(SLOT(ps.Get(),0x30,Name)(ps.Get(),L"XDXR InitHeaders"));
    ComPtr<ID3D12Device> d; HR(xs_bvh_root_host(dev.Get(),root.Get())->GetDevice(IID_PPV_ARGS(&d)));
    UINT expected[1024];
    for (UINT i=0;i<1024;i++) expected[i]=0x9da00000u+79*i;
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
    xs_bvh_range dr={dst.Get(),0x5511ffffff00ull,32,33*72-8};
    xs_bvh_range box={dst.Get(),0x4411ffffff00ull,2560,33*32-8};
    struct Cmd { UINT64 header,bounds; UINT flags,crc,n,remap,elements,parents; } cmd[33];
    static_assert(sizeof(Cmd)==40);
    for (UINT i=0;i<33;i++)
        cmd[i]={dr.guest_base+i*72,i%2 ? 0 : box.guest_base+(32-i)*32,i*117+31,i+200,3*i+9,~i,7*i,51-i};
    xs_bvh_hold *h=nullptr,*empty=nullptr,*single=nullptr;
    auto record=[&](){ return xs_record_bvh_headers(ps.Get(),list.Get(),&dr,&box,cmd,sizeof cmd,33,&h); };
#define NO(expr,err) do { h=(xs_bvh_hold*)1; CK((expr)==(err) && !h); } while(0)
    NO(xs_record_bvh_headers(ps.Get(),list.Get(),&dr,&box,(void*)1,40,1,&h),E_POINTER);
    NO(xs_record_bvh_headers(ps.Get(),list.Get(),&dr,&box,cmd,sizeof cmd-1,33,&h),E_INVALIDARG);
    NO(xs_record_bvh_headers(ps.Get(),list.Get(),&dr,&box,cmd,0,65,&h),E_NOTIMPL);
    UINT partitioned[10]={};
    NO(xs_record_bvh_init(ps.Get(),list.Get(),&dr,partitioned,sizeof partitioned,&h),E_INVALIDARG);
    cmd[32].header-=12; NO(record(),E_INVALIDARG); cmd[32].header+=12;
    cmd[32].bounds++; NO(record(),E_INVALIDARG); cmd[32].bounds--;
    cmd[32].bounds=dst->GetGPUVirtualAddress(); NO(record(),E_INVALIDARG); cmd[32].bounds=box.guest_base;
    box.resource_offset=32; NO(record(),E_INVALIDARG); box.resource_offset=2560;
    box.bytes-=4; NO(record(),E_INVALIDARG); box.bytes+=4;
    dr.bytes-=4; NO(record(),E_INVALIDARG); dr.bytes+=4;
    HR(xs_record_bvh_headers(ps.Get(),list.Get(),&dr,nullptr,nullptr,0,0,&empty)); CK(empty);
    HR(record()); CK(h);
    auto expect=[&](const Cmd &c,UINT header,UINT bounds) {
        const UINT max=0x7f7fffff,min=0xff7fffff;
        UINT words[]={c.flags,0,c.crc,c.n,max,max,max,min,min,min,0,c.remap,~0u,c.elements,c.parents,0x7f7f7f};
        memcpy(expected+header,words,sizeof words);
        if(c.bounds) memcpy(expected+bounds,words+4,24);
    };
    for (UINT i=0;i<33;i++) expect(cmd[i],8+i*18,640+(32-i)*8);
    xs_bvh_range one={dst.Get(),0x551200008000ull,3712,92};
    cmd[0].header=one.guest_base+28; cmd[0].bounds=one.guest_base;
    HR(xs_record_bvh_headers(ps.Get(),list.Get(),&one,nullptr,cmd,40,1,&single)); CK(single);
    expect(cmd[0],935,928);
    memset(cmd,0xff,sizeof cmd); ps.Reset();
    transition(D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);
    list->CopyResource(rb.Get(),dst.Get()); dst.Reset(); HR(list->Close());
    ID3D12CommandList *lists[]={list.Get()}; queue->ExecuteCommandLists(1,lists);
    ComPtr<ID3D12Fence> fence; HR(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));
    HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr); CK(event);
    HR(queue->Signal(fence.Get(),1)); HR(fence->SetEventOnCompletion(1,event)); CK(WaitForSingleObject(event,10000)==WAIT_OBJECT_0);
    CloseHandle(event); CK(fence->GetCompletedValue()==1); xs_drop_bvh_hold(h); xs_drop_bvh_hold(empty); xs_drop_bvh_hold(single);
    HR(rb->Map(0,nullptr,(void **)&mapped));
    for (UINT i=0;i<1024;i++) {
        if (expected[i]!=mapped[i]) { fprintf(stderr,"FAIL word %u got %08x expected %08x\n",i,mapped[i],expected[i]); return 1; }
    }
    rb->Unmap(0,nullptr);
    puts("PASS +1f0 InitHeaders: 34 headers, 18 extra boxes, shared and split windows, 1024 fenced words");
}
