#include <d3d12.h>
#include <wrl/client.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
using Microsoft::WRL::ComPtr;
extern "C" HRESULT WINAPI D3D12XboxCreateDevice(void*,const void*,REFIID,void**);
extern "C" HRESULT xs_color_allocation_info(IUnknown*,UINT,UINT,const void*,D3D12_RESOURCE_ALLOCATION_INFO*);
extern "C" HRESULT xs_msaa_allocation_info1(IUnknown*,UINT,UINT,const void*,D3D12_RESOURCE_ALLOCATION_INFO*,D3D12_RESOURCE_ALLOCATION_INFO1*);
#define CK(x) do{if(!(x)){fprintf(stderr,"FAIL %d %s\n",__LINE__,#x);exit(1);}}while(0)
#define HR(x) CK(SUCCEEDED(x))
int main(){
    DWORD params[20]={};params[0]=0x20004;params[2]=0x100000;params[10]=1;params[15]=0x20;
    IID boot={0x5405c344,0xd457,0x444e,{0xb4,0xdd,0x23,0x66,0xe4,0x5a,0xee,0x39}};
    ComPtr<IUnknown> guest;HR(D3D12XboxCreateDevice(nullptr,params,boot,(void**)guest.GetAddressOf()));
    ComPtr<ID3D12Device> d;HR(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&d)));
    using Query=D3D12_RESOURCE_ALLOCATION_INFO*(WINAPI*)(D3D12_RESOURCE_ALLOCATION_INFO*,IUnknown*,UINT,UINT,const void*);
    auto query=(Query)((*(void***)guest.Get())[0xc8/8]);
    DWORD wire[]={3,0,0,0,994,0,624,0x10001,87,1,0,0,0};
    BYTE *pages=(BYTE*)VirtualAlloc(nullptr,8192,MEM_RESERVE,PAGE_NOACCESS);CK(pages && VirtualAlloc(pages,4096,MEM_COMMIT,PAGE_READWRITE));BYTE *tail=pages+4096-sizeof wire;
    D3D12_HEAP_DESC hd={};hd.Properties.Type=D3D12_HEAP_TYPE_DEFAULT;hd.Properties.VisibleNodeMask=hd.Properties.CreationNodeMask=1;hd.Flags=D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES;
    ComPtr<ID3D12Heap> heap;ComPtr<ID3D12Resource> images[3];UINT flags[]={0xc00005,0x300001,0xc00001};
    D3D12_RESOURCE_DESC rd={};UINT64 slot=0;
    for(UINT i=0;i<3;i++){
        wire[12]=flags[i];memcpy(tail,wire,sizeof wire);memcpy(&rd,wire,sizeof wire);rd.Flags=(D3D12_RESOURCE_FLAGS)(flags[i]&7);
        auto native=d->GetResourceAllocationInfo(1,1,&rd);struct{UINT64 pre;D3D12_RESOURCE_ALLOCATION_INFO info;UINT64 post;} g={11,{},13};
        CK(query(&g.info,guest.Get(),1,1,tail)==&g.info && g.pre==11 && g.post==13 && !memcmp(&g.info,&native,sizeof native));
        CK(native.Alignment==65536 && (native.SizeInBytes==0x280000 || native.SizeInBytes==0x290000));
        auto deny=[&](UINT node,UINT count,const void* p){D3D12_RESOURCE_ALLOCATION_INFO z={1,2};return FAILED(xs_color_allocation_info(guest.Get(),node,count,p,&z)) && !z.Alignment && !z.SizeInBytes;};
        CK(deny(0,1,tail) && deny(1,2,tail) && deny(1,1,tail+1));
        for(UINT j=0;j<13;j++)if(j!=1){wire[j]^=1;CK(deny(1,1,wire));wire[j]^=1;}
        D3D12_RESOURCE_ALLOCATION_INFO1 one={};CK(xs_msaa_allocation_info1(guest.Get(),1,1,tail,&g.info,&one)==E_NOTIMPL);
        if(!i){slot=native.SizeInBytes;hd.SizeInBytes=65536+3*slot;HR(d->CreateHeap(&hd,IID_PPV_ARGS(&heap)));} CK(native.SizeInBytes==slot);
        HR(d->CreatePlacedResource(heap.Get(),65536+i*slot,&rd,D3D12_RESOURCE_STATE_RENDER_TARGET,nullptr,IID_PPV_ARGS(&images[i])));
        printf("BGRA994 guest=%#x host=%#x size=%#llx align=%#llx desktopOffset=%#llx\n",flags[i],rd.Flags,slot,native.Alignment,65536+i*slot);
    }
    CK(VirtualFree(pages,0,MEM_RELEASE));
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp;UINT rows;UINT64 rowBytes,total;d->GetCopyableFootprints(&rd,0,1,0,&fp,&rows,&rowBytes,&total);
    CK(rows==624 && rowBytes==3976 && fp.Footprint.RowPitch==4096);UINT64 stride=(total+511)&~511ull;
    D3D12_RESOURCE_DESC buf={};buf.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;buf.Width=stride*3;buf.Height=buf.DepthOrArraySize=buf.MipLevels=buf.SampleDesc.Count=1;buf.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    D3D12_HEAP_PROPERTIES hp=hd.Properties;hp.Type=D3D12_HEAP_TYPE_READBACK;ComPtr<ID3D12Resource> rb;HR(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&buf,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&rb)));
    ComPtr<ID3D12CommandQueue> q;ComPtr<ID3D12CommandAllocator> a;ComPtr<ID3D12GraphicsCommandList> l;D3D12_COMMAND_QUEUE_DESC qd={};
    HR(d->CreateCommandQueue(&qd,IID_PPV_ARGS(&q)));HR(d->CreateCommandAllocator(qd.Type,IID_PPV_ARGS(&a)));HR(d->CreateCommandList(0,qd.Type,a.Get(),nullptr,IID_PPV_ARGS(&l)));
    D3D12_DESCRIPTOR_HEAP_DESC dd={D3D12_DESCRIPTOR_HEAP_TYPE_RTV,3,D3D12_DESCRIPTOR_HEAP_FLAG_NONE,1};ComPtr<ID3D12DescriptorHeap> rtv;HR(d->CreateDescriptorHeap(&dd,IID_PPV_ARGS(&rtv)));auto h=rtv->GetCPUDescriptorHandleForHeapStart();
    const float colors[][4]={{1,0,0,.25f},{0,1,0,1},{0,0,1,1},{0,1,1,.75f}};const DWORD values[]={0x40ff0000,0xff00ff00,0xff0000ff,0xbf00ffff};
    for(UINT i=0;i<3;i++) {d->CreateRenderTargetView(images[i].Get(),nullptr,h);l->ClearRenderTargetView(h,colors[i],0,nullptr);if(i==0){D3D12_RECT r={991,622,994,624};l->ClearRenderTargetView(h,colors[3],1,&r);}h.ptr+=d->GetDescriptorHandleIncrementSize(dd.Type);}
    D3D12_FEATURE_DATA_FORMAT_SUPPORT fs={DXGI_FORMAT_B8G8R8A8_UNORM,{},{}};
    HR(d->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT,&fs,sizeof fs));
    bool typed=(fs.Support1&D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW) && (fs.Support2&D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE);
    printf("BGRA994 desktop typed UAV supported=%d\n",typed);
    ComPtr<ID3D12DescriptorHeap> cpuUav,visibleUav;
    if (typed) {
        dd={D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,1,D3D12_DESCRIPTOR_HEAP_FLAG_NONE,1};HR(d->CreateDescriptorHeap(&dd,IID_PPV_ARGS(&cpuUav)));
        dd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;HR(d->CreateDescriptorHeap(&dd,IID_PPV_ARGS(&visibleUav)));
        auto cpu=cpuUav->GetCPUDescriptorHandleForHeapStart();d->CreateUnorderedAccessView(images[0].Get(),nullptr,nullptr,cpu);
        d->CopyDescriptorsSimple(1,visibleUav->GetCPUDescriptorHandleForHeapStart(),cpu,dd.Type);
        D3D12_RESOURCE_BARRIER b={};b.Transition={images[0].Get(),0,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_UNORDERED_ACCESS};l->ResourceBarrier(1,&b);
        ID3D12DescriptorHeap *bank=visibleUav.Get();l->SetDescriptorHeaps(1,&bank);D3D12_RECT r={0,0,2,2};
        l->ClearUnorderedAccessViewFloat(bank->GetGPUDescriptorHandleForHeapStart(),cpu,images[0].Get(),colors[2],1,&r);
        b.Transition={images[0].Get(),0,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_RENDER_TARGET};l->ResourceBarrier(1,&b);
    }
    for(UINT i=0;i<3;i++){D3D12_RESOURCE_BARRIER b={};b.Transition={images[i].Get(),0,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE};l->ResourceBarrier(1,&b);
        D3D12_TEXTURE_COPY_LOCATION src={},dst={};src.pResource=images[i].Get();dst.pResource=rb.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=fp;dst.PlacedFootprint.Offset=i*stride;l->CopyTextureRegion(&dst,0,0,0,&src,nullptr);}
    HR(l->Close());ID3D12CommandList *lists[]={l.Get()};q->ExecuteCommandLists(1,lists);ComPtr<ID3D12Fence> f;HR(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&f)));HR(q->Signal(f.Get(),1));HANDLE ev=CreateEventW(nullptr,FALSE,FALSE,nullptr);HR(f->SetEventOnCompletion(1,ev));CK(WaitForSingleObject(ev,10000)==WAIT_OBJECT_0);CloseHandle(ev);
    BYTE *p;HR(rb->Map(0,nullptr,(void**)&p));for(UINT i=0;i<3;i++)for(UINT y=0;y<624;y++)for(UINT x=0;x<994;x++){DWORD got=((DWORD*)(p+i*stride+y*4096))[x],want=values[(!i && x>=991 && y>=622)?3:(!i && typed && x<2 && y<2)?2:i];if(got!=want){fprintf(stderr,"FAIL image=%u (%u,%u) got=%08lx expected=%08lx pitch=%u stride=%llx\n",i,x,y,got,want,fp.Footprint.RowPitch,stride);return 1;}}rb->Unmap(0,nullptr);
    puts("PASS BGRA994: three guarded sizing records, adjacent host placements, BGRA full-image and corner readback");
}
