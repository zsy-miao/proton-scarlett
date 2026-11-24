#include <dxgi1_4.h>
#include <d3d12.h>
#include <wrl/client.h>
#include <cstdio>
#include <cstdlib>
#include "xg/r11_mips.h"
#include <cstring>
using Microsoft::WRL::ComPtr;
extern "C" HRESULT WINAPI D3D12XboxCreateDevice(void*,const void*,REFIID,void**);
extern "C" HRESULT xs_color_allocation_info(IUnknown*,UINT,UINT,const void*,D3D12_RESOURCE_ALLOCATION_INFO*);
extern "C" HRESULT xs_msaa_allocation_info1(IUnknown*,UINT,UINT,const void*,D3D12_RESOURCE_ALLOCATION_INFO*,D3D12_RESOURCE_ALLOCATION_INFO1*);
#define CK(x) do { if (!(x)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); exit(1); } } while(0)
#define HR(x) CK(SUCCEEDED(x))

int main(){
    DWORD params[20]={};params[0]=0x20004;params[2]=0x100000;params[10]=1;params[15]=0x20;
    IID boot={0x5405c344,0xd457,0x444e,{0xb4,0xdd,0x23,0x66,0xe4,0x5a,0xee,0x39}};
    ComPtr<IUnknown> guest;HR(D3D12XboxCreateDevice(nullptr,params,boot,(void**)guest.GetAddressOf()));
    using Query=D3D12_RESOURCE_ALLOCATION_INFO*(WINAPI*)(D3D12_RESOURCE_ALLOCATION_INFO*,IUnknown*,UINT,UINT,const void*);
    auto query=(Query)((*(void***)guest.Get())[0xc8/8]);
    ComPtr<ID3D12Device> d;HR(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&d)));
    BYTE *page=(BYTE*)VirtualAlloc(nullptr,8192,MEM_RESERVE,PAGE_NOACCESS);CK(page && VirtualAlloc(page,4096,MEM_COMMIT,PAGE_READWRITE));
    DWORD *wire=(DWORD*)(page+4096)-13;
    const UINT requests[]={4096,0};
    for(UINT request : requests){
        const DWORD v[]={3,0,request,0,160,0,90,0x50001,54,1,0,0x102,0x300004}; memcpy(wire,v,sizeof v);
        struct { UINT64 pre; D3D12_RESOURCE_ALLOCATION_INFO out; UINT64 post; } g={13,{},17};
        CK(query(&g.out,guest.Get(),1,1,wire)==&g.out && g.pre==13 && g.post==17 &&
            g.out.SizeInBytes==0xa400 && g.out.Alignment==256);
        D3D12_RESOURCE_ALLOCATION_INFO1 detail={};
        CK(FAILED(xs_msaa_allocation_info1(guest.Get(),1,1,wire,&g.out,&detail)));
        auto deny=[&](UINT node,UINT n,const void *p){D3D12_RESOURCE_ALLOCATION_INFO z={1,2};
            return FAILED(xs_color_allocation_info(guest.Get(),node,n,p,&z)) && !z.SizeInBytes && !z.Alignment;};
        CK(deny(0,1,wire) && deny(1,2,wire) && deny(1,1,(BYTE*)wire+1));
        for(UINT j=0;j<13;j++)if(j!=1){wire[j]^=1;CK(deny(1,1,wire));wire[j]^=1;}
    }
    puts("PASS R16 five-mip: guarded +c8 small-alignment probe and retry, rejected neighbors");
    for(UINT levels=5;levels<=6;levels++){
      UINT top=6-levels,w=2560u>>top,h=1440u>>top,bytes=levels==5?0x4b1400:0x12c1400;
      const DWORD v[]={3,0,0,0,w,0,h,(levels<<16)|1u,26,1,0,0x102,0x300004};memcpy(wire,v,sizeof v);
      D3D12_RESOURCE_ALLOCATION_INFO z={};D3D12_RESOURCE_ALLOCATION_INFO1 detail={};
      CK(query(&z,guest.Get(),1,1,wire)==&z && z.SizeInBytes==bytes && z.Alignment==256);
      if(levels==5){
          using Query1=D3D12_RESOURCE_ALLOCATION_INFO*(WINAPI*)(D3D12_RESOURCE_ALLOCATION_INFO*,IUnknown*,UINT,UINT,const void*,D3D12_RESOURCE_ALLOCATION_INFO1*);
          auto query1=(Query1)((*(void***)guest.Get())[0x360/8]);
          detail.Offset=~0ull;z={};CK(query1(&z,guest.Get(),1,1,wire,&detail)==&z && z.SizeInBytes==bytes && z.Alignment==256 &&
              !detail.Offset && detail.Alignment==z.Alignment && detail.SizeInBytes==z.SizeInBytes);
      }else CK(FAILED(xs_msaa_allocation_info1(guest.Get(),1,1,wire,&z,&detail)));
      for(UINT i=0;i<13;i++)if(i!=1){wire[i]^=1;CK(FAILED(xs_color_allocation_info(guest.Get(),1,1,wire,&z)));CK(FAILED(xs_msaa_allocation_info1(guest.Get(),1,1,wire,&z,&detail)));wire[i]^=1;}
      CK(FAILED(xs_color_allocation_info(guest.Get(),1,1,(BYTE*)wire+1,&z)));
      wire[4]=640;wire[6]=360;wire[7]=0x40001;CK(FAILED(xs_color_allocation_info(guest.Get(),1,1,wire,&z)));
      // Fingerprints of every byte address from the independent texture-computer coordinate query.
      const UINT64 golden[]={0xff29bbd9171a2325ull,0xcfdba89f01b06325ull,0x35707ab1449e5325ull,
                             0xb503d8c674f75725ull,0x2afadd7bc24a6e25ull,0x24bb1786cdeacda5ull};
      auto seen=(BYTE*)calloc(bytes/4,1);CK(seen);UINT count=0;
      for(UINT m=0;m<levels;m++){UINT64 hash=14695981039346656037ull;
          for(UINT y=0;y<(h>>m);y++)for(UINT x=0;x<(w>>m);x++){
              UINT b=r11_mip_byte(levels,m,x,y);CK(!(b%4) && b<bytes && !seen[b/4]++);count++;
              hash=(hash^b)*1099511628211ull;
          }
          CK(hash==golden[top+m] && r11_mip_byte(levels,m,w>>m,0)==bytes && r11_mip_byte(levels,m,0,h>>m)==bytes);
      }
      free(seen);CK(count==(levels==5?1227600u:4914000u) && bytes-count*4==9920 && r11_mip_byte(levels,levels,0,0)==bytes);
      printf("PASS R11 %u-mip: %u addresses, 9920 padding bytes, guarded sizing and rejected neighbors\n",levels,count);
    }
    struct Target { UINT w,h,fmt,flags; } targets[]={
        {3840,2160,87,0xc00005}, {3840,2160,87,0x300001},
        {3840,2160,87,0x300001}, {3840,2160,19,0x800002}, {3840,2160,87,0xc00001},
        {2560,1440,19,0x800002}, {640,1440,42,0x300005}, {2560,1440,41,0xc00001}
    };
    constexpr UINT count=ARRAYSIZE(targets);
    D3D12_RESOURCE_DESC rd[count]={}; UINT64 offsets[count],end=65536;
    for(UINT i=0;i<count;i++){
        auto t=targets[i]; DWORD v[]={3,0,0,0,t.w,0,t.h,0x10001,t.fmt,1,0,0,t.flags};
        memcpy(wire,v,sizeof v); memcpy(&rd[i],v,sizeof v);rd[i].Flags=(D3D12_RESOURCE_FLAGS)(t.flags&7);
        auto host=d->GetResourceAllocationInfo(1,1,rd+i);
        struct { UINT64 pre;D3D12_RESOURCE_ALLOCATION_INFO info;UINT64 post; } g={13,{},17};
        CK(host.Alignment==65536 && host.SizeInBytes && host.SizeInBytes<=(64u<<20) && !(host.SizeInBytes%65536));
        CK(query(&g.info,guest.Get(),1,1,wire)==&g.info && g.pre==13 && g.post==17 && !memcmp(&host,&g.info,sizeof host));
        auto deny=[&](UINT node,UINT n,const void *p){D3D12_RESOURCE_ALLOCATION_INFO z={1,2};
            return FAILED(xs_color_allocation_info(guest.Get(),node,n,p,&z)) && !z.SizeInBytes && !z.Alignment;};
        CK(deny(0,1,wire) && deny(1,2,wire) && deny(1,1,(BYTE*)wire+1));
        for(UINT j=0;j<13;j++)if(j!=1){wire[j]^=1; CK(deny(1,1,wire)); wire[j]^=1;}
        D3D12_RESOURCE_ALLOCATION_INFO1 detail={};
        CK(xs_msaa_allocation_info1(guest.Get(),1,1,wire,&g.info,&detail)==E_NOTIMPL);
        offsets[i]=end;end+=host.SizeInBytes;
        printf("RECORD %u %ux%u fmt=%u flags=%#x size=%#llx align=%#llx\n",i,t.w,t.h,t.fmt,t.flags,host.SizeInBytes,host.Alignment);
    }
    { DWORD v[]={3,0,0,0,2560,0,1440,0x10001,86,1,0,0,0x300005};memcpy(wire,v,sizeof v);
      D3D12_RESOURCE_ALLOCATION_INFO z={};D3D12_RESOURCE_ALLOCATION_INFO1 detail={};
      ComPtr<IDXGIFactory4> factory;ComPtr<IDXGIAdapter1> adapter;DXGI_ADAPTER_DESC1 id={};
      HR(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));HR(factory->EnumAdapterByLuid(d->GetAdapterLuid(),IID_PPV_ARGS(&adapter)));HR(adapter->GetDesc1(&id));
      UINT expected=id.VendorId==0x10de && id.DeviceId==0x2484?0x780000:id.VendorId==0x1002 && id.DeviceId==0x164e?0x790000:0;
      if (expected) {
          struct {UINT64 before;D3D12_RESOURCE_ALLOCATION_INFO out;UINT64 after;} guarded={13,{},17};
          CK(query(&guarded.out,guest.Get(),1,1,wire)==&guarded.out && guarded.before==13 && guarded.after==17 && guarded.out.Alignment==65536 && guarded.out.SizeInBytes==expected);
          CK(FAILED(xs_color_allocation_info(guest.Get(),0,1,wire,&z)) && FAILED(xs_color_allocation_info(guest.Get(),1,2,wire,&z)));
          for(UINT j=0;j<13;j++)if(j!=1){wire[j]^=1;CK(FAILED(xs_color_allocation_info(guest.Get(),1,1,wire,&z)));wire[j]^=1;}
          CK(FAILED(xs_color_allocation_info(guest.Get(),1,1,(BYTE*)wire+1,&z)));
          printf("PASS format 86: guarded +c8, exact %x/10000 and rejected neighbors\n",expected);
      } else CK(FAILED(xs_color_allocation_info(guest.Get(),1,1,wire,&z)));
       CK(FAILED(xs_msaa_allocation_info1(guest.Get(),1,1,wire,&z,&detail)));
       const bool amd=id.VendorId==0x1002 && id.DeviceId==0x164e;
       const bool measured=amd || (id.VendorId==0x10de && id.DeviceId==0x2484);
       const struct {UINT w,h,fmt,rtx,radv;} group[]={
           {2560,1440,16,480,462},{2560,1440,61,60,61},{2560,1440,17,480,460},{1280,720,42,60,60},{640,360,62,4,4}};
       using Query1=D3D12_RESOURCE_ALLOCATION_INFO*(WINAPI*)(D3D12_RESOURCE_ALLOCATION_INFO*,IUnknown*,UINT,UINT,const void*,D3D12_RESOURCE_ALLOCATION_INFO1*);
       auto query1=(Query1)((*(void***)guest.Get())[0x360/8]);
       ComPtr<ID3D12Device4> d4;HR(d.As(&d4));
       for(auto t:group){
           DWORD v2[]={3,0,0,0,t.w,0,t.h,0x10001,t.fmt,1,0,0,0xc00005};memcpy(wire,v2,sizeof v2);
           D3D12_RESOURCE_DESC hostrd={};memcpy(&hostrd,v2,sizeof v2);hostrd.Flags=(D3D12_RESOURCE_FLAGS)5;
           D3D12_RESOURCE_ALLOCATION_INFO1 one={};
           auto host=d4->GetResourceAllocationInfo1(1,1,&hostrd,&one);
           printf("RECORD native %04x:%04x %ux%u fmt=%u bytes=%llx/%llx detail=%llx,%llx/%llx\n",
               id.VendorId,id.DeviceId,t.w,t.h,t.fmt,host.SizeInBytes,host.Alignment,one.Offset,one.SizeInBytes,one.Alignment);
           struct {UINT64 pre;D3D12_RESOURCE_ALLOCATION_INFO value;UINT64 post;} g={13,{},17};
           struct {UINT64 pre;D3D12_RESOURCE_ALLOCATION_INFO1 value;UINT64 post;} h={23,{},29};
           if(measured){
               CK(query(&g.value,guest.Get(),1,1,wire)==&g.value && g.value.SizeInBytes==UINT64(amd?t.radv:t.rtx)*65536 &&
                   g.value.Alignment==65536 && !memcmp(&host,&g.value,sizeof host) && !one.Offset &&
                   one.SizeInBytes==host.SizeInBytes && one.Alignment==host.Alignment);
           }else CK(xs_color_allocation_info(guest.Get(),1,1,wire,&g.value)==E_NOTIMPL && !g.value.SizeInBytes && !g.value.Alignment);
           if(measured && t.fmt==62){
               g.value={};h.value.Offset=~0ull;
               CK(query1(&g.value,guest.Get(),1,1,wire,&h.value)==&g.value && !memcmp(&host,&g.value,sizeof host) &&
                   !h.value.Offset && h.value.SizeInBytes==0x40000 && h.value.Alignment==65536);
           }else CK(xs_msaa_allocation_info1(guest.Get(),1,1,wire,&g.value,&h.value)==E_NOTIMPL);
           CK(g.pre==13 && g.post==17 && h.pre==23 && h.post==29);
           auto deny=[&](UINT node,UINT n,const void *ptr){return
               FAILED(xs_color_allocation_info(guest.Get(),node,n,ptr,&z)) && !z.Alignment && !z.SizeInBytes &&
               FAILED(xs_msaa_allocation_info1(guest.Get(),node,n,ptr,&z,&detail));};
           CK(deny(0,1,wire) && deny(1,2,wire) && deny(1,1,(BYTE*)wire+1));
           for(UINT j=0;j<13;j++)if(j!=1){DWORD mask=j==8?0x80:1;wire[j]^=mask;CK(deny(1,1,wire));wire[j]^=mask;}
           wire[12]=0x300005;CK(deny(1,1,wire));
       }
       printf("PASS five-query group: %s; bad fields, node/count and short record denied\n",
           measured?"five +c8, exact R8_UINT +360":"unmeasured adapter denied both slots");
    }
    CK(VirtualFree(page,0,MEM_RELEASE));
    // Place adjacent desktop images, then verify that clearing each leaves its neighbors intact.
    D3D12_HEAP_DESC hd={};hd.SizeInBytes=end;hd.Properties.Type=D3D12_HEAP_TYPE_DEFAULT;
    hd.Properties.CreationNodeMask=hd.Properties.VisibleNodeMask=1;hd.Flags=D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES;
    ComPtr<ID3D12Heap> heap;HR(d->CreateHeap(&hd,IID_PPV_ARGS(&heap)));
    ComPtr<ID3D12CommandQueue> q;ComPtr<ID3D12CommandAllocator> a;ComPtr<ID3D12GraphicsCommandList> l;
    D3D12_COMMAND_QUEUE_DESC qd={};HR(d->CreateCommandQueue(&qd,IID_PPV_ARGS(&q)));HR(d->CreateCommandAllocator(qd.Type,IID_PPV_ARGS(&a)));
    HR(d->CreateCommandList(0,qd.Type,a.Get(),nullptr,IID_PPV_ARGS(&l)));
    ComPtr<ID3D12Resource> tex[count],rb[count];ComPtr<ID3D12DescriptorHeap> views[count];
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp[count][2]={};
    for(UINT i=0;i<count;i++){
        bool ds=targets[i].fmt==19;auto state=ds?D3D12_RESOURCE_STATE_DEPTH_WRITE:D3D12_RESOURCE_STATE_RENDER_TARGET;
        HR(d->CreatePlacedResource(heap.Get(),offsets[i],rd+i,state,nullptr,IID_PPV_ARGS(&tex[i])));
        D3D12_DESCRIPTOR_HEAP_DESC desc={ds?D3D12_DESCRIPTOR_HEAP_TYPE_DSV:D3D12_DESCRIPTOR_HEAP_TYPE_RTV,1,D3D12_DESCRIPTOR_HEAP_FLAG_NONE,1};
        HR(d->CreateDescriptorHeap(&desc,IID_PPV_ARGS(&views[i])));auto h=views[i]->GetCPUDescriptorHandleForHeapStart();
        if(ds){D3D12_DEPTH_STENCIL_VIEW_DESC v={};v.Format=DXGI_FORMAT_D32_FLOAT_S8X24_UINT;v.ViewDimension=D3D12_DSV_DIMENSION_TEXTURE2D;
            d->CreateDepthStencilView(tex[i].Get(),&v,h);l->ClearDepthStencilView(h,D3D12_CLEAR_FLAG_DEPTH|D3D12_CLEAR_FLAG_STENCIL,(i+1)/8.f,(BYTE)(0x31+i),0,nullptr);}
        else{float rgba[]={float(i&1),float((i>>1)&1),float((i>>2)&1),1.f};
            if(targets[i].fmt==42 || targets[i].fmt==41)rgba[0]=float(i+1);
            d->CreateRenderTargetView(tex[i].Get(),nullptr,h);l->ClearRenderTargetView(h,rgba,0,nullptr);}
        D3D12_RESOURCE_BARRIER b={};b.Transition={tex[i].Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,state,D3D12_RESOURCE_STATE_COPY_SOURCE};l->ResourceBarrier(1,&b);
    }
    for(UINT i=0;i<count;i++){
        UINT planes=targets[i].fmt==19?2:1;UINT64 bytes;d->GetCopyableFootprints(rd+i,0,planes,0,fp[i],nullptr,nullptr,&bytes);
        D3D12_RESOURCE_DESC buf={};buf.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;buf.Width=bytes;buf.Height=buf.MipLevels=buf.DepthOrArraySize=buf.SampleDesc.Count=1;buf.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        auto hp=hd.Properties;hp.Type=D3D12_HEAP_TYPE_READBACK;HR(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&buf,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&rb[i])));
        for(UINT p=0;p<planes;p++){D3D12_TEXTURE_COPY_LOCATION src={},dst={};src.pResource=tex[i].Get();src.SubresourceIndex=p;
            dst.pResource=rb[i].Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=fp[i][p];l->CopyTextureRegion(&dst,0,0,0,&src,nullptr);}
    }
    HR(l->Close());ID3D12CommandList *ls[]={l.Get()};q->ExecuteCommandLists(1,ls);ComPtr<ID3D12Fence> f;HR(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&f)));
    HANDLE e=CreateEventW(nullptr,FALSE,FALSE,nullptr);CK(e);HR(q->Signal(f.Get(),1));HR(f->SetEventOnCompletion(1,e));CK(WaitForSingleObject(e,15000)==WAIT_OBJECT_0);CloseHandle(e);
    for(UINT i=0;i<count;i++){
        BYTE *p;HR(rb[i]->Map(0,nullptr,(void**)&p));auto t=targets[i];DWORD expect=0xff000000|((i&1)?0xff0000:0)|((i&2)?0xff00:0)|((i&4)?0xff:0);
        if(t.fmt==42)expect=i+1;
        if(t.fmt==41){float val=float(i+1);memcpy(&expect,&val,4);}
        if(t.fmt==19){float z=(i+1)/8.f;memcpy(&expect,&z,4);}
        for(UINT y=0;y<t.h;y++){auto row=(DWORD*)(p+fp[i][0].Offset+y*fp[i][0].Footprint.RowPitch);
            for(UINT x=0;x<t.w;x++){CK(row[x]==expect);if(t.fmt==19)CK(p[fp[i][1].Offset+y*fp[i][1].Footprint.RowPitch+x]==0x31+i);}}
        rb[i]->Unmap(0,nullptr);
    }
    puts("PASS deferred targets: eight guarded queries, adjacent desktop placements and full fenced color/depth/stencil readbacks");
}
