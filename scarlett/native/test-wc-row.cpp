#include <d3d12.h>
#include <wrl/client.h>
#include <cstdio>
#include "xmem/marked_section.h"
using Microsoft::WRL::ComPtr;
extern "C" HRESULT WINAPI D3D12XboxCreateDevice(void*,const void*,REFIID,void**);
extern "C" ID3D12Resource *test_row_backing(IUnknown*);
extern "C" HRESULT xs_write_view(IUnknown*,UINT,IUnknown*,const void*,SIZE_T);
extern "C" HRESULT xs_resolve_view(IUnknown*,SIZE_T,UINT,ID3D12DescriptorHeap**,D3D12_GPU_DESCRIPTOR_HANDLE*);
#define CK(x) do{if(!(x)){fprintf(stderr,"FAIL %d %s\n",__LINE__,#x);return 1;}}while(0)
#define HR(x) CK(SUCCEEDED(x))
template<bool tiny> static int check(){
    constexpr UINT pool=0x89700000,off=tiny?0xe5c6fc0:0xe650000,n=tiny?4:0x1c200,size=n*4,guard=65536;
    auto xm=LoadLibraryW(L"xmem.dll");CK(xm);
    auto alloc=(void*(WINAPI*)(void*,SIZE_T,DWORD,UINT64,DWORD))(void*)GetProcAddress(xm,"XMemVirtualAlloc");
    auto take=(HRESULT(WINAPI*)(ID3D12Device*,const void*,SIZE_T,ID3D12Heap**,ID3D12Resource**,UINT*))(void*)GetProcAddress(xm,"ScarlettAcquireNativePoolV1");
    CK(alloc && take);auto base=(BYTE*)alloc(nullptr,pool,0x20003000,0x283,0x2404);CK(base);
    DWORD params[20]={};params[0]=0x20004;params[2]=0x100000;params[10]=1;params[15]=0x20;
    IID boot={0x5405c344,0xd457,0x444e,{0xb4,0xdd,0x23,0x66,0xe4,0x5a,0xee,0x39}};
    ComPtr<IUnknown> dev;HR(D3D12XboxCreateDevice(nullptr,params,boot,(void**)dev.GetAddressOf()));auto vt=*(void***)dev.Get();
    ComPtr<ID3D12Device>d;HR(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&d)));
    ComPtr<ID3D12Resource> original;UINT offset=0;HRESULT hr=take(d.Get(),base+off,size,nullptr,&original,&offset);BYTE *alias=nullptr;
    if(hr==S_OK){CK(original && offset==off && original->GetDesc().Width==pool);}else{CK(hr==S_FALSE);scarlett_section_info pin={},map={};HR(scarlett_query_section(0x53430005,base+off,size,&pin));CK(pin.offset==off && pin.view_size==pool);HR(scarlett_query_section_handle(0x53430004,nullptr,pool,&map,pin.section));CloseHandle(pin.section);alias=(BYTE*)map.base;}
    memset(base+off-guard,0xa7,size+2*guard);DWORD w[]={1,0,0,0,size,0,1,0x10001,0,1,0,1,4};void *obj=nullptr;
    using Place=HRESULT(WINAPI*)(IUnknown*,UINT64,const void*,UINT,const void*,REFIID,void**);
    auto create=[&](UINT at,UINT state=UINT_MAX){return ((Place)vt[0x428/8])(dev.Get(),(UINT_PTR)base+at,w,state,nullptr,IID_ID3D12Resource,&obj);};
    for(UINT j=0;j<13;j++)if(j!=1){w[j]^=1;CK(FAILED(create(off))&&!obj);w[j]^=1;}
    CK(FAILED(create(off+guard))&&!obj && FAILED(create(off,3))&&!obj);
    HR(create(off));ComPtr<IUnknown> title;title.Attach((IUnknown*)obj);CK(create(off)==DXGI_ERROR_INVALID_CALL && !obj);
    ComPtr<ID3D12Resource> row;row.Attach(test_row_backing(title.Get()));CK(row && row->GetDesc().Width==size);
    using Make=HRESULT(WINAPI*)(IUnknown*,const void*,REFIID,void**);using Start=SIZE_T(WINAPI*)(IUnknown*);
    UINT hd[]={0,4,1,1};ComPtr<IUnknown> slots;HR(((Make)vt[0x70/8])(dev.Get(),hd,IID_ID3D12DescriptorHeap,(void**)slots.GetAddressOf()));SIZE_T slot=((Start)((*(void***)slots.Get())[0x48/8]))(slots.Get());
    DWORD views[][10]={{0,1,0x1688,0xd,0,0,n,4,0,0},{0,1,0,0,n,4,0,0,0,1}};
    if(tiny){views[0][0]=42;views[0][7]=0;CK(FAILED(xs_write_view(dev.Get(),0x98,title.Get(),views[1],slot)));views[1][0]=42;views[1][5]=0;}
    ID3D12DescriptorHeap *bank[2];D3D12_GPU_DESCRIPTOR_HANDLE h[2];for(UINT j=0;j<2;j++){UINT kind=j?0x98:0x90;SIZE_T dest=slot+32*j;auto write=[&](IUnknown *object){return xs_write_view(dev.Get(),kind,object,views[j],dest);};
        if(j){using Uav=void(WINAPI*)(IUnknown*,IUnknown*,IUnknown*,const void*,SIZE_T);((Uav)vt[0x98/8])(dev.Get(),title.Get(),nullptr,views[j],dest);}
        else HR(write(title.Get()));
        UINT64 tag[2];memcpy(tag,(void*)dest,16);
        CK(FAILED(write(dev.Get())));for(UINT k=0;k<10;k++){if(!j && k==3)continue;views[j][k]^=1;CK(FAILED(write(title.Get())));views[j][k]^=1;}CK(!memcmp(tag,(void*)dest,16));HR(xs_resolve_view(slots.Get(),dest,kind,&bank[j],&h[j]));}CK(bank[0]==bank[1]);
    DWORD other[]={tiny?0u:42u,1,0,0,tiny?0x1c200u:4u,tiny?4u:0u,0,0,0,1};
    CK(FAILED(xs_write_view(dev.Get(),0x98,title.Get(),other,slot+32)));
    ID3D12DescriptorHeap *wrong=nullptr;D3D12_GPU_DESCRIPTOR_HANDLE invalid={};CK(FAILED(xs_resolve_view(slots.Get(),slot+32,0x90,&wrong,&invalid)));
    title.Reset();CK(create(off)==DXGI_ERROR_INVALID_CALL && !obj);
    D3D12_HEAP_PROPERTIES hp={};hp.Type=D3D12_HEAP_TYPE_DEFAULT;hp.VisibleNodeMask=hp.CreationNodeMask=1;auto desc=row->GetDesc();ComPtr<ID3D12Resource> result,read;
    HR(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&result)));hp.Type=D3D12_HEAP_TYPE_READBACK;desc.Flags=D3D12_RESOURCE_FLAG_NONE;desc.Width=2*size+2*guard;
    HR(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&read)));
    ComPtr<ID3D12CommandQueue>q;ComPtr<ID3D12CommandAllocator>a;ComPtr<ID3D12GraphicsCommandList>l;D3D12_COMMAND_QUEUE_DESC qd={};HR(d->CreateCommandQueue(&qd,IID_PPV_ARGS(&q)));HR(d->CreateCommandAllocator(qd.Type,IID_PPV_ARGS(&a)));HR(d->CreateCommandList(0,qd.Type,a.Get(),nullptr,IID_PPV_ARGS(&l)));
    ComPtr<ID3D12RootSignature> rs[2];ComPtr<ID3D12PipelineState> ps[2];l->SetDescriptorHeaps(1,bank);
    auto transition=[&](ID3D12Resource *r,D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after){D3D12_RESOURCE_BARRIER b={};b.Transition={r,0,before,after};l->ResourceBarrier(1,&b);};
    for(int j=1;j>=0;j--){BYTE code[8192];FILE *f=fopen(tiny?(j?"typed-write.dxil":"typed.dxil"):j?"write.dxil":"read.dxil","rb");CK(f);auto len=fread(code,1,sizeof code,f);CK(len && feof(f));fclose(f);HR(d->CreateRootSignature(0,code,len,IID_PPV_ARGS(&rs[j])));D3D12_COMPUTE_PIPELINE_STATE_DESC pd={};pd.pRootSignature=rs[j].Get();pd.CS={code,len};HR(d->CreateComputePipelineState(&pd,IID_PPV_ARGS(&ps[j])));
        l->SetComputeRootSignature(rs[j].Get());l->SetPipelineState(ps[j].Get());l->SetComputeRootDescriptorTable(0,h[j]);if(!j)l->SetComputeRootUnorderedAccessView(1,result->GetGPUVirtualAddress());l->Dispatch(tiny?1:n/64,1,1);if(j)transition(row.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);}
    transition(result.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);l->CopyBufferRegion(read.Get(),0,result.Get(),0,size);
    if(original){D3D12_RESOURCE_BARRIER b={};b.Type=D3D12_RESOURCE_BARRIER_TYPE_ALIASING;b.Aliasing={row.Get(),original.Get()};l->ResourceBarrier(1,&b);l->CopyBufferRegion(read.Get(),size,original.Get(),off-guard,size+2*guard);}HR(l->Close());
    ComPtr<ID3D12Fence> gate,end;HR(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&gate)));HR(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&end)));HR(q->Wait(gate.Get(),1));ID3D12CommandList *list=l.Get();q->ExecuteCommandLists(1,&list);HR(q->Signal(end.Get(),1));
    for(UINT i=0;i<n;i++)((UINT*)(base+off))[i]=71*i+3;
    MemoryBarrier();CK(VirtualFree(base,0,MEM_RELEASE));CK(VirtualAlloc(base,pool,MEM_RESERVE,PAGE_NOACCESS)==base);BYTE *reuse=base+off-guard;CK(VirtualAlloc(reuse,size+2*guard,MEM_COMMIT,PAGE_READWRITE));memset(reuse,0xbd,size+2*guard);CK(FAILED(create(off))&&!obj);
    HANDLE ev=CreateEventW(nullptr,FALSE,FALSE,nullptr);CK(ev);HR(end->SetEventOnCompletion(1,ev));CK(WaitForSingleObject(ev,50)==WAIT_TIMEOUT);HR(gate->Signal(1));CK(WaitForSingleObject(ev,10000)==WAIT_OBJECT_0);CloseHandle(ev);HR(d->GetDeviceRemovedReason());BYTE *data;HR(read->Map(0,nullptr,(void**)&data));const BYTE *raw=original?data+size:alias+off-guard;
    for(UINT i=0;i<n;i++){UINT val=(71*i+3)^0xd83ac217;CK(((UINT*)data)[i]==val && ((const UINT*)(raw+guard))[i]==val);}for(UINT i=0;i<guard;i++)CK(raw[i]==0xa7 && raw[guard+size+i]==0xa7);for(UINT i=0;i<size+2*guard;i++)CK(reuse[i]==0xbd);
    read->Unmap(0,nullptr);if(alias)CK(VirtualFree(alias,0,MEM_RELEASE));CK(VirtualFree(base,0,MEM_RELEASE));if(tiny)puts("PASS row16: four typed UAV writes and SRV reads of original late-written WC bytes, guards, VA reuse, lease and malformed controls");else puts("PASS row: 115200 structured UAV writes/SRV reads, original bytes, guards, VA reuse, overlap and malformed views");return 0;
}

int main(){return check<false>() || check<true>();}
