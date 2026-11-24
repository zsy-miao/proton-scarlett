#include <d3d12.h>
#include <cstdio>
#include <cstdint>
#include <initializer_list>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
extern "C" HRESULT WINAPI D3D12XboxCreateDevice(void *,const void *,REFIID,void **);
extern "C" HRESULT xs_color_allocation_info(IUnknown *,UINT,UINT,const void *,D3D12_RESOURCE_ALLOCATION_INFO *);
extern "C" int r8_bad_host(UINT64,UINT64,const void *);
#define CK(x) do { if (!(x)) { std::fprintf(stderr,"FAIL %d %s\n",__LINE__,#x); return 1; } } while(0)
#define HR(x) CK(SUCCEEDED(x))

int main()
{
    DWORD params[20]={}; params[0]=0x20004; params[2]=0x100000; params[10]=1; params[15]=0x20;
    const IID boot={0x5405c344,0xd457,0x444e,{0xb4,0xdd,0x23,0x66,0xe4,0x5a,0xee,0x39}};
    ComPtr<IUnknown> dev; HR(D3D12XboxCreateDevice(nullptr,params,boot,(void **)dev.GetAddressOf()));
    using Query=D3D12_RESOURCE_ALLOCATION_INFO *(WINAPI *)(D3D12_RESOURCE_ALLOCATION_INFO *,IUnknown *,UINT,UINT,const void *);
    auto slot=(Query)((*reinterpret_cast<void***>(dev.Get()))[0xc8/8]);
    DWORD w[]={3,0,4096,0,256,0,128,0x10001,61,1,0,0,0x300000};
    SYSTEM_INFO sys; GetSystemInfo(&sys);
    auto pages=(BYTE *)VirtualAlloc(nullptr,2*sys.dwPageSize,MEM_RESERVE,PAGE_NOACCESS);
    CK(pages && VirtualAlloc(pages,sys.dwPageSize,MEM_COMMIT,PAGE_READWRITE)==pages);
    BYTE *prefix=pages+sys.dwPageSize-sizeof w;
    struct { UINT64 before; D3D12_RESOURCE_ALLOCATION_INFO info; UINT64 after; } got={1,{},2};
    ComPtr<ID3D12Device> d; HR(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&d)));
    D3D12_RESOURCE_DESC rd={}; memcpy(&rd,w,sizeof w); rd.Flags=D3D12_RESOURCE_FLAG_NONE;
    for (DWORD align:{0u,4096u}) {
        w[2]=align; memcpy(prefix,w,sizeof w); rd.Alignment=align;
        CK(slot(&got.info,dev.Get(),1,1,prefix)==&got.info && got.before==1 && got.after==2);
        auto native=d->GetResourceAllocationInfo(1,1,&rd);
        CK(!memcmp(&got.info,&native,sizeof native));
        CK(native.Alignment==(align?4096u:65536u) && native.SizeInBytes==(align?32768u:65536u));
        CK(r8_bad_host(UINT64_MAX,65536,prefix) && r8_bad_host(UINT64_MAX,4096,prefix) &&
            r8_bad_host(0,4096,prefix) && r8_bad_host(32768,0,prefix));
    }
    auto deny=[&](UINT node,UINT count,const void *p) {
        D3D12_RESOURCE_ALLOCATION_INFO bad={7,8};
        return FAILED(xs_color_allocation_info(dev.Get(),node,count,p,&bad)) && !bad.SizeInBytes && !bad.Alignment;
    };
    for (UINT k:{0u,2u,4u,5u,6u,7u,8u,9u,10u,11u,12u}) {
        w[k]^=1; CK(deny(1,1,w)); w[k]^=1;
    }
    CK(deny(0,1,prefix) && deny(1,2,prefix) && deny(1,1,prefix+1));
    CK(VirtualFree(pages,0,MEM_RELEASE));

    // Three adjacent small placements, none at a 64 KiB boundary. Seed the guards
    // first, then write the middle texture and check all three after the fence.
    constexpr UINT bytes=256*128, total=3*bytes;
    D3D12_HEAP_DESC hd={}; hd.SizeInBytes=2*65536; hd.Properties.Type=D3D12_HEAP_TYPE_DEFAULT;
    hd.Properties.CreationNodeMask=hd.Properties.VisibleNodeMask=1;
    hd.Flags=D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES;
    ComPtr<ID3D12Heap> heap; HR(d->CreateHeap(&hd,IID_PPV_ARGS(&heap)));
    ComPtr<ID3D12Resource> image[3],upload,readback;
    for (UINT j=0;j<3;j++)
        HR(d->CreatePlacedResource(heap.Get(),4096+j*got.info.SizeInBytes,&rd,D3D12_RESOURCE_STATE_COPY_DEST,
                nullptr,IID_PPV_ARGS(&image[j])));
    D3D12_RESOURCE_DESC buf={}; buf.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER; buf.Width=total;
    buf.Height=buf.DepthOrArraySize=buf.MipLevels=buf.SampleDesc.Count=1; buf.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    auto hp=hd.Properties; hp.Type=D3D12_HEAP_TYPE_UPLOAD;
    HR(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&buf,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&upload)));
    hp.Type=D3D12_HEAP_TYPE_READBACK;
    HR(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&buf,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback)));
    auto value=[](UINT n) { return (BYTE)((n*29u)^(n>>5)^(n>>8)^(n/bytes*83u)); };
    BYTE *p=nullptr; HR(upload->Map(0,nullptr,(void **)&p));
    for (UINT n=0;n<total;n++) p[n]=value(n);
    upload->Unmap(0,nullptr);
    D3D12_COMMAND_QUEUE_DESC qd={}; ComPtr<ID3D12CommandQueue> q; ComPtr<ID3D12CommandAllocator> a;
    ComPtr<ID3D12GraphicsCommandList> l; ComPtr<ID3D12Fence> f;
    HR(d->CreateCommandQueue(&qd,IID_PPV_ARGS(&q))); HR(d->CreateCommandAllocator(qd.Type,IID_PPV_ARGS(&a)));
    HR(d->CreateCommandList(0,qd.Type,a.Get(),nullptr,IID_PPV_ARGS(&l)));
    D3D12_TEXTURE_COPY_LOCATION loc[3]={},linear={}; linear.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    linear.PlacedFootprint.Footprint={rd.Format,256,128,1,256}; linear.pResource=upload.Get();
    for (UINT j:{0u,2u,1u}) {
        loc[j].pResource=image[j].Get(); loc[j].Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        linear.PlacedFootprint.Offset=j*bytes;
        l->CopyTextureRegion(&loc[j],0,0,0,&linear,nullptr);
    }
    linear.pResource=readback.Get();
    for (UINT j=0;j<3;j++) {
        D3D12_RESOURCE_BARRIER b={};
        b.Transition={image[j].Get(),0,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COPY_SOURCE};
        l->ResourceBarrier(1,&b);
        linear.PlacedFootprint.Offset=j*bytes; l->CopyTextureRegion(&linear,0,0,0,&loc[j],nullptr);
    }
    HR(l->Close()); ID3D12CommandList *batch[]={l.Get()}; q->ExecuteCommandLists(1,batch);
    HR(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&f))); HR(q->Signal(f.Get(),1));
    HANDLE e=CreateEventW(nullptr,FALSE,FALSE,nullptr); CK(e); HR(f->SetEventOnCompletion(1,e));
    CK(WaitForSingleObject(e,10000)==WAIT_OBJECT_0); CloseHandle(e);
    HR(readback->Map(0,nullptr,(void **)&p));
    for (UINT n=0;n<total;n++) CK(p[n]==value(n));
    readback->Unmap(0,nullptr);
    puts("PASS +c8 R8: native 32768/4096, fallback 65536/65536; rejected bad host sentinels and descriptor controls");
    puts("PASS RADV placement offsets 0x1000/0x9000/0x11000: all 32768 target texels and 65536 guard texels after fence");
}
