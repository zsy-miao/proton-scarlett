#include <d3d12.h>
#include <cstdio>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
extern "C" HRESULT WINAPI D3D12XboxCreateDevice(void *,const void *,REFIID,void **);
extern "C" HRESULT xs_color_allocation_info(IUnknown *,UINT,UINT,const void *,D3D12_RESOURCE_ALLOCATION_INFO *);
#define CK(x) do { if (!(x)) { std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); return 1; } } while(0)
#define HR(x) CK(SUCCEEDED(x))

int main()
{
    DWORD params[20]={}; params[0]=0x20004;params[2]=0x100000;params[10]=1;params[15]=0x20;
    const IID boot={0x5405c344,0xd457,0x444e,{0xb4,0xdd,0x23,0x66,0xe4,0x5a,0xee,0x39}};
    ComPtr<IUnknown> dev; HR(D3D12XboxCreateDevice(nullptr,params,boot,(void **)dev.GetAddressOf()));
    using Query=D3D12_RESOURCE_ALLOCATION_INFO *(WINAPI *)(D3D12_RESOURCE_ALLOCATION_INFO *,IUnknown *,UINT,UINT,const void *);
    auto query=(Query)((*reinterpret_cast<void***>(dev.Get()))[0xc8/8]);
    DWORD wire[]={3,0,0,0,0xf00,0,0x870,0x10001,0x1c,1,0,0,0x300005};
    // Only the 52-byte caller-written prefix is readable.
    SYSTEM_INFO sys; GetSystemInfo(&sys);
    auto mem=(BYTE *)VirtualAlloc(nullptr,2*sys.dwPageSize,MEM_RESERVE,PAGE_NOACCESS);
    CK(mem && VirtualAlloc(mem,sys.dwPageSize,MEM_COMMIT,PAGE_READWRITE)==mem);
    void *prefix=mem+sys.dwPageSize-sizeof wire;
    memcpy(prefix,wire,sizeof wire);
    struct { UINT64 before; D3D12_RESOURCE_ALLOCATION_INFO info; UINT64 after; } got={7,{},8};
    CK(query(&got.info,dev.Get(),1,1,prefix)==&got.info && got.before==7 && got.after==8);
    CK(VirtualFree(mem,0,MEM_RELEASE));
    auto denied=[&](UINT mask,UINT count,const void *ptr) {
        D3D12_RESOURCE_ALLOCATION_INFO bad={1,2};
        return FAILED(xs_color_allocation_info(dev.Get(),mask,count,ptr,&bad)) && !bad.SizeInBytes && !bad.Alignment;
    };
    CK(denied(0,1,wire) && denied(1,2,wire) && denied(1,1,nullptr));
    wire[12]^=0x100000; CK(denied(1,1,wire)); wire[12]^=0x100000;
    wire[2]=4096; CK(denied(1,1,wire)); wire[2]=0;

    ComPtr<ID3D12Device> d; HR(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&d)));
    D3D12_RESOURCE_DESC tex={}; tex.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    tex.Width=3840; tex.Height=2160; tex.DepthOrArraySize=tex.MipLevels=tex.SampleDesc.Count=1;
    tex.Format=DXGI_FORMAT_R8G8B8A8_UNORM; tex.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET|D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    auto ref=d->GetResourceAllocationInfo(1,1,&tex);
    CK(ref.SizeInBytes==got.info.SizeInBytes && ref.Alignment==got.info.Alignment);
    D3D12_HEAP_DESC hd={}; hd.SizeInBytes=got.info.SizeInBytes; hd.Alignment=got.info.Alignment;
    hd.Properties.Type=D3D12_HEAP_TYPE_DEFAULT; hd.Properties.CreationNodeMask=hd.Properties.VisibleNodeMask=1;
    hd.Flags=D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES;
    ComPtr<ID3D12Heap> h; ComPtr<ID3D12Resource> rt,rb;
    HR(d->CreateHeap(&hd,IID_PPV_ARGS(&h)));
    HR(d->CreatePlacedResource(h.Get(),0,&tex,D3D12_RESOURCE_STATE_RENDER_TARGET,nullptr,IID_PPV_ARGS(&rt)));
    D3D12_DESCRIPTOR_HEAP_DESC vd={D3D12_DESCRIPTOR_HEAP_TYPE_RTV,1,D3D12_DESCRIPTOR_HEAP_FLAG_NONE,1};
    ComPtr<ID3D12DescriptorHeap> views; HR(d->CreateDescriptorHeap(&vd,IID_PPV_ARGS(&views)));
    auto rtv=views->GetCPUDescriptorHandleForHeapStart(); d->CreateRenderTargetView(rt.Get(),nullptr,rtv);
    D3D12_RESOURCE_DESC buf={}; buf.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER; buf.Width=512;
    buf.Height=buf.DepthOrArraySize=buf.MipLevels=buf.SampleDesc.Count=1; buf.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    auto hp=hd.Properties; hp.Type=D3D12_HEAP_TYPE_READBACK;
    HR(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&buf,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&rb)));
    D3D12_COMMAND_QUEUE_DESC qd={}; ComPtr<ID3D12CommandQueue> q; ComPtr<ID3D12CommandAllocator> a;
    ComPtr<ID3D12GraphicsCommandList> l; ComPtr<ID3D12Fence> f;
    HR(d->CreateCommandQueue(&qd,IID_PPV_ARGS(&q))); HR(d->CreateCommandAllocator(qd.Type,IID_PPV_ARGS(&a)));
    HR(d->CreateCommandList(0,qd.Type,a.Get(),nullptr,IID_PPV_ARGS(&l)));
    FLOAT red[]={1,0,0,1}; l->ClearRenderTargetView(rtv,red,0,nullptr);
    D3D12_RESOURCE_BARRIER b={}; b.Transition={rt.Get(),0,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE};
    l->ResourceBarrier(1,&b);
    D3D12_TEXTURE_COPY_LOCATION src={},dst={}; src.pResource=rt.Get(); src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.pResource=rb.Get(); dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint.Footprint={tex.Format,1,1,1,256};
    D3D12_BOX last={3839,2159,0,3840,2160,1}; l->CopyTextureRegion(&dst,0,0,0,&src,&last);
    HR(l->Close()); ID3D12CommandList *batch[]={l.Get()}; q->ExecuteCommandLists(1,batch);
    HR(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&f))); HR(q->Signal(f.Get(),1));
    HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr); CK(event);
    HR(f->SetEventOnCompletion(1,event)); CK(WaitForSingleObject(event,10000)==WAIT_OBJECT_0); CloseHandle(event);
    DWORD *pixel; HR(rb->Map(0,nullptr,(void **)&pixel)); CK(*pixel==0xff0000ff); rb->Unmap(0,nullptr);
    std::printf("PASS device +c8 sret: size=%llu alignment=%llu; exact heap/placed RGBA8 RT+UAV; fenced bottom-right pixel ff0000ff\n",
            (unsigned long long)ref.SizeInBytes,(unsigned long long)ref.Alignment);
}
