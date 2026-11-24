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
    const UINT64 boundaries[]={27*mib,74*mib};
    for (unsigned g=0;g<2;g++) {
        if (g) CHECK(map(2,1) && map(3,1) && map(4,1)); // same VAs, different run
        HR(ScarlettCapturePlacedV1(d,at[2],width,&captures[g]));
        for (unsigned i=0;i<2;i++) {
            auto *edge=at[2]+boundaries[i];
            edge[-1]=0x81+g+i;
            edge[0]=0x41+g+i;
        }
        at[2][width-1]=0x11+g;
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
    D3D12_RESOURCE_DESC r={}; r.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER; r.Width=12;
    r.Height=r.DepthOrArraySize=r.MipLevels=r.SampleDesc.Count=1; r.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ID3D12CommandQueue *q; ID3D12CommandAllocator *a; ID3D12GraphicsCommandList *l; ID3D12Resource *rb;
    HR(d->CreateCommandQueue(&qd,IID_PPV_ARGS(&q)));
    HR(d->CreateCommandAllocator(qd.Type,IID_PPV_ARGS(&a)));
    HR(d->CreateCommandList(0,qd.Type,a,nullptr,IID_PPV_ARGS(&l)));
    HR(d->CreateCommittedResource(&h,D3D12_HEAP_FLAG_NONE,&r,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&rb)));
    BYTE *mapped;
    HR(rb->Map(0,nullptr,reinterpret_cast<void **>(&mapped))); memset(mapped,0xdd,12); rb->Unmap(0,nullptr);
    auto refused=[&](ID3D12GraphicsCommandList *list,ID3D12Resource *dest,UINT64 at,UINT64 bytes,UINT64 to) {
        auto *guard=reinterpret_cast<XsPlacedCapture *>(1);
        return FAILED(ScarlettPlacedCopyToV1(captures[0],at,bytes,list,dest,to,&guard)) && !guard;
    };
    CHECK(refused(l,rb,27*mib-1,3,10) && refused(l,rb,width-1,2,10)); // even byte 10 must remain untouched
    CHECK(refused(l,rb,UINT64_MAX,2,0) && refused(l,rb,0,UINT64_MAX,0) && refused(l,rb,0,1,UINT64_MAX));
    CHECK(refused(l,rb,0,0,0) && refused(nullptr,rb,0,1,0) && refused(l,nullptr,0,1,0));
    CHECK(refused(l,op.resource,74*mib,4,0));
    // A second device on the same adapter: LUID equality alone would accept these.
    const GUID factoryClass={0x114863bf,0xc386,0x4aee,{0xb3,0x9d,0x8f,0xb,0xbb,0x06,0x29,0x55}};
    ID3D12DeviceFactory *factory;
    auto get=(decltype(&D3D12GetInterface))(void *)GetProcAddress(GetModuleHandleW(L"d3d12.dll"),"D3D12GetInterface");
    CHECK(get); HR(get(factoryClass,IID_PPV_ARGS(&factory)));
    HR(factory->SetFlags(D3D12_DEVICE_FACTORY_FLAG_DISALLOW_STORING_NEW_DEVICE_AS_SINGLETON));
    ID3D12Device3 *other;
    HR(factory->CreateDevice(nullptr,D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&other))); CHECK(other!=d);
    ID3D12Resource *foreign;
    ID3D12CommandAllocator *fa; ID3D12GraphicsCommandList *fl;
    HR(other->CreateCommittedResource(&h,D3D12_HEAP_FLAG_NONE,&r,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&foreign)));
    HR(other->CreateCommandAllocator(qd.Type,IID_PPV_ARGS(&fa)));
    HR(other->CreateCommandList(0,qd.Type,fa,nullptr,IID_PPV_ARGS(&fl)));
    CHECK(refused(l,foreign,0,1,0) && refused(fl,rb,0,1,0));
    HR(fl->Close()); fl->Release(); fa->Release(); foreign->Release(); other->Release(); factory->Release();

    XsPlacedCapture *holds[2][3];
    for (unsigned g=0;g<2;g++) {
        for (unsigned i=0;i<2;i++)
            HR(ScarlettPlacedCopyToV1(captures[g],boundaries[i]-1,2,l,rb,5*g+2*i,&holds[g][i]));
        HR(ScarlettPlacedCopyToV1(captures[g],width-1,1,l,rb,5*g+4,&holds[g][2]));
        ScarlettReleasePlacedV1(captures[g]); // only recording references survive submission
    }
    HR(l->Close());
    ID3D12CommandList *batch[]={l}; q->ExecuteCommandLists(1,batch);
    ID3D12Fence *f; HR(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&f)));
    HR(q->Signal(f,1)); HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr); CHECK(event);
    HR(f->SetEventOnCompletion(1,event)); CHECK(WaitForSingleObject(event,10000)==WAIT_OBJECT_0);
    HR(rb->Map(0,nullptr,reinterpret_cast<void **>(&mapped)));
    const BYTE expected[]={0x81,0x41,0x82,0x42,0x11,0x82,0x42,0x83,0x43,0x12,0xdd,0xdd};
    CHECK(!memcmp(mapped,expected,sizeof expected));
    rb->Unmap(0,nullptr); rb->Release(); l->Release(); a->Release(); q->Release();
    CloseHandle(event); f->Release();
    for (auto &g:holds) for (auto p:g) ScarlettReleasePlacedV1(p);
    d->Release(); CHECK(VirtualFree(at[2],0,MEM_RELEASE));
    puts("PASS placed copy: both boundaries and final byte in two freed generations, fenced after release/reuse; rejected copies preserved guards, foreign devices denied");
}
