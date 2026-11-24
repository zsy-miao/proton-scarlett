// Test-only producer. The #4 host fixture records the draws, not the guest receiver.
#include "emitter.hpp"
#include "recorder.hpp"
#include "../../native/xmem/gpu_buffer.hpp"
extern "C" {
__declspec(dllimport) BOOL WINAPI XMemAllocatePhysicalPages(ULONGLONG,PULONG_PTR,PULONG_PTR);
__declspec(dllimport) BOOL WINAPI XMemFreePhysicalPages(ULONG_PTR,PULONG_PTR);
__declspec(dllimport) void *WINAPI XMemMapPhysicalPages(void *,ULONG_PTR,PULONG_PTR);
__declspec(dllimport) void *WINAPI XMemVirtualAlloc(void *,SIZE_T,DWORD,ULONGLONG,DWORD);
}
struct ClearSourceControl {
    Emitter writer;
    ImportedReadSpan live;
    ClearBindings b{};
    alignas(32) BYTE slot[32]{};
    BYTE *at[5]{};
    ULONG_PTR ids[2960];
    BYTE *record() const {return at[2]+b.offset;}
    void write(const float (&color)[4]){
        struct Producer : ClearRecorder {
            using ClearRecorder::ClearRecorder;
            HRESULT record() override {return S_OK;}
        } rec(b,nullptr);
        CK(rec.guest);writer.run(rec.guest,b.pso.Get(),b.root.Get(),b.slot,at[2],b.alias,color);
        MemoryBarrier();HR(rec.status);CK(rec.step==9);
        const DWORD headers[]={0x8e3,0,0,0xee4};CK(!memcmp(record(),headers,16));
        for(unsigned i=0;i<3;i++)CK(!memcmp(record()+28+i*28,color,16));
    }
    ClearSourceControl(ID3D12Device *d,IUnknown *p,IUnknown *r){
        constexpr SIZE_T pg=65536;
        const UINT pages[]={160,16,432,752,1600},policy[]={0x1004,0x1404,0x8404,0x2404,0x2004};
        auto va=(BYTE*)XMemVirtualAlloc(nullptr,2960*pg,0x42000,7,PAGE_NOACCESS);CK(va);
        for(unsigned i=0;i<5;i++){at[i]=va;if(i<4)CK(VirtualFree(va,pages[i]*pg,MEM_RELEASE|MEM_PRESERVE_PLACEHOLDER));va+=pages[i]*pg;}
        for(unsigned i=0;i<5;i++)CK(XMemVirtualAlloc(at[i],pages[i]*pg,0x20406000,0x27,policy[i])==at[i]);
        ULONG_PTR n=2960;CK(XMemAllocatePhysicalPages(0x280,&n,ids) && n==2960);
        CK(XMemMapPhysicalPages(at[2],432,ids+2528)==at[2]);
        b.pso=p;b.root=r;b.slot=(SIZE_T)slot;b.alias=(SIZE_T)at[2];b.offset=144;
        write({.125f,.375f,.625f,.875f});
        ComPtr<ID3D12Device3> d3;HR(d->QueryInterface(IID_PPV_ARGS(&d3)));HR(live.acquire(d3.Get(),record(),100));
    }
    bool replaced=false;
    void replace(){
        CK(XMemMapPhysicalPages(at[3],752,nullptr)==at[3] && XMemMapPhysicalPages(at[4],1600,nullptr)==at[4]);
        CK(XMemMapPhysicalPages(at[2],432,nullptr)==at[2] && XMemFreePhysicalPages(2960,ids));
        for(auto va:at)CK(VirtualFree(va,0,MEM_RELEASE));
        CK(VirtualAlloc(at[2],27u<<20,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE)==at[2]);replaced=true;
    }
    ~ClearSourceControl(){CK(replaced && VirtualFree(at[2],0,MEM_RELEASE));}
};
