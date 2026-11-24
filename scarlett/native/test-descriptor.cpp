#include <windows.h>
#include <d3d12.h>
#include <cstdio>
#include <initializer_list>
extern "C" HRESULT WINAPI D3D12XboxCreateDevice(void *,const void *,REFIID,void **);
extern "C" HRESULT xs_descriptor_slot(IUnknown *,SIZE_T,D3D12_CPU_DESCRIPTOR_HANDLE *,D3D12_GPU_DESCRIPTOR_HANDLE *);
extern "C" HRESULT xs_descriptor_range(IUnknown *,SIZE_T,UINT,ID3D12DescriptorHeap **,D3D12_CPU_DESCRIPTOR_HANDLE *,D3D12_GPU_DESCRIPTOR_HANDLE *);
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
static void *(WINAPI *default_alloc)(SIZE_T,UINT64);
static void (WINAPI *default_free)(void *,UINT64);
static BYTE *cpu_backing;
static void *WINAPI take(SIZE_T n,UINT64 attr) {
    if (n==0xc38*32) {
        CK(!alloc_hook && !cpu_backing && attr==0x800004);
        return cpu_backing=(BYTE *)default_alloc(n,attr);
    }
    CK(alloc_hook && n==extent && attr==0x800004);
    ++taken;
    return arena;
}
static void WINAPI give(void *p,UINT64 attr) {
    if (cpu_backing && p==cpu_backing) {
        CK(!free_hook && attr==0x800004);
        default_free(p,attr); cpu_backing=nullptr; return;
    }
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
#include "test-view.hpp"
static void test_dma_slot(IUnknown *d,ID3D12Device *host)
{
    HMODULE xm=GetModuleHandleW(L"xmem.dll");
    auto reserve=(void *(WINAPI *)(void *,SIZE_T,DWORD,UINT64,DWORD))(void *)GetProcAddress(xm,"XMemVirtualAlloc");
    auto alloc=(BOOL(WINAPI *)(UINT64,ULONG_PTR *,ULONG_PTR *))(void *)GetProcAddress(xm,"XMemAllocatePhysicalPages");
    auto free_run=(BOOL(WINAPI *)(ULONG_PTR,ULONG_PTR *))(void *)GetProcAddress(xm,"XMemFreePhysicalPages");
    auto map=(void *(WINAPI *)(void *,ULONG_PTR,ULONG_PTR *))(void *)GetProcAddress(xm,"XMemMapPhysicalPages");
    CK(reserve && alloc && free_run && map);
    const SIZE_T pg=65536, counts[]={160,16,432,752,1600}, ids0[]={2352,2512,2528};
    const DWORD prot[]={0x1004,0x1404,0x8404,0x2404,0x2004};
    BYTE *at[5]; arena=(BYTE *)reserve(nullptr,2960*pg,0x42000,7,PAGE_NOACCESS); CK(arena);
    BYTE *p=arena;
    for (unsigned i=0;i<5;i++) {
        at[i]=p;
        if (i<4) CK(VirtualFree(p,counts[i]*pg,MEM_RELEASE|MEM_PRESERVE_PLACEHOLDER));
        p+=counts[i]*pg;
    }
    for (unsigned i=0;i<5;i++) CK(reserve(at[i],counts[i]*pg,0x20406000,0x27,prot[i])==at[i]);
    ULONG_PTR n=2960,ids[2960]; CK(alloc(0x280,&n,ids));
    for (unsigned i=0;i<3;i++) CK(map(at[i],counts[i],ids+ids0[i])==at[i]);
    UINT desc[]={0,0x130000,1,1},words[10]={42,2,0x1688},copy[10];
    SIZE_T got=0; CK(ReadProcessMemory(GetCurrentProcess(),words,copy,sizeof copy,&got) && got==sizeof copy && !memcmp(words,copy,sizeof copy));
    MemoryBarrier();
    SIZE_T wrote=0;
    BOOL ok=WriteProcessMemory(GetCurrentProcess(),arena,words,16,&wrote);
    std::printf("DMA placement probe: stack RPM %zu bytes; WB destination WPM ok=%d bytes=%zu error=%lu\n",got,ok,wrote,GetLastError());
    extent=38u<<20;
    IUnknown *heap; alloc_hook=true; HR(SLOT(d,0x70,Make)(d,desc,IID_ID3D12DescriptorHeap,(void **)&heap)); alloc_hook=false;
    SIZE_T slot=SLOT(heap,0x48,Start)(heap); CK(slot==(SIZE_T)arena);
    CK(xs_write_view(d,0x90,nullptr,(void *)1,slot)==E_POINTER);
    CK(xs_write_view(d,0x90,nullptr,words,slot+1)==E_INVALIDARG);
    *(UINT64 *)(slot+16)=0x0190face;
    using Srv=void(WINAPI *)(IUnknown *,IUnknown *,const void *,SIZE_T);
    SLOT(d,0x90,Srv)(d,nullptr,words,slot);
    CK(*(UINT64 *)(slot+8)==0x5853444553430090ull && *(UINT64 *)(slot+16)==0x0190face);
    ID3D12DescriptorHeap *bank; D3D12_GPU_DESCRIPTOR_HANDLE gpu;
    HR(xs_resolve_view(heap,slot,0x90,&bank,&gpu)); CK(bank && gpu.ptr);
    test_writers(d,host,heap,slot,32); // dispatches a typed null Texture1D load on real DMA slot storage
    SLOT(d,0x90,Srv)(d,nullptr,words,(SIZE_T)at[1]); // WC descriptor extent, same wire bytes
    HR(xs_resolve_view(heap,(SIZE_T)at[1],0x90,&bank,&gpu));
    CK(map(at[0],counts[0],nullptr)==at[0]); // exact slot now inaccessible
    CK(xs_write_view(d,0x90,nullptr,words,slot)==E_POINTER);
    CK(FAILED(xs_resolve_view(heap,slot,0x90,&bank,&gpu)) && !bank && !gpu.ptr);
    free_hook=true; CK(!heap->Release()); free_hook=false;
    for (unsigned i=1;i<3;i++) CK(map(at[i],counts[i],nullptr)==at[i]);
    CK(free_run(n,ids)); for (auto a:at) CK(VirtualFree(a,0,MEM_RELEASE));
    puts("PASS public +90 on WB/WC DMA title slots; Texture1D null load returned zero; revoked slot rejected");
}

int main()
{
    auto xm=LoadLibraryW(L"xmem.dll"); CK(xm);
    default_alloc=(decltype(default_alloc))(void *)GetProcAddress(xm,"XMemAllocDefault");
    default_free=(decltype(default_free))(void *)GetProcAddress(xm,"XMemFreeDefault");
    CK(default_alloc && default_free);
    DWORD p[20]={}; p[0]=0x20004;p[2]=0x100000;p[10]=1;p[15]=0x20;
    const IID id={0x5405c344,0xd457,0x444e,{0xb4,0xdd,0x23,0x66,0xe4,0x5a,0xee,0x39}};
    IUnknown *d; ID3D12Device *host;
    HR(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&host)));
    HR(D3D12XboxCreateDevice(nullptr,p,id,(void **)&d));
    CK(((BYTE*)d)[0x18]==0);
    auto create=SLOT(d,0x70,Make); auto increment=SLOT(d,0x78,Stride);
    UINT desc[][4]={{0,0x130000,1,1},{1,128,1,1},{1,128,0,1},{2,4096,0,1},{3,2048,0,1}};
    extent=desc[0][1]*increment(d,0); CK(extent>=65536);
    arena=(BYTE*)VirtualAlloc(nullptr,extent,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE); CK(arena);
    for (auto &v : desc) {
        IUnknown *h,*qi;
        alloc_hook=v[0]==0;
        HR(create(d,v,IID_ID3D12DescriptorHeap,(void **)&h)); alloc_hook=false;
        HR(h->QueryInterface(IID_IUnknown,(void **)&qi)); CK(qi==h); qi->Release();
        auto cpu=SLOT(h,0x48,Start)(h), gpu=SLOT(h,0x50,Start)(h);
        UINT stride=increment(d,v[0]); CK(cpu && stride>=16 && (v[2]?gpu==cpu:!gpu));
        if (!v[0]) {
            CK(cpu==(SIZE_T)arena && gpu==cpu && taken==1 && extent==(38u<<20));
            const SIZE_T boundary=cpu+1000000ull*stride;
            ID3D12DescriptorHeap *first,*tail,*bad; D3D12_GPU_DESCRIPTOR_HANDLE handle;
            HR(xs_descriptor_range(h,cpu,1000000,&first,nullptr,&handle));
            HR(xs_descriptor_range(h,boundary,v[1]-1000000,&tail,nullptr,&handle));
            CK(first!=tail && first->GetDesc().NumDescriptors==1000000 &&
                tail->GetDesc().NumDescriptors==v[1]-1000000);
            CK(xs_descriptor_range(h,boundary-stride,2,&bad,nullptr,&handle)==E_NOTIMPL && !bad && !handle.ptr);
            // Same shader/recorder uses first bank's final four descriptors,
            // then tail bank's final four descriptors.
            test_writers(d,host,h,boundary-5*stride,stride);
            test_writers(d,host,h,cpu+extent-5*stride,stride);
        }
        SIZE_T last=cpu+(v[1]-1)*stride;
        *(UINT64*)last=0x10000000000000ull; *((UINT64*)last+1)=0x2403c00000000ull;
        D3D12_CPU_DESCRIPTOR_HANDLE c0,cl; D3D12_GPU_DESCRIPTOR_HANDLE g0,gl;
        HR(xs_descriptor_slot(h,cpu,&c0,v[2]?&g0:nullptr));
        HR(xs_descriptor_slot(h,last,&cl,v[2]?&gl:nullptr));
        if (!v[0]) HR(xs_descriptor_slot(h,cpu+1000000ull*stride,&c0,&g0)); // tail-relative
        SIZE_T delta=(v[1]-1-(v[0]?0:1000000))*host->GetDescriptorHandleIncrementSize((D3D12_DESCRIPTOR_HEAP_TYPE)v[0]);
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
    const SIZE_T stale=(SIZE_T)arena+2*increment(d,0);
    UINT nulls[10]={42,1,0x1688};
    CK(xs_write_view(d,0x90,nullptr,nulls,stale)==E_INVALIDARG);
    IUnknown *fresh;
    alloc_hook=true; HR(create(d,desc[0],IID_ID3D12DescriptorHeap,(void **)&fresh)); alloc_hook=false;
    ID3D12DescriptorHeap *desktop=nullptr; D3D12_GPU_DESCRIPTOR_HANDLE g={};
    CK(FAILED(xs_resolve_view(fresh,stale,0x90,&desktop,&g)) && !desktop && !g.ptr);
    HR(xs_write_view(d,0x90,nullptr,nulls,stale)); HR(xs_resolve_view(fresh,stale,0x90,&desktop,&g));
    free_hook=true; CK(!fresh->Release()); free_hook=false; CK(returned==2);
    CK(VirtualFree(arena,0,MEM_RELEASE)); test_dma_slot(d,host);
    UINT cpu_desc[]={0,0xc37,0,1};
    CK(create(d,cpu_desc,IID_ID3D12DescriptorHeap,&out)==E_NOTIMPL && !out);
    cpu_desc[1]=0xc38;
    HR(create(d,cpu_desc,IID_ID3D12DescriptorHeap,(void **)&fresh));
    SIZE_T cpu=SLOT(fresh,0x48,Start)(fresh);
    UINT stride=increment(d,0);
    CK(cpu && cpu==(SIZE_T)cpu_backing && stride==32 && !SLOT(fresh,0x50,Start)(fresh));
    SIZE_T end=cpu+cpu_desc[1]*stride;
    D3D12_CPU_DESCRIPTOR_HANDLE c;
    CK(xs_descriptor_range(fresh,end-stride,2,&desktop,&c,nullptr)==E_INVALIDARG && !desktop && !c.ptr);
    CK(xs_descriptor_range(fresh,cpu,1,&desktop,&c,&g)==E_INVALIDARG && !desktop && !c.ptr && !g.ptr);
    test_writers(d,host,fresh,end-5*stride,stride,true);
    CK(d->Release()==1 && cpu_backing); // heap still owns device
    CK(!fresh->Release() && !cpu_backing);
    host->Release();
    puts("PASS CPU-only 0xc38: default-backed XMem, no GPU handle, copied final four descriptors and fenced RAW/null readback");
    puts("PASS descriptor heaps: actual 0x130000 placed slots, two fenced host-bank dispatches, crossing rejected, exact XMem base/free and final-slot RTV readback");
}
