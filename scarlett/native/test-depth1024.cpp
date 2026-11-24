#include <d3d12.h>
#include <wrl/client.h>
#include <cstdio>
#include <cstring>
using Microsoft::WRL::ComPtr;
extern "C" HRESULT WINAPI D3D12XboxCreateDevice(void *,const void *,REFIID,void **);
extern "C" HRESULT xs_color_allocation_info(IUnknown *,UINT,UINT,const void *,D3D12_RESOURCE_ALLOCATION_INFO *);
extern "C" HRESULT xs_msaa_allocation_info1(IUnknown *,UINT,UINT,const void *,D3D12_RESOURCE_ALLOCATION_INFO *,D3D12_RESOURCE_ALLOCATION_INFO1 *);
#define CK(x) do{if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);return 1;}}while(0)
#define HR(x) CK(SUCCEEDED(x))
static int check(UINT width, UINT height, DWORD flags) {
    DWORD params[20]={};params[0]=0x20004;params[2]=0x100000;params[10]=1;params[15]=0x20;
    const IID boot={0x5405c344,0xd457,0x444e,{0xb4,0xdd,0x23,0x66,0xe4,0x5a,0xee,0x39}};
    ComPtr<IUnknown> dev;HR(D3D12XboxCreateDevice(nullptr,params,boot,(void**)dev.GetAddressOf()));
    using Query=D3D12_RESOURCE_ALLOCATION_INFO *(WINAPI *)(D3D12_RESOURCE_ALLOCATION_INFO *,IUnknown *,UINT,UINT,const void *);
    auto query=(Query)((*(void***)dev.Get())[0xc8/8]);
    DWORD wire[]={3,0,0,0,width,0,height,0x10001,19,1,0,0,flags};
    auto pages=(BYTE*)VirtualAlloc(nullptr,8192,MEM_RESERVE,PAGE_NOACCESS);CK(pages && VirtualAlloc(pages,4096,MEM_COMMIT,PAGE_READWRITE));
    BYTE *prefix=pages+4096-sizeof wire;memcpy(prefix,wire,sizeof wire);
    struct {UINT64 before;D3D12_RESOURCE_ALLOCATION_INFO info;UINT64 after;} g={1,{},2};
    CK(query(&g.info,dev.Get(),1,1,prefix)==&g.info && g.before==1 && g.after==2);
    D3D12_RESOURCE_DESC rd={};memcpy(&rd,wire,sizeof wire);rd.Flags=D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    ComPtr<ID3D12Device> d;HR(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&d)));
    auto desktop=d->GetResourceAllocationInfo(1,1,&rd);
    CK(!memcmp(&desktop,&g.info,sizeof desktop));
    printf("D994 query %ux%u flags=%#lx size=%#llx align=%#llx\n",width,height,flags,g.info.SizeInBytes,g.info.Alignment);
    CK(g.info.Alignment==65536 && (width==994 ? (g.info.SizeInBytes==0x4f0000 || g.info.SizeInBytes==0x350000) : (g.info.SizeInBytes==0x800000 || g.info.SizeInBytes==0x520000)));
    D3D12_RESOURCE_ALLOCATION_INFO total={};D3D12_RESOURCE_ALLOCATION_INFO1 one={};
    CK(xs_msaa_allocation_info1(dev.Get(),1,1,prefix,&total,&one)==E_NOTIMPL);
    auto deny=[&](UINT node,UINT cnt,const void *p){D3D12_RESOURCE_ALLOCATION_INFO bad={1,2};
        return FAILED(xs_color_allocation_info(dev.Get(),node,cnt,p,&bad)) && !bad.SizeInBytes && !bad.Alignment;};
    CK(deny(0,1,prefix) && deny(1,2,prefix) && deny(1,1,prefix+1));
    for(UINT k=0;k<13;k++)if(k!=1){wire[k]^=1;CK(deny(1,1,wire));wire[k]^=1;}
    wire[12]=width==994?2:0x800002;CK(deny(1,1,wire));
    CK(VirtualFree(pages,0,MEM_RELEASE));
    // Desktop reference placement. No console VA or title descriptor is supplied by +c8.
    D3D12_HEAP_DESC hd={};hd.SizeInBytes=g.info.SizeInBytes+65536;hd.Properties.Type=D3D12_HEAP_TYPE_DEFAULT;
    hd.Properties.CreationNodeMask=hd.Properties.VisibleNodeMask=1;hd.Flags=D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES;
    ComPtr<ID3D12Heap> heap;HR(d->CreateHeap(&hd,IID_PPV_ARGS(&heap)));
    ComPtr<ID3D12Resource> image,rb,report;D3D12_CLEAR_VALUE cv={};cv.Format=DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
    HR(d->CreatePlacedResource(heap.Get(),65536,&rd,D3D12_RESOURCE_STATE_DEPTH_WRITE,&cv,IID_PPV_ARGS(&image)));
    D3D12_DESCRIPTOR_HEAP_DESC ds={D3D12_DESCRIPTOR_HEAP_TYPE_DSV,1,D3D12_DESCRIPTOR_HEAP_FLAG_NONE,1};
    ComPtr<ID3D12DescriptorHeap> dsv,srv;HR(d->CreateDescriptorHeap(&ds,IID_PPV_ARGS(&dsv)));
    auto h=dsv->GetCPUDescriptorHandleForHeapStart();D3D12_DEPTH_STENCIL_VIEW_DESC vd={};vd.Format=cv.Format;vd.ViewDimension=D3D12_DSV_DIMENSION_TEXTURE2D;
    d->CreateDepthStencilView(image.Get(),&vd,h);
    ds={D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,2,D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,1};HR(d->CreateDescriptorHeap(&ds,IID_PPV_ARGS(&srv)));
    auto sh=srv->GetCPUDescriptorHandleForHeapStart();D3D12_SHADER_RESOURCE_VIEW_DESC sv={};sv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;
    sv.Format=DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;sv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;sv.Texture2D.MipLevels=1;
    d->CreateShaderResourceView(image.Get(),&sv,sh);sh.ptr+=d->GetDescriptorHandleIncrementSize(ds.Type);
    sv.Format=DXGI_FORMAT_X32_TYPELESS_G8X24_UINT;sv.Texture2D.PlaneSlice=1;d->CreateShaderResourceView(image.Get(),&sv,sh);
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp[2]={};UINT64 bytes;d->GetCopyableFootprints(&rd,0,2,0,fp,nullptr,nullptr,&bytes);
    CK(fp[0].Footprint.Format==DXGI_FORMAT_R32_TYPELESS && fp[1].Footprint.Format==DXGI_FORMAT_R8_TYPELESS);
    D3D12_RESOURCE_DESC buf={};buf.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;buf.Width=bytes+64;buf.Height=buf.DepthOrArraySize=buf.MipLevels=buf.SampleDesc.Count=1;buf.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    auto hp=hd.Properties;hp.Type=D3D12_HEAP_TYPE_READBACK;HR(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&buf,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&rb)));
    hp.Type=D3D12_HEAP_TYPE_DEFAULT;buf.Width=64;buf.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;HR(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&buf,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&report)));
    BYTE dxil[8192];auto file=fopen("depth1024.dxil","rb");CK(file);size_t n=fread(dxil,1,sizeof dxil,file);CK(n && feof(file));fclose(file);
    ComPtr<ID3D12RootSignature> root;HR(d->CreateRootSignature(0,dxil,n,IID_PPV_ARGS(&root)));D3D12_COMPUTE_PIPELINE_STATE_DESC ps={};ps.pRootSignature=root.Get();ps.CS={dxil,n};
    ComPtr<ID3D12PipelineState> pso;HR(d->CreateComputePipelineState(&ps,IID_PPV_ARGS(&pso)));
    ComPtr<ID3D12CommandQueue> q;ComPtr<ID3D12CommandAllocator> a;ComPtr<ID3D12GraphicsCommandList> l;D3D12_COMMAND_QUEUE_DESC qd={};
    HR(d->CreateCommandQueue(&qd,IID_PPV_ARGS(&q)));HR(d->CreateCommandAllocator(qd.Type,IID_PPV_ARGS(&a)));HR(d->CreateCommandList(0,qd.Type,a.Get(),pso.Get(),IID_PPV_ARGS(&l)));
    D3D12_RECT zrect={(LONG)width-2,(LONG)height-1,(LONG)width,(LONG)height},srect=zrect;srect.left++;
    l->ClearDepthStencilView(h,D3D12_CLEAR_FLAG_DEPTH|D3D12_CLEAR_FLAG_STENCIL,.25f,0x31,0,nullptr);
    l->ClearDepthStencilView(h,D3D12_CLEAR_FLAG_DEPTH,.75f,0,1,&zrect);l->ClearDepthStencilView(h,D3D12_CLEAR_FLAG_STENCIL,0,0xa5,1,&srect);
    auto transition=[&](ID3D12Resource *p,D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after){D3D12_RESOURCE_BARRIER b={};b.Transition={p,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,before,after};l->ResourceBarrier(1,&b);};
    transition(image.Get(),D3D12_RESOURCE_STATE_DEPTH_WRITE,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    ID3D12DescriptorHeap *heaps[]={srv.Get()};l->SetDescriptorHeaps(1,heaps);l->SetComputeRootSignature(root.Get());l->SetComputeRootDescriptorTable(0,srv->GetGPUDescriptorHandleForHeapStart());l->SetComputeRootUnorderedAccessView(1,report->GetGPUVirtualAddress());l->Dispatch(1,1,1);
    transition(report.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);l->CopyBufferRegion(rb.Get(),bytes,report.Get(),0,64);
    transition(image.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE);
    for(UINT i=0;i<2;i++){D3D12_TEXTURE_COPY_LOCATION src={},dst={};src.pResource=image.Get();src.SubresourceIndex=i;dst.pResource=rb.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=fp[i];l->CopyTextureRegion(&dst,0,0,0,&src,nullptr);}
    HR(l->Close());ID3D12CommandList *lists[]={l.Get()};q->ExecuteCommandLists(1,lists);ComPtr<ID3D12Fence> f;HR(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&f)));HR(q->Signal(f.Get(),1));HANDLE ev=CreateEventW(nullptr,FALSE,FALSE,nullptr);CK(ev);HR(f->SetEventOnCompletion(1,ev));CK(WaitForSingleObject(ev,10000)==WAIT_OBJECT_0);CloseHandle(ev);
    BYTE *p;HR(rb->Map(0,nullptr,(void**)&p));for(UINT y=0;y<height;y++)for(UINT x=0;x<width;x++){
        const DWORD *depth=(const DWORD*)(p+fp[0].Offset+y*fp[0].Footprint.RowPitch);CK(depth[x]==(y==height-1 && x>=width-2?0x3f400000u:0x3e800000u));
        CK(p[fp[1].Offset+y*fp[1].Footprint.RowPitch+x]==(x==width-1 && y==height-1?0xa5:0x31));
    }
    for(UINT i=0;i<4;i++){auto v=(DWORD*)(p+bytes)+4*i;printf("sample %u: depth=%08lx stencil=%02lx compare=%lu select=%04lx\n",i,v[0],v[1],v[2],v[3]);
        CK(v[0]==(i<2?0x3e800000u:0x3f400000u) && v[1]==(i==3?0xa5u:0x31u) && v[2]==(i>=2) && v[3]==(i==3?0xcafeu:0x1234u));}
    rb->Unmap(0,nullptr);printf("PASS depth%u: guarded query, placed D32S8, both full planes, point comparison and stencil selection\n",width);return 0;
}

int main() { return check(1024,1024,2) || check(994,624,0x800002); }
