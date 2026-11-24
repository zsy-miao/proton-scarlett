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
    BYTE code[0x493c]; FILE *f=fopen("private-instances.bin","rb"); CK(f);
    CK(fread(code,1,sizeof code,f)==sizeof code && fgetc(f)==EOF && !ferror(f)); fclose(f);
    struct Wire { IUnknown *root; const void *cs; SIZE_T bytes; BYTE tail[32]; } w={root.Get(),code,sizeof code,{}};
    using Factory=HRESULT(WINAPI *)(IUnknown *,const Wire *,UINT64,UINT64,REFIID,void **);
    auto make=[&](){return SLOT(dev.Get(),0x1f0,Factory)(dev.Get(),&w,0,0,IID_ID3D12PipelineState,(void **)ps.GetAddressOf());};
    ++code[sizeof code-1]; CK(make()==E_NOTIMPL && !ps); --code[sizeof code-1];
    --w.bytes; CK(make()==E_NOTIMPL && !ps); ++w.bytes;
    w.tail[31]=1; CK(make()==E_NOTIMPL && !ps); w.tail[31]=0;
#ifdef TEST_NO_INSTANCE_COMPILER
    CK(make()==E_NOTIMPL && !ps); puts("PASS unavailable compiler declined"); return 0;
#endif
    HR(make());
    using Name=HRESULT(WINAPI *)(IUnknown *,const WCHAR *);
    CK(SLOT(ps.Get(),0x30,Name)(ps.Get(),L"XDXR WriteOfflinePartition")==E_NOTIMPL);
    HR(SLOT(ps.Get(),0x30,Name)(ps.Get(),L"XDXR WriteInstances"));
    ComPtr<ID3D12Device> d; HR(xs_bvh_root_host(dev.Get(),root.Get())->GetDevice(IID_PPV_ARGS(&d)));
    UINT expected[192],inputs[192];
    for (UINT i=0;i<192;i++) { expected[i]=0x9da00000u+79*i; inputs[i]=0x12340000u+17*i; }
    UINT *header=expected+8; /* TLAS view begins +32, surrounding canaries included. */
    header[3]=4; header[13]=128;
    xs_bvh_range sr={nullptr,0x6699fffffe00ull,12,320};
    struct Cmd { float x[12]; UINT aabb[6],id,hit,flags,index; UINT64 va; };
    static_assert(sizeof(Cmd)==96);
    Cmd cmd[4]={};
    for (UINT i=0;i<4;i++) {
        cmd[i].x[0]=cmd[i].x[5]=cmd[i].x[10]=1;
        cmd[i].id=0xa1774422+i; cmd[i].hit=0x89234560+i; cmd[i].index=i; cmd[i].flags=0xbeef;
        for(auto &w:cmd[i].aabb) w=0xffff7799;
    }
    cmd[0].x[10]=0; /* singular, null BLAS */
    cmd[1].va=sr.guest_base; cmd[1].x[3]=4; cmd[1].x[7]=-2; cmd[1].x[11]=8;
    cmd[3].va=sr.guest_base+256;
    float box[6]={2,-2,-4,4,2,-2}; memcpy(inputs+7,box,24);
    inputs[3+12]=0xd2345678; inputs[3+15]=0xc07f7e80;
    inputs[67+12]=0x92341234; inputs[67+15]=0x01000000;
    auto buffer=[&](D3D12_HEAP_TYPE type,D3D12_RESOURCE_STATES state,ID3D12Resource **out) {
        D3D12_HEAP_PROPERTIES hp={}; hp.Type=type; hp.CreationNodeMask=hp.VisibleNodeMask=1;
        D3D12_RESOURCE_DESC rd={}; rd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER; rd.Width=sizeof expected;
        rd.Height=rd.DepthOrArraySize=rd.MipLevels=rd.SampleDesc.Count=1; rd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if(type==D3D12_HEAP_TYPE_DEFAULT) rd.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        return d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,state,nullptr,IID_PPV_ARGS(out));
    };
    ComPtr<ID3D12Resource> seed,dst,source,rb;
    HR(buffer(D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ,&seed));
    HR(buffer(D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ,&source));
    HR(buffer(D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_COPY_DEST,&dst));
    HR(buffer(D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST,&rb));
    UINT *mapped; HR(seed->Map(0,nullptr,(void **)&mapped)); memcpy(mapped,expected,sizeof expected); seed->Unmap(0,nullptr);
    HR(source->Map(0,nullptr,(void **)&mapped)); memcpy(mapped,inputs,sizeof inputs); source->Unmap(0,nullptr);
    D3D12_COMMAND_QUEUE_DESC qd={}; qd.Type=D3D12_COMMAND_LIST_TYPE_COMPUTE;
    ComPtr<ID3D12CommandQueue> queue; HR(d->CreateCommandQueue(&qd,IID_PPV_ARGS(&queue)));
    ComPtr<ID3D12CommandAllocator> alloc; HR(d->CreateCommandAllocator(qd.Type,IID_PPV_ARGS(&alloc)));
    ComPtr<ID3D12GraphicsCommandList> list; HR(d->CreateCommandList(1,qd.Type,alloc.Get(),nullptr,IID_PPV_ARGS(&list)));
    list->CopyResource(dst.Get(),seed.Get());
    auto transition=[&](D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after) {
        D3D12_RESOURCE_BARRIER b={}; b.Transition={dst.Get(),0,before,after}; list->ResourceBarrier(1,&b);
    };
    transition(D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    xs_bvh_range dr={dst.Get(),0x551100000000ull,32,640}; sr.resource=source.Get();
    xs_bvh_hold *h=nullptr,*empty=nullptr;
    auto record=[&](){ return xs_record_bvh_instances(ps.Get(),list.Get(),&dr,header,&sr,cmd,sizeof cmd,4,&h); };
#define NO(expr,err) do { h=(xs_bvh_hold*)1; CK((expr)==(err) && !h); } while(0)
    NO(xs_record_bvh_instances(ps.Get(),list.Get(),&dr,header,&sr,(void*)1,96,1,&h),E_POINTER);
    NO(xs_record_bvh_instances(ps.Get(),list.Get(),&dr,header,&sr,cmd,95,1,&h),E_INVALIDARG);
    NO(xs_record_bvh_instances(ps.Get(),list.Get(),&dr,header,&sr,cmd,0,33,&h),E_NOTIMPL);
    NO(xs_record_bvh_offline(ps.Get(),list.Get(),&dr,header,&sr,cmd,48,3,&h),E_INVALIDARG);
    cmd[1].index=3; NO(record(),E_INVALIDARG); cmd[1].index=1;
    header[13]=UINT32_MAX; NO(record(),E_INVALIDARG); header[13]=128;
    cmd[1].va=source->GetGPUVirtualAddress(); NO(record(),E_INVALIDARG); cmd[1].va=sr.guest_base;
    --sr.bytes; NO(record(),E_INVALIDARG); ++sr.bytes;
    dr.bytes-=4; NO(record(),E_INVALIDARG); dr.bytes+=4;
    HR(xs_record_bvh_instances(ps.Get(),list.Get(),&dr,header,nullptr,nullptr,0,0,&empty)); CK(empty);
    HR(record()); CK(h);
    for (UINT i=0;i<4;i++) {
        UINT *w=header+32+32*i;
        memcpy(w,cmd[i].x,48); w[12]=cmd[i].va ? cmd[i].id : (cmd[i].id&0xffffff);
        w[13]=cmd[i].hit; w[14]=i==1?0x52345678:i==2?0x80000000:i==3?0x52341234:0;
        w[15]=(UINT)(cmd[i].va>>8);
        const float c=0x1.d906bcp-1f,s=0x1.87de2ap-2f;
        const float inverse[4][12]={{},{-14,1,-5,2,0,0,0,.5,0,0,0,1},
                {0,0,0,1,0,0,0,1,0,0,0,1},{0,0,0,1,0,0,0,c,s,0,-s,c}};
        memcpy(w+16,inverse[i],48); w[28]=i; w[29]=0; w[30]=i==1?0xc07f7e80:i==3?0x01000000:0x007f7f7f; w[31]=0;
    }
    memset(cmd,0xff,sizeof cmd);
    HR(source->Map(0,nullptr,(void **)&mapped)); memset(mapped,0xff,sizeof inputs); source->Unmap(0,nullptr);
    source.Reset(); ps.Reset();
    transition(D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);
    list->CopyResource(rb.Get(),dst.Get()); dst.Reset(); HR(list->Close());
    ID3D12CommandList *lists[]={list.Get()}; queue->ExecuteCommandLists(1,lists);
    ComPtr<ID3D12Fence> fence; HR(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));
    HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr); CK(event);
    HR(queue->Signal(fence.Get(),1)); HR(fence->SetEventOnCompletion(1,event)); CK(WaitForSingleObject(event,10000)==WAIT_OBJECT_0);
    CloseHandle(event); CK(fence->GetCompletedValue()==1); xs_drop_bvh_hold(h); xs_drop_bvh_hold(empty);
    HR(rb->Map(0,nullptr,(void **)&mapped));
    for (UINT i=0;i<192;i++) {
        UINT lane=(i-40)%32;
        if(i>=40 && i<168 && lane>=16 && lane<28 && !expected[i] && mapped[i]==0x80000000u) continue;
        if (expected[i]!=mapped[i]) { fprintf(stderr,"FAIL word %u got %08x expected %08x\n",i,mapped[i],expected[i]); return 1; }
    }
    rb->Unmap(0,nullptr);
    puts("PASS +1f0 WriteInstances: four full instance records, guest address packing, inverse/center/scale/rotation and guards, 192 fenced words");
}
