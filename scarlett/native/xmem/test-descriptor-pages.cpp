#include "gpu_buffer.hpp"
#include <cstdio>

extern "C" {
__declspec(dllimport) BOOL WINAPI XMemAllocatePhysicalPages(ULONGLONG,PULONG_PTR,PULONG_PTR);
__declspec(dllimport) BOOL WINAPI XMemFreePhysicalPages(ULONG_PTR,PULONG_PTR);
__declspec(dllimport) void *WINAPI XMemMapPhysicalPages(void *,ULONG_PTR,PULONG_PTR);
__declspec(dllimport) void *WINAPI XMemVirtualAlloc(void *,SIZE_T,DWORD,ULONGLONG,DWORD);
}
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"%d: %s (error %lu)\n",__LINE__,#x,GetLastError()); return 1; } } while (0)
#define HR(x) CHECK(SUCCEEDED(x))

int main()
{
    constexpr SIZE_T pg=65536, n=2960, fourth=752;
    const SIZE_T counts[]={160,16,432,fourth,1600};
    const DWORD prot[]={0x1004,0x1404,0x8404,0x2404,0x2004};
    auto p=(BYTE *)XMemVirtualAlloc(nullptr,n*pg,0x42000,7,1);
    BYTE *at[5];
    CHECK(p);
    for (SIZE_T i=0,off=0;i<5;++i) {
        at[i]=p+off;
        off+=pg*counts[i];
        if (i<4) CHECK(VirtualFree(at[i],counts[i]*pg,MEM_RELEASE|MEM_PRESERVE_PLACEHOLDER));
    }
    for (SIZE_T i=0;i<5;++i)
        CHECK(XMemVirtualAlloc(at[i],counts[i]*pg,0x20406000,0x27,prot[i])==at[i]);
    ULONG_PTR ids[n], count=n;
    CHECK(XMemAllocatePhysicalPages(0x280,&count,ids) && count==n);
    CHECK(!XMemMapPhysicalPages(at[3],fourth-1,ids) && !XMemMapPhysicalPages(at[3]+pg,fourth,ids));
    ids[fourth-1]--;
    CHECK(!XMemMapPhysicalPages(at[3],fourth,ids));
    ids[fourth-1]++;
    CHECK(XMemMapPhysicalPages(at[3],fourth,ids)==at[3]);
    CHECK(!XMemFreePhysicalPages(n,ids) && GetLastError()==ERROR_BUSY);
    CHECK(!XMemMapPhysicalPages(at[4],counts[4],ids+fourth));
    CHECK(!XMemMapPhysicalPages(at[0],counts[0],ids+2352));
    CHECK(!XMemMapPhysicalPages(at[1],counts[1],ids+2512));
    CHECK(!XMemMapPhysicalPages(at[2],counts[2],ids+2528));
    MEMORY_BASIC_INFORMATION m;
    CHECK(VirtualQuery(at[3],&m,sizeof m) && m.RegionSize==fourth*pg &&
          m.Protect==(PAGE_READWRITE|PAGE_WRITECOMBINE));
    auto last=(volatile DWORD *)(at[3]+fourth*pg-4);
    auto alias=XMemVirtualAlloc(nullptr,pg,0x20402000,0x23,0x2404);
    CHECK(alias && XMemMapPhysicalPages(alias,1,ids+fourth-1)==alias);
    auto aliasLast=(volatile DWORD *)((BYTE *)alias+pg-4);
    *last=0x839ab7c1;
    MemoryBarrier();
    CHECK(*aliasLast==0x839ab7c1);
    ID3D12Device3 *d;
    HR(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&d)));
    ImportedBuffer held;
    SIZE_T offset;
    HR(held.acquire(d,(const void *)last,4,&offset));
    CHECK(offset==fourth*pg-4 && held.resource()->GetDesc().Width==n*pg);
    scarlett_section_info q;
    CHECK(FAILED(scarlett_query_section(0x53430005,(const void *)last,5,&q)));
    D3D12_COMMAND_QUEUE_DESC qd={};
    qd.Type=D3D12_COMMAND_LIST_TYPE_COPY;
    ID3D12CommandQueue *queue;
    ID3D12CommandAllocator *alloc;
    ID3D12GraphicsCommandList *list;
    ID3D12Fence *fence;
    HR(d->CreateCommandQueue(&qd,IID_PPV_ARGS(&queue)));
    HR(d->CreateCommandAllocator(qd.Type,IID_PPV_ARGS(&alloc)));
    HR(d->CreateCommandList(0,qd.Type,alloc,nullptr,IID_PPV_ARGS(&list)));
    HR(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));
    D3D12_HEAP_PROPERTIES hp={};
    hp.Type=D3D12_HEAP_TYPE_DEFAULT;
    hp.CreationNodeMask=hp.VisibleNodeMask=1;
    auto desc=held.resource()->GetDesc();
    desc.Width=pg;
    ID3D12Resource *tmp;
    HR(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&tmp)));
    list->CopyBufferRegion(tmp,0,held.resource(),offset,4);
    HR(list->Close());
    ID3D12CommandList *batch[]={list};
    queue->ExecuteCommandLists(1,batch);
    HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    CHECK(event);
    HR(queue->Signal(fence,1));
    HR(fence->SetEventOnCompletion(1,event));
    CHECK(WaitForSingleObject(event,10000)==WAIT_OBJECT_0);
    *aliasLast=0;
    MemoryBarrier();
    CHECK(!*last);
    HR(alloc->Reset());
    HR(list->Reset(alloc,nullptr));
    list->CopyBufferRegion(held.resource(),offset,tmp,0,4);
    HR(list->Close());
    queue->ExecuteCommandLists(1,batch);
    HR(queue->Signal(fence,2));
    HR(fence->SetEventOnCompletion(2,event));
    CHECK(WaitForSingleObject(event,10000)==WAIT_OBJECT_0 && *last==0x839ab7c1 && *aliasLast==*last);
    CHECK(XMemMapPhysicalPages(at[3],fourth,nullptr)==at[3]);
    CHECK(FAILED(scarlett_query_section(0x53430005,(const void *)last,4,&q)));
    CHECK(XMemMapPhysicalPages(at[3],fourth,ids+n-fourth)==at[3]);
    ImportedBuffer tail;
    HR(tail.acquire(d,(const void *)last,4,&offset));
    CHECK(offset==n*pg-4);
    *last=12345;
    MemoryBarrier();
    CHECK(*(volatile DWORD *)((BYTE *)tail.cpu()+offset)==12345 && *aliasLast==0x839ab7c1);
    CHECK(XMemMapPhysicalPages(at[3],fourth,nullptr)==at[3]);
    CHECK(XMemMapPhysicalPages(alias,1,nullptr)==alias && VirtualFree(alias,0,MEM_RELEASE));
    for (auto a:at) CHECK(VirtualFree(a,0,MEM_RELEASE));
    CHECK(VirtualAlloc(at[3],fourth*pg,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE)==at[3]);
    *last=999;
    CHECK(!XMemMapPhysicalPages(at[3],fourth,ids) && *last==999 && VirtualFree(at[3],0,MEM_RELEASE));
    auto va2=(Alloc2)(void *)GetProcAddress(GetModuleHandleW(L"kernelbase.dll"),"VirtualAlloc2");
    CHECK(va2 && va2(nullptr,at[3],fourth*pg,MEM_RESERVE|MEM_RESERVE_PLACEHOLDER,PAGE_NOACCESS,nullptr,0)==at[3]);
    CHECK(!XMemMapPhysicalPages(at[3],fourth,ids)); /* same base, size and MBI; old policy expired */
    CHECK(XMemVirtualAlloc(at[3],fourth*pg,0x20406000,0x27,0x2004)==at[3]);
    CHECK(!XMemMapPhysicalPages(at[3],fourth,ids) && VirtualFree(at[3],0,MEM_RELEASE));
    CHECK(XMemFreePhysicalPages(n,ids) && *(volatile DWORD *)((BYTE *)tail.cpu()+offset)==12345);
    CloseHandle(event);
    tmp->Release(); fence->Release(); list->Release(); alloc->Release(); queue->Release(); d->Release();
    puts("PASS descriptor physical: WC extent, second VA alias, last-word CPU->GPU->title fenced copies; backing offset, reuse, map policy and lifetime");
}
