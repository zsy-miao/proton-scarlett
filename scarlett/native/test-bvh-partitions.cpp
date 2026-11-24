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
    BYTE code[0x1580]; FILE *f=fopen("private-partitions.bin","rb"); CK(f);
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
    HR(SLOT(ps.Get(),0x30,Name)(ps.Get(),L"XDXR WritePartitions"));
    ComPtr<ID3D12Device> d; HR(xs_bvh_root_host(dev.Get(),root.Get())->GetDevice(IID_PPV_ARGS(&d)));
    UINT expected[192],inputs[192];
    for (UINT i=0;i<192;i++) { expected[i]=0x9da00000u+79*i; inputs[i]=0x12340000u+17*i; }
    UINT *header=expected+8; /* TLAS view begins +32, surrounding canaries included. */
    header[16]=3; header[17]=32; header[18]=3; header[19]=0xa001;
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
    xs_bvh_range dr={dst.Get(),0x551100000000ull,32,588},sr={source.Get(),0x662200000000ull,12,144};
    struct Cmd { UINT id,xyz[3],n,pad; UINT64 va; } cmd[]={
        {1,{4,5,6},32,9,sr.guest_base+4},{0,{8,9,10},0,0,~0ull},{0xfffffffe,{1,2,3},3,0,sr.guest_base+132},
        {2,{},2,0,sr.guest_base} /* ignored: outside b0.x */
    };
    xs_bvh_hold *h=nullptr,*empty=nullptr;
    auto record=[&](){ return xs_record_bvh_partitions(ps.Get(),list.Get(),&dr,header,&sr,cmd,96,3,&h); };
#define NO(expr,err) do { h=(xs_bvh_hold*)1; CK((expr)==(err) && !h); } while(0)
    NO(xs_record_bvh_partitions(ps.Get(),list.Get(),&dr,header,&sr,(void*)1,96,3,&h),E_POINTER);
    NO(xs_record_bvh_partitions(ps.Get(),list.Get(),&dr,header,&sr,cmd,95,3,&h),E_INVALIDARG);
    NO(xs_record_bvh_partitions(ps.Get(),list.Get(),&dr,header,&sr,cmd,1056,33,&h),E_NOTIMPL);
    NO(xs_record_bvh_offline(ps.Get(),list.Get(),&dr,header,&sr,cmd,48,3,&h),E_INVALIDARG);
    cmd[0].n=33; NO(record(),E_NOTIMPL); cmd[0].n=32;
    cmd[1].id=1; NO(record(),E_INVALIDARG); cmd[1].id=0;
    header[17]=UINT32_MAX; NO(record(),E_INVALIDARG); header[17]=32;
    cmd[2].n=4; NO(record(),E_INVALIDARG); cmd[2].n=3;
    cmd[2].va=source->GetGPUVirtualAddress()+144; NO(record(),E_INVALIDARG); cmd[2].va=sr.guest_base+132;
    --sr.bytes; NO(record(),E_INVALIDARG); ++sr.bytes;
    dr.bytes-=4; NO(record(),E_INVALIDARG); dr.bytes+=4;
    HR(xs_record_bvh_partitions(ps.Get(),list.Get(),&dr,header,nullptr,nullptr,0,0,&empty)); CK(empty);
    HR(record()); CK(h);
    // Expected DWORD offsets from the TLAS, fixed for N=3, M=32. Global writes no mask bit.
    header[19]=0xa002; header[20]=0; header[27]=32; header[41]=3;
    memcpy(header+80,inputs+4,128); memcpy(header+144,inputs+36,12);
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
    HR(rb->Map(0,nullptr,(void **)&mapped)); CK(!memcmp(mapped,expected,sizeof expected)); rb->Unmap(0,nullptr);
    puts("PASS synthetic +1f0 WritePartitions: ordinary/set/clear, global, snapshots, rejection, all 192 fenced DWORDs");
}
