#include <d3d12.h>
#include <cstdio>

extern "C" {
HRESULT WINAPI D3D12XboxCreateDevice(void *,const void *,REFIID,void **);
HRESULT xs_placed_copy_readback(IUnknown *,SIZE_T,UINT64,UINT64,ID3D12GraphicsCommandList *,ID3D12Resource *,UINT64,IUnknown **);
HRESULT xs_resolve_view(IUnknown *,SIZE_T,UINT,ID3D12DescriptorHeap **,D3D12_GPU_DESCRIPTOR_HANDLE *);
HRESULT xs_write_view(IUnknown *,UINT,IUnknown *,const void *,SIZE_T);
__declspec(dllimport) BOOL WINAPI XMemAllocatePhysicalPages(ULONGLONG,PULONG_PTR,PULONG_PTR);
__declspec(dllimport) BOOL WINAPI XMemFreePhysicalPages(ULONG_PTR,PULONG_PTR);
__declspec(dllimport) void *WINAPI XMemMapPhysicalPages(void *,ULONG_PTR,PULONG_PTR);
__declspec(dllimport) void *WINAPI XMemVirtualAlloc(void *,SIZE_T,DWORD,ULONGLONG,DWORD);
}
#define CK(x) do { if (!(x)) { std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); return 1; } } while(0)
#define HR(x) CK(SUCCEEDED(x))
#define SLOT(o,n,T) ((T)((*reinterpret_cast<void***>(o))[n/8]))
using Make=HRESULT(WINAPI *)(IUnknown *,UINT64,const void *,UINT,const void *,REFIID,void **);
using Heap=HRESULT(WINAPI *)(IUnknown *,const void *,REFIID,void **);
using Start=SIZE_T(WINAPI *)(IUnknown *);
using Srv=void(WINAPI *)(IUnknown *,IUnknown *,const void *,SIZE_T);
using Uav=void(WINAPI *)(IUnknown *,IUnknown *,IUnknown *,const void *,SIZE_T);
int main()
{
    constexpr UINT64 pg=65536,mib=1<<20,width=174*mib;
    const SIZE_T count[]={160,16,432,752,1600}, id[]={2352,2512,2528,0,752};
    const DWORD policy[]={0x1004,0x1404,0x8404,0x2404,0x2004};
    BYTE *at[5],*va=(BYTE *)XMemVirtualAlloc(nullptr,2960*pg,0x42000,7,PAGE_NOACCESS);
    CK(va);
    for (unsigned i=0;i<5;i++) {
        at[i]=va;
        if (i<4) CK(VirtualFree(va,count[i]*pg,MEM_RELEASE|MEM_PRESERVE_PLACEHOLDER));
        va+=count[i]*pg;
    }
    for (unsigned i=0;i<5;i++) CK(XMemVirtualAlloc(at[i],count[i]*pg,0x20406000,0x27,policy[i])==at[i]);
    ULONG_PTR ids[2960],pages=2960; CK(XMemAllocatePhysicalPages(0x280,&pages,ids));
    DWORD params[20]={}; params[0]=0x20004;params[2]=0x100000;params[10]=1;params[15]=0x20;
    const IID boot={0x5405c344,0xd457,0x444e,{0xb4,0xdd,0x23,0x66,0xe4,0x5a,0xee,0x39}};
    IUnknown *dev; HR(D3D12XboxCreateDevice(nullptr,params,boot,(void **)&dev));
    Make create=SLOT(dev,0x428,Make);
    D3D12_RESOURCE_DESC desc={}; desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER; desc.Width=width;
    desc.Height=desc.DepthOrArraySize=desc.MipLevels=desc.SampleDesc.Count=1;
    desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR; desc.Flags=(D3D12_RESOURCE_FLAGS)0x20004;
    auto rejected=[&](UINT64 base,const void *wire,UINT state,const void *clear,REFIID iid) {
        void *out=dev; return FAILED(create(dev,base,wire,state,clear,iid,&out)) && !out;
    };
    UINT64 base=(UINT64)at[2];
    CK(rejected(base,&desc,~0u,nullptr,IID_ID3D12Resource)); // unmapped
    for (unsigned i=2;i<5;i++) CK(XMemMapPhysicalPages(at[i],count[i],ids+id[i])==at[i]);
    CK(rejected(base,&desc,~0u,nullptr,IID_IUnknown) && rejected(base,at[1],~0u,nullptr,IID_ID3D12Resource));
    CK(rejected(base,&desc,0,nullptr,IID_ID3D12Resource) && rejected(base,&desc,~0u,&desc,IID_ID3D12Resource));
    CK(rejected(base+pg,&desc,~0u,nullptr,IID_ID3D12Resource));
    desc.Width--; CK(rejected(base,&desc,~0u,nullptr,IID_ID3D12Resource)); desc.Width++;
    desc.Alignment=pg; CK(rejected(base,&desc,~0u,nullptr,IID_ID3D12Resource)); desc.Alignment=0;
#ifdef TEST_DEFAULT_PLACED
    CK(rejected(base,&desc,~0u,nullptr,IID_ID3D12Resource));
    for (unsigned i=2;i<5;i++) CK(XMemMapPhysicalPages(at[i],count[i],nullptr)==at[i]);
    CK(XMemFreePhysicalPages(2960,ids));
    for (auto a:at) CK(VirtualFree(a,0,MEM_RELEASE));
    CK(dev->Release()==0);
    puts("PASS default +428: qualified same-run placement still rejected with null output");
#else
    IUnknown *obj,*qi;
    HR(create(dev,base,&desc,~0u,nullptr,IID_ID3D12Resource,(void **)&obj));
    HR(obj->QueryInterface(IID_IUnknown,(void **)&qi)); CK(qi==obj && qi->Release()==1);
    HR(obj->QueryInterface(IID_ID3D12Resource,(void **)&qi)); CK(qi==obj && qi->Release()==1);
    CK(obj->QueryInterface(IID_ID3D12Resource1,(void **)&qi)==E_NOINTERFACE && !qi);
    D3D12_DESCRIPTOR_HEAP_DESC hd={D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,2,D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,1};
    IUnknown *heap; HR(SLOT(dev,0x70,Heap)(dev,&hd,IID_ID3D12DescriptorHeap,(void **)&heap));
    SIZE_T srv=SLOT(heap,0x48,Start)(heap),uav=srv+32;
    DWORD s[10]={39,1,0x1688,0,0,0,width/4,0,1},u[10]={39,1,0,0,width/4,0,0,0,1};
    SLOT(dev,0x90,Srv)(dev,obj,s,srv);
    SLOT(dev,0x98,Uav)(dev,obj,nullptr,u,uav);
    s[6]--; CK(FAILED(xs_write_view(dev,0x90,obj,s,srv))); s[6]++;
    u[6]=4; CK(FAILED(xs_write_view(dev,0x98,obj,u,uav)));
    CK(obj->Release()==2); // two slots own the identity
    ID3D12DescriptorHeap *desktop; D3D12_GPU_DESCRIPTOR_HANDLE handle;
    CK(xs_resolve_view(heap,srv,0x90,&desktop,&handle)==E_NOTIMPL && !desktop && !handle.ptr);
    CK(xs_resolve_view(heap,uav,0x98,&desktop,&handle)==E_NOTIMPL && !desktop && !handle.ptr);
    const UINT64 pos[]={74*mib-4,width-4};
    for (unsigned i=0;i<2;i++) *(volatile UINT *)(base+pos[i])=0xa4280000+i;
    MemoryBarrier();
    for (unsigned i=2;i<5;i++) CK(XMemMapPhysicalPages(at[i],count[i],nullptr)==at[i]);
    CK(XMemFreePhysicalPages(2960,ids));
    for (auto a:at) CK(VirtualFree(a,0,MEM_RELEASE));
    CK(VirtualAlloc(at[2],width,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE)==at[2]);
    ID3D12Device *d; HR(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&d)));
    D3D12_COMMAND_QUEUE_DESC qd={}; qd.Type=D3D12_COMMAND_LIST_TYPE_COPY;
    ID3D12CommandQueue *q; ID3D12CommandAllocator *a; ID3D12GraphicsCommandList *l; ID3D12Resource *rb;
    HR(d->CreateCommandQueue(&qd,IID_PPV_ARGS(&q))); HR(d->CreateCommandAllocator(qd.Type,IID_PPV_ARGS(&a)));
    HR(d->CreateCommandList(0,qd.Type,a,nullptr,IID_PPV_ARGS(&l)));
    D3D12_HEAP_PROPERTIES hp={}; hp.Type=D3D12_HEAP_TYPE_READBACK; hp.CreationNodeMask=hp.VisibleNodeMask=1;
    desc.Width=8; desc.Flags=D3D12_RESOURCE_FLAG_NONE;
    HR(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&rb)));
    auto bad=[&](SIZE_T slot,UINT64 at,UINT64 off) {
        IUnknown *hold=dev; return FAILED(xs_placed_copy_readback(heap,slot,at,4,l,rb,off,&hold)) && !hold;
    };
    CK(bad(srv,0,0) && bad(srv,pos[0]+1,0) && bad(srv,pos[1]+1,0) && bad(uav,pos[0],0));
    CK(bad(srv,pos[0],6) && bad(srv,pos[0],~0ull));
    auto *tag=(UINT64 *)srv; tag[0]^=1; CK(bad(srv,pos[0],0)); tag[0]^=1;
    IUnknown *holds[2];
    for (unsigned i=0;i<2;i++) { HR(xs_placed_copy_readback(heap,srv,pos[i],4,l,rb,4*i,&holds[i])); CK(holds[i]==obj); }
    DWORD nulls[10]={42,1,0x1688};
    SLOT(dev,0x90,Srv)(dev,nullptr,nulls,srv); CK(bad(srv,pos[0],0));
    CK(heap->Release()==0 && dev->Release()==1);
    HR(l->Close()); ID3D12CommandList *lists[]={l}; q->ExecuteCommandLists(1,lists);
    ID3D12Fence *f; HR(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&f))); HR(q->Signal(f,1));
    HANDLE e=CreateEventW(nullptr,FALSE,FALSE,nullptr); CK(e); HR(f->SetEventOnCompletion(1,e));
    CK(WaitForSingleObject(e,10000)==WAIT_OBJECT_0);
    UINT *words; HR(rb->Map(0,nullptr,(void **)&words)); CK(words[0]==0xa4280000 && words[1]==0xa4280001);
    rb->Unmap(0,nullptr); CK(holds[0]->Release()==1 && holds[1]->Release()==0);
    rb->Release(); f->Release(); l->Release(); a->Release(); q->Release(); d->Release();
    CloseHandle(e); CK(VirtualFree(at[2],0,MEM_RELEASE));
    puts("PASS staged +428: actual factory/writers, logical RAW metadata, shader binding denied; retired WC/WB copies survived VA reuse and release of title, heap and device");
#endif
}
