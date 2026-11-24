/* Query and ordinary host placements; no guest tiling assumed. */
#include <windows.h>
#include <d3d12.h>
#include <wrl/client.h>
#include <cstdio>
using Microsoft::WRL::ComPtr;
extern "C" HRESULT WINAPI D3D12XboxCreateDevice(void *,const void *,REFIID,void **);
extern "C" HRESULT xs_color_allocation_info(IUnknown *,UINT,UINT,const void *,D3D12_RESOURCE_ALLOCATION_INFO *);
#define CK(x) do { if (!(x)) {printf("FAIL %d: %s\n",__LINE__,#x);return 1;} } while(0)
#define HR(x) CK(SUCCEEDED(x))
static int check(IUnknown *dev,UINT w,UINT h,UINT flags)
{
    ComPtr<ID3D12Device> d;HR(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&d)));
    D3D12_RESOURCE_DESC r={};r.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;r.Width=w;r.Height=h;
    r.DepthOrArraySize=r.MipLevels=r.SampleDesc.Count=1;r.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;
    r.Flags=(D3D12_RESOURCE_FLAGS)flags;
    auto page=(BYTE*)VirtualAlloc(nullptr,8192,MEM_RESERVE,PAGE_NOACCESS);CK(page && VirtualAlloc(page,4096,MEM_COMMIT,PAGE_READWRITE));
    constexpr SIZE_T n=offsetof(D3D12_RESOURCE_DESC,Flags)+sizeof r.Flags;
    auto wire=page+4096-n;memcpy(wire,&r,n);
    using Query=D3D12_RESOURCE_ALLOCATION_INFO *(WINAPI*)(D3D12_RESOURCE_ALLOCATION_INFO*,IUnknown*,UINT,UINT,const void*);
    auto query=(Query)((*(void***)dev)[0xc8/8]);struct {UINT64 a;D3D12_RESOURCE_ALLOCATION_INFO i;UINT64 b;} result={1,{},2};
    CK(query(&result.i,dev,1,1,wire)==&result.i && result.a==1 && result.b==2);
    if(w==2560){
        using Q1=D3D12_RESOURCE_ALLOCATION_INFO *(WINAPI*)(D3D12_RESOURCE_ALLOCATION_INFO*,IUnknown*,UINT,UINT,const void*,D3D12_RESOURCE_ALLOCATION_INFO1*);
        auto query1=(Q1)((*(void***)dev)[0x360/8]);D3D12_RESOURCE_ALLOCATION_INFO1 one={};D3D12_RESOURCE_ALLOCATION_INFO again={};
        CK(query1(&again,dev,1,1,wire,&one)==&again && !memcmp(&again,&result.i,sizeof again) && !one.Offset &&
            one.Alignment==again.Alignment && one.SizeInBytes==again.SizeInBytes);
    }
    for(UINT k=0;k<n/4;k++)if(k!=1){((DWORD*)wire)[k]^=1;D3D12_RESOURCE_ALLOCATION_INFO bad={1,2};
        CK(FAILED(xs_color_allocation_info(dev,1,1,wire,&bad)) && !bad.Alignment && !bad.SizeInBytes);((DWORD*)wire)[k]^=1;}
    D3D12_RESOURCE_ALLOCATION_INFO bad={};
    CK(FAILED(xs_color_allocation_info(dev,1,1,wire+1,&bad)) && FAILED(xs_color_allocation_info(dev,1,2,wire,&bad)) &&
        FAILED(xs_color_allocation_info(dev,0,1,wire,&bad)) && VirtualFree(page,0,MEM_RELEASE));
    r.Flags=w==1280?D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET:D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET|D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    auto info=d->GetResourceAllocationInfo(1,1,&r);
    CK(!memcmp(&info,&result.i,sizeof info));
    CK(info.Alignment==65536 && info.SizeInBytes>=UINT64(w)*h*8 && info.SizeInBytes<32*1024*1024 && !(info.SizeInBytes%info.Alignment));
    D3D12_HEAP_DESC hd={};hd.SizeInBytes=info.SizeInBytes*2;hd.Alignment=info.Alignment;
    hd.Properties.Type=D3D12_HEAP_TYPE_DEFAULT;hd.Properties.CreationNodeMask=hd.Properties.VisibleNodeMask=1;
    hd.Flags=D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES;
    ComPtr<ID3D12Heap> heap;HR(d->CreateHeap(&hd,IID_PPV_ARGS(&heap)));
    ComPtr<ID3D12Resource> tex[2],rb;
    D3D12_DESCRIPTOR_HEAP_DESC dd={D3D12_DESCRIPTOR_HEAP_TYPE_RTV,2,D3D12_DESCRIPTOR_HEAP_FLAG_NONE,1};
    ComPtr<ID3D12DescriptorHeap> views;HR(d->CreateDescriptorHeap(&dd,IID_PPV_ARGS(&views)));
    D3D12_CPU_DESCRIPTOR_HANDLE view[2];view[0]=view[1]=views->GetCPUDescriptorHandleForHeapStart();
    view[1].ptr+=d->GetDescriptorHandleIncrementSize(dd.Type);
    for(UINT i=0;i<2;i++){
        HR(d->CreatePlacedResource(heap.Get(),i*info.SizeInBytes,&r,D3D12_RESOURCE_STATE_RENDER_TARGET,nullptr,IID_PPV_ARGS(&tex[i])));
        d->CreateRenderTargetView(tex[i].Get(),nullptr,view[i]);
    }
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp;UINT rows;UINT64 bytes,total;
    d->GetCopyableFootprints(&r,0,1,0,&fp,&rows,&bytes,&total);
    CK(rows==h && bytes==w*8 && fp.Footprint.RowPitch>=bytes);
    UINT64 stride=(total+511)&~511ull;
    D3D12_RESOURCE_DESC b={};b.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;b.Width=stride*2;
    b.Height=b.DepthOrArraySize=b.MipLevels=b.SampleDesc.Count=1;b.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    D3D12_HEAP_PROPERTIES hp={};hp.Type=D3D12_HEAP_TYPE_READBACK;hp.CreationNodeMask=hp.VisibleNodeMask=1;
    HR(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&b,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&rb)));
    D3D12_COMMAND_QUEUE_DESC qd={};ComPtr<ID3D12CommandQueue> q;HR(d->CreateCommandQueue(&qd,IID_PPV_ARGS(&q)));
    ComPtr<ID3D12CommandAllocator> a;HR(d->CreateCommandAllocator(qd.Type,IID_PPV_ARGS(&a)));
    ComPtr<ID3D12GraphicsCommandList> l;HR(d->CreateCommandList(0,qd.Type,a.Get(),nullptr,IID_PPV_ARGS(&l)));
    const float colors[2][4]={{1,0,-1,0.5f},{-1,0.5f,1,0}};
    for(UINT i=0;i<2;i++)l->ClearRenderTargetView(view[i],colors[i],0,nullptr);
    RECT stripe={13,3,31,718};l->ClearRenderTargetView(view[1],colors[0],1,&stripe);
    for(UINT i=0;i<2;i++){
        D3D12_RESOURCE_BARRIER br={};br.Transition={tex[i].Get(),0,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE};
        l->ResourceBarrier(1,&br);
        D3D12_TEXTURE_COPY_LOCATION src={},dst={};src.pResource=tex[i].Get();dst.pResource=rb.Get();
        dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=fp;dst.PlacedFootprint.Offset=i*stride;
        l->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
    }
    HR(l->Close());ID3D12CommandList *cmd[]={l.Get()};q->ExecuteCommandLists(1,cmd);
    ComPtr<ID3D12Fence> f;HR(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&f)));HR(q->Signal(f.Get(),1));
    HANDLE e=CreateEventW(nullptr,FALSE,FALSE,nullptr);CK(e);HR(f->SetEventOnCompletion(1,e));CK(WaitForSingleObject(e,10000)==WAIT_OBJECT_0);
    CloseHandle(e);HR(d->GetDeviceRemovedReason());
    BYTE *p;HR(rb->Map(0,nullptr,(void**)&p));constexpr UINT64 bits[]={0x3800bc0000003c00ull,0x00003c003800bc00ull};
    for(UINT i=0;i<2;i++)for(UINT y=0;y<h;y++)for(UINT x=0;x<w;x++)
        CK(*(UINT64*)(p+i*stride+y*fp.Footprint.RowPitch+x*8)==bits[i && !(x>=13 && x<31 && y>=3 && y<718)]);
    rb->Unmap(0,nullptr);
    printf("PASS %ux%u RGBA16_FLOAT guest flags=%#x host flags=%#x +c8 size=%#llx alignment=%#llx copy pitch=%u two placements all pixels verified\n",
        w,h,flags,r.Flags,info.SizeInBytes,info.Alignment,fp.Footprint.RowPitch);return 0;
}
int main()
{
    DWORD params[20]={};params[0]=0x20004;params[2]=0x100000;params[10]=1;params[15]=0x20;
    const IID boot={0x5405c344,0xd457,0x444e,{0xb4,0xdd,0x23,0x66,0xe4,0x5a,0xee,0x39}};
    ComPtr<IUnknown> dev;HR(D3D12XboxCreateDevice(nullptr,params,boot,(void**)dev.GetAddressOf()));
    return check(dev.Get(),1280,720,0xc00001) || check(dev.Get(),2560,1440,0x300005);
}
