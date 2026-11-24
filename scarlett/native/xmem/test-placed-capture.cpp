#include "placed_capture.h"
#include <cstdint>
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
    constexpr SIZE_T pg=65536,n=2960,mib=1<<20,width=174*mib;
    const SIZE_T count[]={160,16,432,752,1600}, id[]={2352,2512,2528,0,752};
    const DWORD policy[]={0x1004,0x1404,0x8404,0x2404,0x2004};
    BYTE *at[5];
    auto va=(BYTE *)XMemVirtualAlloc(nullptr,n*pg,0x42000,7,PAGE_NOACCESS);
    CHECK(va);
    for (unsigned i=0;i<5;i++) {
        at[i]=va;
        if (i<4) CHECK(VirtualFree(va,count[i]*pg,MEM_RELEASE|MEM_PRESERVE_PLACEHOLDER));
        va+=count[i]*pg;
    }
    for (unsigned i=0;i<5;i++)
        CHECK(XMemVirtualAlloc(at[i],count[i]*pg,0x20406000,0x27,policy[i])==at[i]);
    ULONG_PTR ids[2][n];
    for (auto &run:ids) {
        ULONG_PTR pages=n;
        CHECK(XMemAllocatePhysicalPages(0x280,&pages,run) && pages==n);
    }
    ID3D12Device3 *d;
    HR(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&d)));
    auto map=[&](unsigned i, int g) {
        return XMemMapPhysicalPages(at[i],count[i],g<0?nullptr:ids[g]+id[i])==at[i];
    };
    auto denied=[&](const void *base,UINT64 size) {
        auto *p=reinterpret_cast<XsPlacedCapture *>(1);
        return FAILED(ScarlettCapturePlacedV1(d,base,size,&p)) && !p;
    };
    CHECK(map(2,0) && map(3,0) && denied(at[2],width)); // missing WB
    CHECK(map(4,1) && denied(at[2],width));              // right layout, wrong run
    CHECK(map(4,-1) && map(4,0));
    CHECK(denied(at[0],width) && denied(at[2]+1,width) && denied(at[2],width-1));
    CHECK(denied(at[2],width+1) && denied(at[3],width));
    XsPlacedCapture *captures[2];
    const UINT64 positions[]={74*mib-4,width-4};
    for (unsigned g=0;g<2;g++) {
        if (g) CHECK(map(2,1) && map(3,1) && map(4,1)); // same VAs, different run
        HR(ScarlettCapturePlacedV1(d,at[2],width,&captures[g]));
        for (unsigned i=0;i<2;i++) {
            auto *p=reinterpret_cast<volatile UINT *>(at[2]+positions[i]);
            *p=0x42800000+2*g+i; // after capture: live original backing, not a snapshot
        }
        MemoryBarrier();
        CHECK(map(2,-1) && map(3,-1) && map(4,-1));
        CHECK(XMemFreePhysicalPages(n,ids[g]) && denied(at[2],width));
    }
    for (auto base:at) CHECK(VirtualFree(base,0,MEM_RELEASE));
    CHECK(VirtualAlloc(at[2],width,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE)==at[2]);
    CHECK(denied(at[2],width));

    XsDataOperand op;
    auto invalid=[&](UINT64 at, UINT64 bytes) {
        op={reinterpret_cast<ID3D12Resource *>(1),1,1};
        return FAILED(ScarlettPlacedDataV1(captures[0],at,bytes,&op)) && !op.resource && !op.offset && !op.bytes;
    };
    CHECK(invalid(0,4) && invalid(27*mib-1,1) && invalid(27*mib-1,2));
    CHECK(invalid(27*mib,0) && invalid(74*mib-1,2) && invalid(width-1,2));
    CHECK(invalid(width,1) && invalid(UINT64_MAX,1) && invalid(27*mib,UINT64_MAX));
    HR(ScarlettPlacedDataV1(captures[0],27*mib,47*mib,&op)); CHECK(!op.offset && op.bytes==47*mib);
    HR(ScarlettPlacedDataV1(captures[0],74*mib,100*mib,&op)); CHECK(!op.offset && op.bytes==100*mib);
    CHECK(!GetProcAddress(GetModuleHandleW(L"xmem.dll"),"xmem_pin_placed"));

    D3D12_COMMAND_QUEUE_DESC qd={}; qd.Type=D3D12_COMMAND_LIST_TYPE_COPY;
    D3D12_HEAP_PROPERTIES h={}; h.Type=D3D12_HEAP_TYPE_READBACK; h.CreationNodeMask=h.VisibleNodeMask=1;
    D3D12_RESOURCE_DESC r={}; r.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER; r.Width=16;
    r.Height=r.DepthOrArraySize=r.MipLevels=r.SampleDesc.Count=1; r.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ID3D12CommandQueue *q; ID3D12CommandAllocator *a; ID3D12GraphicsCommandList *l; ID3D12Resource *rb;
    HR(d->CreateCommandQueue(&qd,IID_PPV_ARGS(&q)));
    HR(d->CreateCommandAllocator(qd.Type,IID_PPV_ARGS(&a)));
    HR(d->CreateCommandList(0,qd.Type,a,nullptr,IID_PPV_ARGS(&l)));
    HR(d->CreateCommittedResource(&h,D3D12_HEAP_FLAG_NONE,&r,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&rb)));
    unsigned j=0;
    for (auto p:captures) for (auto pos:positions) {
        HR(ScarlettPlacedDataV1(p,pos,4,&op));
        CHECK(op.resource && op.offset+4==op.resource->GetDesc().Width && op.bytes==4);
        l->CopyBufferRegion(rb,4*j++,op.resource,op.offset,op.bytes);
    }
    HR(l->Close());
    ID3D12CommandList *batch[]={l}; q->ExecuteCommandLists(1,batch);
    ID3D12Fence *f; HR(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&f)));
    HR(q->Signal(f,1)); HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr); CHECK(event);
    HR(f->SetEventOnCompletion(1,event)); CHECK(WaitForSingleObject(event,10000)==WAIT_OBJECT_0);
    UINT *words; HR(rb->Map(0,nullptr,reinterpret_cast<void **>(&words)));
    for (unsigned i=0;i<4;i++) CHECK(words[i]==0x42800000+i);
    rb->Unmap(0,nullptr); rb->Release(); l->Release(); a->Release(); q->Release();
    CloseHandle(event); f->Release();
    for (auto p:captures) ScarlettReleasePlacedV1(p);
    d->Release(); CHECK(VirtualFree(at[2],0,MEM_RELEASE));
    puts("PASS placed capture: incomplete/mixed rejected; code and crossing operands denied; two freed runs at identical VAs delivered distinct fenced WC/WB data");
}
