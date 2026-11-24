#include "gpu_buffer.hpp"
#include <cstdio>
#include <cstring>
extern "C" __declspec(dllimport) void *WINAPI XMemVirtualAlloc(void *,SIZE_T,DWORD,ULONGLONG,DWORD);
#define CK(x) do{if(!(x)){fprintf(stderr,"line %u: %s (last error %lu)\n",__LINE__,#x,GetLastError());ExitProcess(1);}}while(0)
#define HR(x) do{HRESULT h=(x);if(FAILED(h)){fprintf(stderr,"line %u: %s: HRESULT %#lx\n",__LINE__,#x,(unsigned long)h);ExitProcess(1);}}while(0)
int main(int argc,char **argv)
{
    const SIZE_T n=65536; const DWORD marker=0xbede27ca;
    auto title=(DWORD*)XMemVirtualAlloc(nullptr,n,0x20401000,0x283,0x42404);
    if(argc==2 && !strcmp(argv[1],"negative")) {
        CK(!title && GetLastError()!=ERROR_SUCCESS);puts("PASS unavailable control page rejected");return 0;
    }
    CK(title);
    MEMORY_BASIC_INFORMATION mi;
    CK(VirtualQuery(title,&mi,sizeof mi)==sizeof mi && mi.RegionSize==n && mi.State==MEM_COMMIT &&
       mi.Protect==(PAGE_READWRITE|PAGE_WRITECOMBINE) && mi.Type==MEM_MAPPED);
    memset(title,0x21,n);MemoryBarrier();memset(title,0,n);MemoryBarrier();
    for(SIZE_T j=0;j<n/4;j++) CK(!((volatile DWORD*)title)[j]);
    CK(!VirtualFree(title,n-4096,MEM_RELEASE) && !VirtualFree(title+1,n,MEM_RELEASE));
    scarlett_section_info held,bad;
    HR(scarlett_query_section(0x5343000a,title+n/4-1,4,&held));
    CK(held.section && held.view_size==n && held.offset==n-4);
    CK(FAILED(scarlett_query_section(0x53430005,title,1,&bad)) &&
       FAILED(scarlett_query_section(0x5343000a,title+n/4-1,5,&bad)));
    HANDLE ordinary=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,n,nullptr); CK(ordinary);
    CK(FAILED(scarlett_query_section_handle(0x53430008,nullptr,n,&bad,ordinary)) && CloseHandle(ordinary));
    ID3D12Device3 *d;
    HR(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&d)));
    {
        HANDLE wc_handle=nullptr;
        ImportedBuffer plain_wc,wrong_import;
        HR(plain_wc.wc(d,n,nullptr,&wc_handle));
        CK(wc_handle && FAILED(wrong_import.wc(d,n,wc_handle,nullptr,true)));
        CK(FAILED(scarlett_query_section_handle(0x53430008,nullptr,n,&bad,wc_handle)));
        HR(scarlett_query_section_handle(0x53430004,nullptr,n,&bad,wc_handle));
        CK(!VirtualFree(bad.base,n,MEM_RELEASE) && VirtualFree(bad.base,0,MEM_RELEASE));
        CK(CloseHandle(wc_handle));
    }
    ImportedBuffer bind;
    SIZE_T off;HR(bind.acquire(d,title+n/4-1,4,&off)); CK(off==n-4 && bind.resource()->GetDesc().Width==n);
    D3D12_HEAP_PROPERTIES hp; D3D12_HEAP_FLAGS flags;HR(bind.resource()->GetHeapProperties(&hp,&flags));
    CK(hp.Type==D3D12_HEAP_TYPE_CUSTOM && hp.CPUPageProperty==D3D12_CPU_PAGE_PROPERTY_WRITE_COMBINE);
    title[0]=marker;MemoryBarrier();CK(*(volatile DWORD*)bind.cpu()==marker);
    ID3D12CommandQueue *q; ID3D12CommandAllocator *alloc; ID3D12GraphicsCommandList *list;
    ID3D12Fence *gate,*done;
    D3D12_COMMAND_QUEUE_DESC qd={};qd.Type=D3D12_COMMAND_LIST_TYPE_COPY;
    HR(d->CreateCommandQueue(&qd,IID_PPV_ARGS(&q)));HR(d->CreateCommandAllocator(qd.Type,IID_PPV_ARGS(&alloc)));
    HR(d->CreateCommandList(0,qd.Type,alloc,nullptr,IID_PPV_ARGS(&list)));
    HR(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&gate)));HR(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&done)));
    ID3D12Resource *tmp;hp={};hp.Type=D3D12_HEAP_TYPE_DEFAULT;hp.CreationNodeMask=hp.VisibleNodeMask=1;
    auto desc=bind.resource()->GetDesc();
    HR(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&tmp)));
    HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);CK(event);
    ID3D12CommandList *batch[]={list};
    for(unsigned iter=1;iter<=3;iter++) {
        if(iter>1){HR(alloc->Reset());HR(list->Reset(alloc,nullptr));}
        if(iter==1)list->CopyBufferRegion(tmp,0,bind.resource(),0,4);
        else list->CopyBufferRegion(bind.resource(),iter==2?4:off,tmp,0,4);
        HR(list->Close());if(iter==3)HR(q->Wait(gate,1));
        q->ExecuteCommandLists(1,batch);HR(q->Signal(done,iter));
        if(iter==3){
            CK(VirtualFree(title,n,MEM_RELEASE));
            CK(FAILED(scarlett_query_section(0x5343000a,title,4,&bad)));
            CK(VirtualAlloc(title,n,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE)==title);
            CK(!VirtualFree(title,n,MEM_RELEASE) && GetLastError()==ERROR_INVALID_PARAMETER);
            title[n/4-1]=42; CK(done->GetCompletedValue()==2);HR(gate->Signal(1));
        }
        HR(done->SetEventOnCompletion(iter,event));CK(WaitForSingleObject(event,15000)==WAIT_OBJECT_0);HR(d->GetDeviceRemovedReason());
        if(iter==2)CK(title[1]==marker);
    }
    ImportedBuffer late;HR(late.wc(d,n,held.section,nullptr,true));CK(CloseHandle(held.section));
    CK(((volatile DWORD*)late.cpu())[n/4-1]==marker && title[n/4-1]==42);
    CK(VirtualFree(title,0,MEM_RELEASE));CK(CloseHandle(event));
    q->Release();list->Release();alloc->Release();gate->Release();done->Release();tmp->Release();d->Release();
    puts("PASS control64 AMD: qualified public page; live CPU/GPU visibility; exact-size release; delayed GPU write stayed on old backing after VA reuse");
}
