#include "gpu_buffer.hpp"
#include <cstdio>
extern "C" {
__declspec(dllimport) BOOL WINAPI XMemAllocatePhysicalPages(ULONGLONG,PULONG_PTR,PULONG_PTR);
__declspec(dllimport) BOOL WINAPI XMemFreePhysicalPages(ULONG_PTR,PULONG_PTR);
__declspec(dllimport) void *WINAPI XMemMapPhysicalPages(void *,ULONG_PTR,PULONG_PTR);
__declspec(dllimport) void *WINAPI XMemVirtualAlloc(void *,SIZE_T,DWORD,ULONGLONG,DWORD);
}
#define CK(x) do { if (!(x)) {fprintf(stderr,"FAIL %d: %s error %lu\n",__LINE__,#x,GetLastError()); ExitProcess(1);} } while(0)
#define HR(x) do { HRESULT h_=(x); if (FAILED(h_)) {fprintf(stderr,"FAIL %d: %s hr=%#lx\n",__LINE__,#x,(unsigned long)h_); ExitProcess(1);} } while(0)
int main()
{
    constexpr SIZE_T n=99776, size=n*65536, boundary=1ULL<<30;
    static ULONG_PTR id[n];
    SIZE_T count=n;
    CK(!XMemAllocatePhysicalPages(0x381,&count,id) && !count && GetLastError()==ERROR_NOT_SUPPORTED);
    count=n; CK(XMemAllocatePhysicalPages(0x380,&count,id) && count==n);
    for (SIZE_T i=0;i<n;i++) CK(!(id[i]&15) && id[i]>>4<0x1fffff && (!i || id[i]==id[i-1]+16));
    auto reserve=[](SIZE_T bytes) {return (BYTE*)XMemVirtualAlloc(nullptr,bytes,0x20402000,0x23,0x2004);};
    CK(!reserve(size-65536));
    BYTE *a=reserve(size), *b=reserve(size);
    CK(a && b && a!=b);
    auto cpu=XMemVirtualAlloc(nullptr,65536,0x20402000,0x21,PAGE_READWRITE);
    CK(cpu && !XMemMapPhysicalPages(cpu,1,id) && VirtualFree(cpu,0,MEM_RELEASE));
    CK(!XMemMapPhysicalPages(a,n-1,id) && !XMemMapPhysicalPages(a+65536,n-1,id+1));
    ++id[n-1]; CK(!XMemMapPhysicalPages(a,n,id) && !XMemFreePhysicalPages(n,id)); --id[n-1];
    CK(XMemMapPhysicalPages(a,n,id)==a && XMemMapPhysicalPages(b,n,id)==b);
    CK(!XMemMapPhysicalPages(a,n,id) && !XMemMapPhysicalPages(a,n-1,nullptr));
    CK(!XMemFreePhysicalPages(n,id) && GetLastError()==ERROR_BUSY);
    for (unsigned i=0;i<6;i++) {
        SIZE_T off=(SIZE_T)i<<30, bytes=i==5 ? 1116ULL<<20 : boundary;
        MEMORY_BASIC_INFORMATION m;
        CK(VirtualQuery(a+off,&m,sizeof m) && m.BaseAddress==a+off && m.RegionSize==bytes && m.Protect==PAGE_READWRITE);
        *(volatile DWORD *)(b+off)=0x1200+i;
        *(volatile DWORD *)(a+off+bytes-4)=0x3400+i;
        CK(*(volatile DWORD *)(a+off)==0x1200+i && *(volatile DWORD *)(b+off+bytes-4)==0x3400+i);
    }
    ID3D12Device3 *dev=nullptr;
    HR(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&dev)));
    {
        ImportedBuffer left,right,bad;
        SIZE_T l,r,z;
        HR(left.acquire(dev,a+boundary-4,4,&l));
        HR(right.acquire(dev,b+boundary,4,&r));
        CK(l==boundary-4 && !r && FAILED(bad.acquire(dev,a+boundary-4,8,&z)));
        D3D12_COMMAND_QUEUE_DESC qd={}; qd.Type=D3D12_COMMAND_LIST_TYPE_COPY;
        ID3D12CommandQueue *q; ID3D12CommandAllocator *ca; ID3D12GraphicsCommandList *cl; ID3D12Fence *f;
        HR(dev->CreateCommandQueue(&qd,IID_PPV_ARGS(&q)));
        HR(dev->CreateCommandAllocator(qd.Type,IID_PPV_ARGS(&ca)));
        HR(dev->CreateCommandList(0,qd.Type,ca,nullptr,IID_PPV_ARGS(&cl)));
        HR(dev->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&f)));
        HANDLE ev=CreateEventW(nullptr,FALSE,FALSE,nullptr); CK(ev);
        auto submit=[&](UINT64 value) {
            HR(cl->Close()); ID3D12CommandList *lists[]={cl}; q->ExecuteCommandLists(1,lists);
            HR(q->Signal(f,value)); HR(f->SetEventOnCompletion(value,ev)); CK(WaitForSingleObject(ev,15000)==WAIT_OBJECT_0);
        };
        auto lw=(volatile DWORD*)(a+boundary-4), rw=(volatile DWORD*)(b+boundary);
        *lw=0xabcde001; *rw=0x13578888; MemoryBarrier();
        cl->CopyBufferRegion(right.resource(),r,left.resource(),l,4); submit(1);
        CK(*rw==0xabcde001 && *(volatile DWORD*)(a+boundary)==*rw);
        *rw=0x56781111; MemoryBarrier();
        HR(ca->Reset()); HR(cl->Reset(ca,nullptr));
        cl->CopyBufferRegion(left.resource(),l,right.resource(),r,4); submit(2);
        CK(*lw==0x56781111 && *(volatile DWORD*)(b+boundary-4)==*lw);
        CK(XMemMapPhysicalPages(a,n,nullptr)==a && !XMemFreePhysicalPages(n,id) && GetLastError()==ERROR_BUSY);
        CK(XMemMapPhysicalPages(b,n,nullptr)==b);
        MEMORY_BASIC_INFORMATION m;
        CK(VirtualQuery(a,&m,sizeof m) && m.State==MEM_RESERVE && m.AllocationBase==a && m.RegionSize==size);
        CK(XMemMapPhysicalPages(a,n,id)==a && *lw==0x56781111 && *(volatile DWORD*)(a+boundary)==0x56781111);
        CK(XMemMapPhysicalPages(a,n,nullptr)==a);
        CK(VirtualFree(a,0,MEM_RELEASE));
        CK(VirtualAlloc(a,size,MEM_RESERVE,PAGE_NOACCESS)==a && !XMemMapPhysicalPages(a,n,id));
        CK(VirtualFree(a,0,MEM_RELEASE) && XMemFreePhysicalPages(n,id) && !XMemFreePhysicalPages(n,id));
        CK(!XMemMapPhysicalPages(b,n,id) && VirtualFree(b,0,MEM_RELEASE));
        CK(*(volatile DWORD *)((BYTE*)left.cpu()+l)==0x56781111 && *(volatile DWORD *)right.cpu()==0x56781111);
        HR(dev->GetDeviceRemovedReason());
        CK(CloseHandle(ev)); f->Release(); cl->Release(); ca->Release(); q->Release();
    }
    dev->Release();
    puts("PASS: six WB chunks, two public aliases, fenced GPU copies both directions across 1 GiB, invalid inputs, remap, retained import and teardown");
}
