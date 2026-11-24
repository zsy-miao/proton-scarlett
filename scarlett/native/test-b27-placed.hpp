// Fixture-only: a copy head and a separately bound graphics tail share a retirement fence.
#include "xmem/placed_capture.h"
extern "C" {
__declspec(dllimport) BOOL WINAPI XMemAllocatePhysicalPages(ULONGLONG,PULONG_PTR,PULONG_PTR);
__declspec(dllimport) BOOL WINAPI XMemFreePhysicalPages(ULONG_PTR,PULONG_PTR);
__declspec(dllimport) void *WINAPI XMemMapPhysicalPages(void *,ULONG_PTR,PULONG_PTR);
__declspec(dllimport) void *WINAPI XMemVirtualAlloc(void *,SIZE_T,DWORD,ULONGLONG,DWORD);
}

struct B27Copy {
    static constexpr UINT offset=0x3ff20, bytes=96, width=256;
    static_assert(offset+bytes==0x3ff80 && width==D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
    static constexpr UINT count[]={160,16,432,752,1600}, first[]={2352,2512,2528,0,752};
    BYTE *at[5], expected[width];
    ULONG_PTR ids[2960];
    ComPtr<ID3D12Resource> cb,audit;
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> head;
    ComPtr<ID3D12Fence> gate;
    XsPlacedCapture *retained=nullptr;

    int map_run() {
        ULONG_PTR n=2960;
        CK(XMemAllocatePhysicalPages(0x280,&n,ids) && n==2960);
        for (UINT i=2;i<5;i++) CK(XMemMapPhysicalPages(at[i],count[i],ids+first[i])==at[i]);
        return 0;
    }
    int free_run() {
        for (UINT i=2;i<5;i++) CK(XMemMapPhysicalPages(at[i],count[i],nullptr)==at[i]);
        CK(XMemFreePhysicalPages(2960,ids));
        return 0;
    }
    HRESULT record(XsPlacedCapture *source,ID3D12GraphicsCommandList *l,XsPlacedCapture **hold) {
        *hold=nullptr;
        if (!ScarlettPlacedMatchesV1(source,at[2])) return E_INVALIDARG;
        return ScarlettPlacedCopyToV1(source,offset,bytes,l,cb.Get(),0,hold);
    }
    int prepare(ID3D12Device *d, ID3D12Resource *seed) {
        constexpr SIZE_T page=65536;
        const UINT policy[]={0x1004,0x1404,0x8404,0x2404,0x2004};
        auto va=(BYTE*)XMemVirtualAlloc(nullptr,2960*page,0x42000,7,PAGE_NOACCESS);CK(va);
        for (UINT i=0;i<5;i++) {
            at[i]=va;
            if (i<4) CK(VirtualFree(va,count[i]*page,MEM_RELEASE|MEM_PRESERVE_PLACEHOLDER));
            va+=count[i]*page;
        }
        for (UINT i=0;i<5;i++) CK(XMemVirtualAlloc(at[i],count[i]*page,0x20406000,0x27,policy[i])==at[i]);
        CK(!map_run());
        memset(at[2]+offset,0x11,bytes);                 // a record-time snapshot must fail
        memset(at[2]+offset+bytes,0x77,width-bytes);     // includes the adjacent 100-byte VB
        MemoryBarrier();

        D3D12_RESOURCE_DESC rd=seed->GetDesc();CK(rd.Dimension==D3D12_RESOURCE_DIMENSION_BUFFER && rd.Width==width);
        D3D12_HEAP_PROPERTIES hp={};hp.Type=D3D12_HEAP_TYPE_DEFAULT;hp.CreationNodeMask=hp.VisibleNodeMask=1;
        HR(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&cb)));
        hp.Type=D3D12_HEAP_TYPE_READBACK;
        HR(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&audit)));
        HR(d->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)));
        HR(d->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&head)));
        head->CopyBufferRegion(cb.Get(),bytes,seed,bytes,width-bytes);
        ComPtr<ID3D12Device3> d3;HR(d->QueryInterface(IID_PPV_ARGS(&d3)));
        XsPlacedCapture *original=nullptr;HR(ScarlettCapturePlacedV1(d3.Get(),at[2],174ull<<20,&original));
        HR(record(original,head.Get(),&retained));CK(retained);
        ScarlettReleasePlacedV1(original);
        D3D12_RESOURCE_BARRIER b={};b.Transition={cb.Get(),0,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER};
        head->ResourceBarrier(1,&b);HR(head->Close());
        HR(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&gate)));
        return 0;
    }
    void read_after_draw(ID3D12GraphicsCommandList *tail) {
        D3D12_RESOURCE_BARRIER b={};b.Transition={cb.Get(),0,D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER,D3D12_RESOURCE_STATE_COPY_SOURCE};
        tail->ResourceBarrier(1,&b);tail->CopyBufferRegion(audit.Get(),0,cb.Get(),0,width);
    }
    int replace_while_queued(const UINT (&vals)[4], ID3D12GraphicsCommandList *fresh) {
        memset(expected,0xcc,width);
        for (UINT i=0;i<bytes;i++) expected[i]=BYTE(3*i+7);
        memcpy(expected+32,vals,sizeof vals);
        memcpy(at[2]+offset,expected,bytes);
        MemoryBarrier();
        CK(!free_run() && !map_run());                   // valid arena, identical VA, new generation
        memset(at[2]+offset,0xcd,width);
        MemoryBarrier();
        XsPlacedCapture *no=(XsPlacedCapture*)1;
        CK(record(retained,fresh,&no)==E_INVALIDARG && !no);
        return 0;
    }
    int retire() {
        BYTE *m;HR(audit->Map(0,nullptr,(void**)&m));
        CK(!memcmp(m,expected,bytes));
        CK(!memcmp(m+bytes,expected+bytes,width-bytes));
        audit->Unmap(0,nullptr);
        ScarlettReleasePlacedV1(retained);
        CK(!free_run());
        for (auto va:at) CK(VirtualFree(va,0,MEM_RELEASE));
        return 0;
    }
};
