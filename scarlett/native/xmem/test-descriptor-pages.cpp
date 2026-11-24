#include "gpu_buffer.hpp"
#include <cstdio>
#include <initializer_list>

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
    CHECK(XMemMapPhysicalPages(at[4],counts[4],ids+fourth)==at[4]);
    CHECK(XMemMapPhysicalPages(at[0],counts[0],ids+2352)==at[0]);
    CHECK(XMemMapPhysicalPages(at[1],counts[1],ids+2512)==at[1]);
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
    CHECK(offset==fourth*pg-4 && held.resource()->GetDesc().Width==fourth*pg);
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
    // GPU reads from both restricted views; both directions on WB/RW. Verify independently of resource->Map().
    ImportedBuffer wb;
    auto wbLast=(DWORD *)(at[4]+counts[4]*pg-4);
    HR(wb.acquire(d,wbLast,4,&offset));
    CHECK(offset==counts[4]*pg-4 && wb.resource()->GetDesc().Width==counts[4]*pg);
    D3D12_HEAP_PROPERTIES wbProps; D3D12_HEAP_FLAGS wf;
    HR(wb.resource()->GetHeapProperties(&wbProps,&wf));
    CHECK(wbProps.CPUPageProperty==D3D12_CPU_PAGE_PROPERTY_WRITE_BACK);
    auto out=(volatile DWORD *)at[4];
    auto aLast=(DWORD *)(at[0]+counts[0]*pg-4), bLast=(DWORD *)(at[1]+counts[1]*pg-4);
    ImportedReadSpan ra,rb;
    HR(ra.acquire(d,aLast,4)); HR(rb.acquire(d,bLast,4));
    for (unsigned i: {0u,1u,4u}) {
        CHECK(VirtualQuery(at[i],&m,sizeof m) && m.RegionSize==counts[i]*pg &&
            m.Protect==(DWORD)(PAGE_READWRITE|(prot[i]&PAGE_WRITECOMBINE)));
        if (i!=4) { ImportedBuffer denied; SIZE_T o; CHECK(FAILED(denied.acquire(d,at[i],4,&o))); }
    }
    *aLast=0xb5f30001; *bLast=0xb5f30002; *wbLast=0xb5f30003; MemoryBarrier();
    HR(alloc->Reset()); HR(list->Reset(alloc,nullptr));
    HR(ra.copy_to(list,tmp,0)); HR(rb.copy_to(list,tmp,4));
    list->CopyBufferRegion(tmp,8,wb.resource(),offset,4);
    HR(list->Close()); queue->ExecuteCommandLists(1,batch); HR(queue->Signal(fence,3));
    HR(fence->SetEventOnCompletion(3,event)); CHECK(WaitForSingleObject(event,10000)==WAIT_OBJECT_0);
    HR(alloc->Reset()); HR(list->Reset(alloc,nullptr));
    list->CopyBufferRegion(wb.resource(),0,tmp,0,12);
    HR(list->Close()); queue->ExecuteCommandLists(1,batch); HR(queue->Signal(fence,4));
    HR(fence->SetEventOnCompletion(4,event)); CHECK(WaitForSingleObject(event,10000)==WAIT_OBJECT_0);
    CHECK(out[0]==*aLast && out[1]==*bLast && out[2]==*wbLast);
    CHECK(XMemMapPhysicalPages(alias,1,nullptr)==alias);
    CHECK(!XMemMapPhysicalPages(alias,1,ids+fourth)); // WC alias of a WB ID denied
    CHECK(!XMemMapPhysicalPages(alias,1,ids+2512)); // cannot get RW alias via this worker arena
    CHECK(XMemMapPhysicalPages(at[3],fourth,nullptr)==at[3]);
    CHECK(!XMemMapPhysicalPages(at[3],fourth,ids+fourth)); // no WC map covering WB IDs
    CHECK(XMemMapPhysicalPages(at[3],fourth,ids)==at[3] && *last==0x839ab7c1);
    for (unsigned i: {3u,4u,0u,1u}) CHECK(XMemMapPhysicalPages(at[i],counts[i],nullptr)==at[i]);
    CHECK(VirtualFree(alias,0,MEM_RELEASE));
    for (auto a:at) CHECK(VirtualFree(a,0,MEM_RELEASE));
    CHECK(VirtualAlloc(at[3],fourth*pg,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE)==at[3]);
    *last=999;
    CHECK(!XMemMapPhysicalPages(at[3],fourth,ids) && *last==999 && VirtualFree(at[3],0,MEM_RELEASE));
    auto va2=(Alloc2)(void *)GetProcAddress(GetModuleHandleW(L"kernelbase.dll"),"VirtualAlloc2");
    CHECK(va2 && va2(nullptr,at[3],fourth*pg,MEM_RESERVE|MEM_RESERVE_PLACEHOLDER,PAGE_NOACCESS,nullptr,0)==at[3]);
    CHECK(!XMemMapPhysicalPages(at[3],fourth,ids)); /* same base, size and MBI; old policy expired */
    CHECK(XMemVirtualAlloc(at[3],fourth*pg,0x20406000,0x27,0x2004)==at[3]);
    CHECK(!XMemMapPhysicalPages(at[3],fourth,ids) && VirtualFree(at[3],0,MEM_RELEASE));
    CHECK(XMemFreePhysicalPages(n,ids) && *(volatile DWORD *)((BYTE *)wb.cpu()+offset)==0xb5f30003);
    CloseHandle(event);
    tmp->Release(); fence->Release(); list->Release(); alloc->Release(); queue->Release(); d->Release();
    puts("PASS actual 2960 IDs: WC/RW, WB/RW, WB/source and WC/source, disjoint policies, fenced GPU copies, RX rejected, unmap/reuse/free");
}
