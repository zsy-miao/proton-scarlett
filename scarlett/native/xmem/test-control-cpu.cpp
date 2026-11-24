#include "gpu_buffer.hpp"
#include <cstdio>
#include <initializer_list>
extern "C" __declspec(dllimport) void *WINAPI XMemVirtualAlloc(void *,SIZE_T,DWORD,ULONGLONG,DWORD);
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL line %d: %s err=%lu\n",__LINE__,#x,GetLastError());return 1;}}while(0)
int main()
{
    ID3D12Device3 *d=nullptr;
    CHECK(SUCCEEDED(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&d))));
    using Maker=HRESULT(WINAPI*)(ID3D12Device3*,SIZE_T,void**);
    auto make=(Maker)(void*)GetProcAddress(GetModuleHandleW(L"d3d12core.dll"),"ScarlettCpuControlViewV1");
    void *wrong=(void*)1; CHECK(make && FAILED(make(d,4096,&wrong)) && !wrong);
    CHECK(!XMemVirtualAlloc(nullptr,65535,0x20401000,0x283,0x42404));
    CHECK(!XMemVirtualAlloc(nullptr,65536,0x20401000,0x283,0x42404)); /* synthetic PE is not allowlisted */
    SIZE_T previous=0; uintptr_t first=0; bool reused=false;
    for(int iter=0;iter<16;iter++){
        void *view=nullptr;CHECK(SUCCEEDED(make(d,65536,&view)));auto p=(BYTE*)view;
        CHECK(p); MEMORY_BASIC_INFORMATION mi;
        CHECK(VirtualQuery(p,&mi,sizeof(mi)) && mi.State==MEM_COMMIT && mi.Type==MEM_MAPPED &&
            mi.Protect==(PAGE_READWRITE|PAGE_WRITECOMBINE) && mi.RegionSize==65536);
        memset(p,0,65536); *(volatile UINT64*)p=1; *(volatile DWORD*)(p+220)=42; MemoryBarrier();
        CHECK(*(volatile UINT64*)p==1 && *(volatile DWORD*)(p+220)==42);
        scarlett_section_info id={},i={};
        CHECK(SUCCEEDED(scarlett_query_section(0x53430015,p+65535,1,&id)) && id.base==p && !id.section &&
            id.view_size==65536 && id.offset>previous);previous=id.offset;
        CHECK(FAILED(scarlett_query_section(0x53430015,p+65535,2,&i)));
        for(ULONG op:{0x53430002u,0x53430005u,0x5343000au,0x5343000cu,0x53430010u,0x53430011u})
            CHECK(FAILED(scarlett_query_section(op,p+4,4,&i)) && !i.section);
        ImportedBuffer denied;SIZE_T offset=0;
        CHECK(FAILED(denied.acquire(d,p+4,4,&offset)) && !denied.resource());
        ID3D12Heap *heap=nullptr;
        CHECK(FAILED(d->OpenExistingHeapFromAddress(p,IID_PPV_ARGS(&heap))) && !heap);
        CHECK(FAILED(d->OpenExistingHeapFromAddress(p+8,IID_PPV_ARGS(&heap))) && !heap);
        CHECK(!VirtualFree(p,4096,MEM_RELEASE) && !VirtualFree(p+4,65536,MEM_RELEASE));
        CHECK(SUCCEEDED(scarlett_query_section(0x53430015,p,8,&i)) && i.offset==previous);
        if(!iter)first=(uintptr_t)p;else if(first==(uintptr_t)p)reused=true;
        CHECK(VirtualFree(p,65536,MEM_RELEASE));
        CHECK(FAILED(scarlett_query_section(0x53430015,p,1,&i)) && VirtualAlloc(p,65536,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE)==p);
        CHECK(!VirtualFree(p,65536,MEM_RELEASE) && VirtualFree(p,0,MEM_RELEASE));
    }
    CHECK(reused);
    ImportedBuffer good;HANDLE ordinary=nullptr;CHECK(SUCCEEDED(good.wc(d,65536,nullptr,&ordinary)));
    scarlett_section_info bad={};CHECK(FAILED(scarlett_query_section_handle(0x53430014,nullptr,65536,&bad,ordinary)));
    CloseHandle(ordinary); d->Release();
    puts("PASS RTX CPU-only control: 16 exact releases; reused VA with new identity; raw and general heap imports denied; WC control preserved");
}
