#include <d3d12.h>
#include <wrl/client.h>
#include <cstdio>
#include <cstring>
#include <initializer_list>
using Microsoft::WRL::ComPtr;
extern "C" HRESULT WINAPI D3D12XboxCreateDevice(void *,const void *,REFIID,void **);
extern "C" HRESULT xs_color_allocation_info(IUnknown *,UINT,UINT,const void *,D3D12_RESOURCE_ALLOCATION_INFO *);
extern "C" HRESULT xs_msaa_allocation_info1(IUnknown *,UINT,UINT,const void *,D3D12_RESOURCE_ALLOCATION_INFO *,D3D12_RESOURCE_ALLOCATION_INFO1 *);
#define CK(x) do{if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);return 1;}}while(0)
#define HR(x) CK(SUCCEEDED(x))
#ifdef TEST_ATLAS
#include "test-a8-atlas.hpp"
#endif
int main() {
#ifdef TEST_ATLAS
    HMODULE xm=LoadLibraryW(L"xmem.dll");CK(xm);BYTE *firstPool=atlasPool();CK(firstPool);
#endif
    DWORD params[20]={};params[0]=0x20004;params[2]=0x100000;params[10]=1;params[15]=0x20;
    const IID boot={0x5405c344,0xd457,0x444e,{0xb4,0xdd,0x23,0x66,0xe4,0x5a,0xee,0x39}};
    ComPtr<IUnknown> dev;HR(D3D12XboxCreateDevice(nullptr,params,boot,(void**)dev.GetAddressOf()));
    using Query=D3D12_RESOURCE_ALLOCATION_INFO *(WINAPI *)(D3D12_RESOURCE_ALLOCATION_INFO *,IUnknown *,UINT,UINT,const void *);
    auto query=(Query)((*(void***)dev.Get())[0xc8/8]);
#ifdef TEST_BGRA
    constexpr bool bgra=true;
#else
    constexpr bool bgra=false;
#endif
    constexpr UINT width=bgra?2048:3840,height=bgra?2048:2160;
    constexpr DXGI_FORMAT format=bgra?DXGI_FORMAT_B8G8R8A8_UNORM:DXGI_FORMAT_A8_UNORM;
    DWORD wire[]={3,0,0,0,width,0,height,0x10001,format,1,0,0,0x300001};
    auto pages=(BYTE*)VirtualAlloc(nullptr,8192,MEM_RESERVE,PAGE_NOACCESS);CK(pages && VirtualAlloc(pages,4096,MEM_COMMIT,PAGE_READWRITE));
    BYTE *prefix=pages+4096-sizeof wire;memcpy(prefix,wire,sizeof wire);
    struct {UINT64 before;D3D12_RESOURCE_ALLOCATION_INFO info;UINT64 after;} g={1,{},2};
    CK(query(&g.info,dev.Get(),1,1,prefix)==&g.info && g.before==1 && g.after==2);
    D3D12_RESOURCE_DESC rd={};memcpy(&rd,wire,sizeof wire);rd.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    ComPtr<ID3D12Device> d;HR(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&d)));
    auto desktop=d->GetResourceAllocationInfo(1,1,&rd);
    CK(!memcmp(&desktop,&g.info,sizeof desktop));
    using Query1=D3D12_RESOURCE_ALLOCATION_INFO *(WINAPI *)(D3D12_RESOURCE_ALLOCATION_INFO *,IUnknown *,UINT,UINT,const void *,D3D12_RESOURCE_ALLOCATION_INFO1 *);
    auto query1=(Query1)((*(void***)dev.Get())[0x360/8]);struct {UINT64 a;D3D12_RESOURCE_ALLOCATION_INFO1 one;UINT64 b;} extra={1,{},2};
    extra.one.Offset=~0ull;g.info={};
    CK(query1(&g.info,dev.Get(),1,1,prefix,&extra.one)==&g.info && g.before==1 && g.after==2 && extra.a==1 && extra.b==2);
    CK(extra.one.Offset==0 && extra.one.SizeInBytes==desktop.SizeInBytes && extra.one.Alignment==desktop.Alignment);
    printf("format=%u +c8/+360 size=%#llx alignment=%#llx host=%#llx/%#llx\n",format,g.info.SizeInBytes,g.info.Alignment,desktop.SizeInBytes,desktop.Alignment);
    CK(!memcmp(&desktop,&g.info,sizeof desktop) && (bgra ? (g.info.SizeInBytes==0x1000000 || g.info.SizeInBytes==0x1010000) : (g.info.SizeInBytes==0x880000 || g.info.SizeInBytes==0x800000)) && g.info.Alignment==65536);
    auto deny=[&](UINT node,UINT cnt,const void *p){D3D12_RESOURCE_ALLOCATION_INFO bad={1,2};
        return FAILED(xs_color_allocation_info(dev.Get(),node,cnt,p,&bad)) && !bad.SizeInBytes && !bad.Alignment &&
            FAILED(xs_msaa_allocation_info1(dev.Get(),node,cnt,p,&bad,&extra.one));};
    CK(deny(0,1,prefix) && deny(1,2,prefix) && deny(1,1,prefix+1));
    for(UINT k=0;k<13;k++)if(k!=1){wire[k]^=1;CK(deny(1,1,wire));wire[k]^=1;}
    if(!bgra){
        const auto rtInfo=g.info;
        for(UINT side : {1024u,2048u}){
            DWORD atlas[]={3,0,0,0,side,0,side,0x10001,65,1,0,0x100,0x300000};memcpy(prefix,atlas,sizeof atlas);
            CK(query(&g.info,dev.Get(),1,1,prefix)==&g.info && g.info.SizeInBytes==side*side && g.info.Alignment==256 && g.before==1 && g.after==2);
            {extra.one.Offset=~0ull;CK(query1(&g.info,dev.Get(),1,1,prefix,&extra.one)==&g.info && !extra.one.Offset && extra.one.SizeInBytes==side*side && extra.one.Alignment==256 && extra.a==1 && extra.b==2);}
            for(UINT j=0;j<13;j++)if(j!=1){((DWORD*)prefix)[j]^=1;CK(deny(1,1,prefix));((DWORD*)prefix)[j]^=1;}
            CK(deny(0,1,prefix) && deny(1,2,prefix) && deny(1,1,prefix+1));
        }
        puts("PASS A8 linear atlas: guarded aggregate/detail and field negatives");
        g.info=rtInfo;
    }
#ifdef TEST_ATLAS
    for(UINT side : {1024u,2048u}) CK(!checkAtlas(dev.Get(),d.Get(),side,side==1024?firstPool:atlasPool()));
#endif
    CK(VirtualFree(pages,0,MEM_RELEASE));
    // One ordinary desktop placed RT at a nonzero offset, using exactly the reported remaining extent.
    D3D12_HEAP_DESC hd={};hd.SizeInBytes=g.info.Alignment+g.info.SizeInBytes;hd.Properties.Type=D3D12_HEAP_TYPE_DEFAULT;
    hd.Properties.CreationNodeMask=hd.Properties.VisibleNodeMask=1;hd.Flags=D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES;
    ComPtr<ID3D12Heap> heap;HR(d->CreateHeap(&hd,IID_PPV_ARGS(&heap)));
    ComPtr<ID3D12Resource> image,rb;
    HR(d->CreatePlacedResource(heap.Get(),g.info.Alignment,&rd,D3D12_RESOURCE_STATE_RENDER_TARGET,nullptr,IID_PPV_ARGS(&image)));
    D3D12_DESCRIPTOR_HEAP_DESC rtv={D3D12_DESCRIPTOR_HEAP_TYPE_RTV,1,D3D12_DESCRIPTOR_HEAP_FLAG_NONE,1};
    ComPtr<ID3D12DescriptorHeap> descriptors;HR(d->CreateDescriptorHeap(&rtv,IID_PPV_ARGS(&descriptors)));
    auto view=descriptors->GetCPUDescriptorHandleForHeapStart();d->CreateRenderTargetView(image.Get(),nullptr,view);
    D3D12_RESOURCE_DESC buf={};buf.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;buf.Width=256;
    buf.Height=buf.DepthOrArraySize=buf.MipLevels=buf.SampleDesc.Count=1;buf.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    auto hp=hd.Properties;hp.Type=D3D12_HEAP_TYPE_READBACK;HR(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&buf,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&rb)));
    ComPtr<ID3D12CommandQueue> q;ComPtr<ID3D12CommandAllocator> a;ComPtr<ID3D12GraphicsCommandList> l;D3D12_COMMAND_QUEUE_DESC qd={};
    HR(d->CreateCommandQueue(&qd,IID_PPV_ARGS(&q)));HR(d->CreateCommandAllocator(qd.Type,IID_PPV_ARGS(&a)));HR(d->CreateCommandList(0,qd.Type,a.Get(),nullptr,IID_PPV_ARGS(&l)));
    float dark[]={0.25f,0.25f,0.25f,0.25f},bright[]={0.75f,0.75f,0.75f,0.75f};D3D12_RECT corner={width-1,height-1,width,height};
    l->ClearRenderTargetView(view,dark,0,nullptr);l->ClearRenderTargetView(view,bright,1,&corner);
    D3D12_RESOURCE_BARRIER b={};b.Transition={image.Get(),0,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE};l->ResourceBarrier(1,&b);
    D3D12_TEXTURE_COPY_LOCATION src={},dst={};src.pResource=image.Get();dst.pResource=rb.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint.Footprint={format,2,1,1,256};D3D12_BOX box={width-2,height-1,0,width,height,1};
    l->CopyTextureRegion(&dst,0,0,0,&src,&box);HR(l->Close());ID3D12CommandList *lists[]={l.Get()};q->ExecuteCommandLists(1,lists);
    ComPtr<ID3D12Fence> fence;HR(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));HR(q->Signal(fence.Get(),1));
    HANDLE e=CreateEventW(nullptr,FALSE,FALSE,nullptr);CK(e);HR(fence->SetEventOnCompletion(1,e));CK(WaitForSingleObject(e,10000)==WAIT_OBJECT_0);CloseHandle(e);
    BYTE *p;HR(rb->Map(0,nullptr,(void**)&p));CK(bgra ? (((DWORD*)p)[0]==0x40404040 && ((DWORD*)p)[1]==0xbfbfbfbf) : (p[0]==64 && p[1]==191));rb->Unmap(0,nullptr);
    printf("PASS %s allocation query: guarded wire/output, native placement and fenced two-texel readback\n",bgra?"BGRA8":"A8");
}
