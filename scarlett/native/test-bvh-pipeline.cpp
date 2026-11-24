#include <d3d12.h>
#include <cstdio>
#include <wrl/client.h>
#include "bvh_record.h"
using Microsoft::WRL::ComPtr;
extern "C" HRESULT WINAPI D3D12XboxCreateDevice(void *,const void *,REFIID,void **);
extern "C" ID3D12RootSignature *xs_bvh_root_host(IUnknown *,IUnknown *);
extern "C" const IID xs_sync_list_id;
#define SLOT(o,n,T) ((T)((*reinterpret_cast<void***>(o))[n/8]))
#define CK(x) do { if (!(x)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); return 1; } } while(0)
#define HR(x) CK(SUCCEEDED(x))
int main()
{
    DWORD params[20]={}; params[0]=0x20004; params[2]=0x100000; params[10]=1; params[15]=0x20;
    const IID boot={0x5405c344,0xd457,0x444e,{0xb4,0xdd,0x23,0x66,0xe4,0x5a,0xee,0x39}};
    ComPtr<IUnknown> dev,other,root,ps;
    HR(D3D12XboxCreateDevice(nullptr,params,boot,(void **)dev.GetAddressOf()));
    HR(D3D12XboxCreateDevice(nullptr,params,boot,(void **)other.GetAddressOf()));
    D3D12_ROOT_PARAMETER1 p[3]={};
    p[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV; p[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV;
    p[2].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS; p[2].Constants.Num32BitValues=16;
    D3D12_VERSIONED_ROOT_SIGNATURE_DESC sig={}; sig.Version=D3D_ROOT_SIGNATURE_VERSION_1_1;
    sig.Desc_1_1={3,p,0,nullptr,(D3D12_ROOT_SIGNATURE_FLAGS)0x03000000};
    ComPtr<ID3DBlob> blob; HR(D3D12SerializeVersionedRootSignature(&sig,&blob,nullptr));
    using Root=HRESULT(WINAPI *)(IUnknown *,UINT,const void *,SIZE_T,REFIID,void **);
    HR(SLOT(dev.Get(),0x80,Root)(dev.Get(),1,blob->GetBufferPointer(),blob->GetBufferSize(),IID_ID3D12RootSignature,(void **)root.GetAddressOf()));
    BYTE code[0x1410]; FILE *file=fopen("private-builder.bin","rb"); CK(file);
    CK(fread(code,1,sizeof code,file)==sizeof code && fgetc(file)==EOF && !ferror(file)); fclose(file);
    struct Wire { IUnknown *root; const void *data; SIZE_T n; BYTE tail[32]; } w={root.Get(),code,sizeof code,{}};
    using Factory=HRESULT(WINAPI *)(IUnknown *,const Wire *,UINT64,UINT64,REFIID,void **);
    auto make=SLOT(dev.Get(),0x1f0,Factory);
    void *bad;
#define REFUSE(expr,hr) do { bad=dev.Get(); CK((expr)==(hr) && !bad); } while(0)
    CK(make(dev.Get(),&w,0,0,IID_ID3D12PipelineState,nullptr)==E_POINTER);
    REFUSE(make(dev.Get(),&w,0,0,IID_IUnknown,&bad),E_NOINTERFACE);
    REFUSE(make(dev.Get(),nullptr,0,0,IID_ID3D12PipelineState,&bad),E_POINTER);
    REFUSE(make(dev.Get(),&w,0,1,IID_ID3D12PipelineState,&bad),E_NOTIMPL);
    REFUSE(make(dev.Get(),&w,1,0,IID_ID3D12PipelineState,&bad),E_NOTIMPL);
    REFUSE(make(other.Get(),&w,0,0,IID_ID3D12PipelineState,&bad),E_INVALIDARG);
    w.root=dev.Get(); REFUSE(make(dev.Get(),&w,0,0,IID_ID3D12PipelineState,&bad),E_INVALIDARG); w.root=root.Get();
    w.data=(void *)1; REFUSE(make(dev.Get(),&w,0,0,IID_ID3D12PipelineState,&bad),E_POINTER); w.data=code;
    --w.n; REFUSE(make(dev.Get(),&w,0,0,IID_ID3D12PipelineState,&bad),E_NOTIMPL); ++w.n;
    for(auto &v:w.tail) { v=1; REFUSE(make(dev.Get(),&w,0,0,IID_ID3D12PipelineState,&bad),E_NOTIMPL); v=0; }
    code[0xa00]^=1; REFUSE(make(dev.Get(),&w,0,0,IID_ID3D12PipelineState,&bad),E_NOTIMPL); code[0xa00]^=1;
    HR(make(dev.Get(),&w,0,0,IID_ID3D12PipelineState,(void **)ps.GetAddressOf()));
    memset(code,0,sizeof code);
    IUnknown *alias=nullptr; HR(ps->QueryInterface(IID_ID3D12PipelineState,(void **)&alias)); CK(alias==ps.Get() && alias->Release()==1);
    HR(ps->QueryInterface(IID_IUnknown,(void **)&alias)); CK(alias==ps.Get() && alias->Release()==1);
    REFUSE(ps->QueryInterface(IID_ID3D12Device,&bad),E_NOINTERFACE);
    using Name=HRESULT(WINAPI *)(IUnknown *,const WCHAR *);
    CK(SLOT(ps.Get(),0x30,Name)(ps.Get(),nullptr)==E_POINTER);
    CK(SLOT(ps.Get(),0x30,Name)(ps.Get(),L"XDXR InitPartitionedHeaders?")==E_NOTIMPL);
    HR(SLOT(ps.Get(),0x30,Name)(ps.Get(),L"XDXR InitPartitionedHeaders"));
    using Alloc=HRESULT(WINAPI *)(IUnknown *,D3D12_COMMAND_LIST_TYPE,REFIID,void **);
    using List=HRESULT(WINAPI *)(IUnknown *,const void *,IUnknown *,IUnknown *,REFIID,void **);
    ComPtr<IUnknown> allocWrapper;
    HR(SLOT(dev.Get(),0x48,Alloc)(dev.Get(),D3D12_COMMAND_LIST_TYPE_COMPUTE,IID_ID3D12CommandAllocator,
            (void **)allocWrapper.GetAddressOf()));
    const UINT startup[]={D3D12_COMMAND_LIST_TYPE_COMPUTE,2,1,0};
    REFUSE(SLOT(dev.Get(),0x248,List)(dev.Get(),startup,allocWrapper.Get(),ps.Get(),xs_sync_list_id,&bad),E_NOTIMPL);
    allocWrapper.Reset();
    ComPtr<ID3D12Device> d; HR(xs_bvh_root_host(dev.Get(),root.Get())->GetDevice(IID_PPV_ARGS(&d)));
    CK(root.Reset()==0 && dev.Reset()==1); other.Reset();
    puts("PASS +1f0 factory, schema, digest, root owner, QI, name and ownership; guest +248 PSO rejected");
    const UINT cases[]={0,1,5};
    for(UINT count: cases) {
        UINT expected[96]; for(UINT i=0;i<96;i++) expected[i]=0xa0000213+169*i;
        auto buffer=[&](D3D12_HEAP_TYPE type,D3D12_RESOURCE_STATES state,ID3D12Resource **out) {
            D3D12_HEAP_PROPERTIES hp={}; hp.Type=type; hp.CreationNodeMask=hp.VisibleNodeMask=1;
            D3D12_RESOURCE_DESC rd={}; rd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER; rd.Width=sizeof expected;
            rd.Height=rd.DepthOrArraySize=rd.MipLevels=rd.SampleDesc.Count=1; rd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            if(type==D3D12_HEAP_TYPE_DEFAULT) rd.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
            return d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,state,nullptr,IID_PPV_ARGS(out));
        };
        ComPtr<ID3D12Resource> seed,dst,readback;
        HR(buffer(D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ,&seed));
        HR(buffer(D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_COPY_DEST,&dst));
        HR(buffer(D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST,&readback));
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
        const UINT64 guest=0x5eee00000000ull; UINT size=count ? 84+28*(count-1) : 80;
        xs_bvh_range range={dst.Get(),guest,16,16+size};
        struct { UINT64 addr; UINT f,n,nodes,elements,parents,count,per,global; } init=
            {guest+16,0x59a6a0+count,137+count,230+count,340+count,550+count,count,89+count,113+count};
        xs_bvh_hold *hold=nullptr;
        auto record=[&]() { return xs_record_bvh_init(ps.Get(),list.Get(),&range,&init,sizeof init,&hold); };
#define NO_RECORD(expr,hr) do { hold=(xs_bvh_hold *)1; CK((expr)==(hr) && !hold); } while(0)
        NO_RECORD(xs_record_bvh_init(ps.Get(),list.Get(),&range,(void *)1,sizeof init,&hold),E_POINTER);
        NO_RECORD(xs_record_bvh_init(ps.Get(),list.Get(),&range,&init,sizeof init-1,&hold),E_INVALIDARG);
        range.resource=seed.Get(); NO_RECORD(record(),E_INVALIDARG); range.resource=dst.Get();
        --range.bytes; NO_RECORD(record(),E_INVALIDARG); ++range.bytes;
        range.resource_offset=~UINT64(0); NO_RECORD(record(),E_INVALIDARG); range.resource_offset=16;
        range.guest_base=~UINT64(8); NO_RECORD(record(),E_INVALIDARG); range.guest_base=guest;
        init.addr=guest-4; NO_RECORD(record(),E_INVALIDARG);
        init.addr=guest+4; NO_RECORD(record(),E_INVALIDARG);
        init.addr=dst->GetGPUVirtualAddress()+32; CK(init.addr!=guest+16); NO_RECORD(record(),E_INVALIDARG);
        init.addr=guest+16; init.count=65537; NO_RECORD(record(),E_NOTIMPL); init.count=count;
        UINT head[]={init.f,0,0,init.n,0x7f7fffff,0x7f7fffff,0x7f7fffff,0xff7fffff,0xff7fffff,0xff7fffff,
            init.nodes,0,0xffffffff,init.elements,init.parents,0x007f7f7f,count,init.per,init.global,0};
        memcpy(expected+8,head,sizeof head); for(UINT j=0;j<count;j++) expected[28+j*7]=0;
        HR(record()); CK(hold); memset(&init,0xff,sizeof init); range={};
        if(count==5) CK(ps.Reset()==1);
        transition(D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);
        list->CopyResource(readback.Get(),dst.Get()); dst.Reset(); HR(list->Close());
        ID3D12CommandList *lists[]={list.Get()}; queue->ExecuteCommandLists(1,lists);
        ComPtr<ID3D12Fence> fence; HR(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));
        HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr); CK(event);
        HR(queue->Signal(fence.Get(),1)); HR(fence->SetEventOnCompletion(1,event)); CK(WaitForSingleObject(event,10000)==WAIT_OBJECT_0);
        CloseHandle(event); CK(fence->GetCompletedValue()==1); xs_drop_bvh_hold(hold);
        HR(readback->Map(0,nullptr,(void**)&mapped)); CK(!memcmp(mapped,expected,sizeof expected)); readback->Unmap(0,nullptr);
        printf("PASS partitions=%u: exact resolved subrange and all 96 seeded words; snapshot mutated before execution\n",count);
    }
    return 0;
}
