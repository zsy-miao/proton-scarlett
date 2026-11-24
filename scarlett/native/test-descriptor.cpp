#include <windows.h>
#include <d3d12.h>
#include <cstdio>
extern "C" HRESULT WINAPI D3D12XboxCreateDevice(void *,const void *,REFIID,void **);
extern "C" HRESULT xs_descriptor_slot(IUnknown *,SIZE_T,D3D12_CPU_DESCRIPTOR_HANDLE *,D3D12_GPU_DESCRIPTOR_HANDLE *);
#define CK(x) do { if (!(x)) { std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); ExitProcess(1); } } while(0)
#define HR(x) do { HRESULT h_=(x); if(FAILED(h_)){std::fprintf(stderr,"FAIL %d: %s hr=%#lx\n",__LINE__,#x,(unsigned long)h_);ExitProcess(1);} } while(0)
#define SLOT(o,n,T) ((T)((*reinterpret_cast<void***>(o))[n/8]))
using Make = HRESULT (WINAPI *)(IUnknown *,const void *,REFIID,void **);
using Stride = UINT (WINAPI *)(IUnknown *,UINT);
using Start = SIZE_T (WINAPI *)(IUnknown *);
static BYTE *arena;
static SIZE_T extent;
static bool alloc_hook,free_hook;
static unsigned taken,returned;
static void *WINAPI take(SIZE_T n,UINT64 attr) {
    CK(alloc_hook && n==extent && attr==0x800004);
    ++taken;
    return arena;
}
static void WINAPI give(void *p,UINT64 attr) {
    CK(free_hook && p==arena && attr==0x800004);
    ++returned;
}
extern "C" { __declspec(dllexport) struct {decltype(&take) a; decltype(&give) f;} __XMemAllocationHooks__={take,give}; }

// Real GPU use of a translated nonzero slot; check the original title bytes survive.
static void check_rtv(ID3D12Device *d,D3D12_CPU_DESCRIPTOR_HANDLE rtv)
{
    D3D12_RESOURCE_DESC tex = {};
    tex.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    tex.Width=tex.Height=tex.DepthOrArraySize=tex.MipLevels=tex.SampleDesc.Count=1;
    tex.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    tex.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    D3D12_HEAP_PROPERTIES hp={}; hp.Type=D3D12_HEAP_TYPE_DEFAULT; hp.CreationNodeMask=hp.VisibleNodeMask=1;
    ID3D12Resource *target,*readback;
    HR(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&tex,D3D12_RESOURCE_STATE_RENDER_TARGET,nullptr,IID_PPV_ARGS(&target)));
    d->CreateRenderTargetView(target,nullptr,rtv);
    hp.Type=D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC buf={}; buf.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER; buf.Width=512;
    buf.Height=buf.DepthOrArraySize=buf.MipLevels=buf.SampleDesc.Count=1; buf.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    HR(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&buf,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback)));
    ID3D12CommandQueue *queue; ID3D12CommandAllocator *alloc; ID3D12GraphicsCommandList *list; ID3D12Fence *fence;
    D3D12_COMMAND_QUEUE_DESC q={};
    HR(d->CreateCommandQueue(&q,IID_PPV_ARGS(&queue)));
    HR(d->CreateCommandAllocator(q.Type,IID_PPV_ARGS(&alloc)));
    HR(d->CreateCommandList(0,q.Type,alloc,nullptr,IID_PPV_ARGS(&list)));
    FLOAT red[]={1,0,0,1};
    list->ClearRenderTargetView(rtv,red,0,nullptr);
    D3D12_RESOURCE_BARRIER b={}; b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource=target; b.Transition.StateBefore=D3D12_RESOURCE_STATE_RENDER_TARGET; b.Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_SOURCE;
    list->ResourceBarrier(1,&b);
    D3D12_TEXTURE_COPY_LOCATION src={},dst={}; src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; src.pResource=target;
    dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; dst.pResource=readback;
    dst.PlacedFootprint.Footprint={tex.Format,1,1,1,256};
    list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
    HR(list->Close()); ID3D12CommandList *batch[]={list}; queue->ExecuteCommandLists(1,batch);
    HR(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence))); HR(queue->Signal(fence,1));
    HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr); CK(event);
    HR(fence->SetEventOnCompletion(1,event)); CK(WaitForSingleObject(event,5000)==WAIT_OBJECT_0);
    DWORD *word; HR(readback->Map(0,nullptr,(void **)&word)); CK(*word==0xff0000ff); readback->Unmap(0,nullptr);
    CloseHandle(event); fence->Release(); list->Release(); alloc->Release(); queue->Release(); readback->Release(); target->Release();
}
int main()
{
    CK(LoadLibraryW(L"xmem.dll"));
    DWORD p[20]={}; p[0]=0x20004;p[2]=0x100000;p[10]=1;p[15]=0x20;
    const IID id={0x5405c344,0xd457,0x444e,{0xb4,0xdd,0x23,0x66,0xe4,0x5a,0xee,0x39}};
    IUnknown *d; ID3D12Device *host;
    HR(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&host)));
    HR(D3D12XboxCreateDevice(nullptr,p,id,(void **)&d));
    CK(((BYTE*)d)[0x18]==0);
    auto create=SLOT(d,0x70,Make); auto increment=SLOT(d,0x78,Stride);
    UINT desc[][4]={{0,4096,1,1},{1,128,1,1},{1,128,0,1},{2,4096,0,1},{3,2048,0,1}};
    extent=desc[0][1]*increment(d,0); CK(extent>=65536);
    arena=(BYTE*)VirtualAlloc(nullptr,extent,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE); CK(arena);
    for (auto &v : desc) {
        IUnknown *h,*qi;
        alloc_hook=v[0]==0;
        HR(create(d,v,IID_ID3D12DescriptorHeap,(void **)&h)); alloc_hook=false;
        HR(h->QueryInterface(IID_IUnknown,(void **)&qi)); CK(qi==h); qi->Release();
        auto cpu=SLOT(h,0x48,Start)(h), gpu=SLOT(h,0x50,Start)(h);
        UINT stride=increment(d,v[0]); CK(cpu && stride>=16 && (v[2]?gpu==cpu:!gpu));
        if (!v[0]) CK(cpu==(SIZE_T)arena && taken==1);
        SIZE_T last=cpu+(v[1]-1)*stride;
        *(UINT64*)last=0x10000000000000ull; *((UINT64*)last+1)=0x2403c00000000ull;
        D3D12_CPU_DESCRIPTOR_HANDLE c0,cl; D3D12_GPU_DESCRIPTOR_HANDLE g0,gl;
        HR(xs_descriptor_slot(h,cpu,&c0,v[2]?&g0:nullptr));
        HR(xs_descriptor_slot(h,last,&cl,v[2]?&gl:nullptr));
        SIZE_T delta=(v[1]-1)*host->GetDescriptorHandleIncrementSize((D3D12_DESCRIPTOR_HEAP_TYPE)v[0]);
        CK(cl.ptr-c0.ptr==delta && (!v[2] || gl.ptr-g0.ptr==delta) && cl.ptr!=last);
        if (v[0]==2) check_rtv(host,cl);
        CK(*(UINT64*)last==0x10000000000000ull && *((UINT64*)last+1)==0x2403c00000000ull);
        CK(FAILED(xs_descriptor_slot(h,last+1,&cl,nullptr)) && !cl.ptr);
        CK(FAILED(xs_descriptor_slot(h,last+stride,&cl,nullptr)) && !cl.ptr);
        free_hook=!v[0]; CK(!h->Release()); free_hook=false;
    }
    UINT bad[]={4,128,0,1}; void *out=d;
    CK(increment(d,4)==0 && create(d,bad,IID_ID3D12DescriptorHeap,&out)==E_NOTIMPL && !out);
    CK(returned==1 && *(UINT64*)(arena+extent-increment(d,0))==0x10000000000000ull);
    CK(VirtualFree(arena,0,MEM_RELEASE)); d->Release(); host->Release();
    puts("PASS descriptor heaps: XMem placed base/free, five heap shapes, bounds, title writes isolated, final-slot RTV GPU clear/readback");
}
