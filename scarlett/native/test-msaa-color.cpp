#include <d3d12.h>
#include <wrl/client.h>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include "xmem/marked_section.h"
using Microsoft::WRL::ComPtr;
#define CK(x) do { if (!(x)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); ExitProcess(1); } } while(0)
#define HR(x) do { HRESULT v=(x); if (FAILED(v)) {fprintf(stderr,"FAIL %d %s hr=%08lx\n",__LINE__,#x,(DWORD)v);ExitProcess(1);} } while(0)
extern "C" HRESULT WINAPI D3D12XboxCreateDevice(void *,const void *,REFIID,void **);
extern "C" HRESULT xs_msaa_allocation_info1(IUnknown *,UINT,UINT,const void *,D3D12_RESOURCE_ALLOCATION_INFO *,D3D12_RESOURCE_ALLOCATION_INFO1 *);
extern "C" ID3D12Resource *test_msaa_host(IUnknown *);
extern "C" HRESULT xs_resolve_color_rtv(IUnknown *,SIZE_T,D3D12_CPU_DESCRIPTOR_HANDLE *,ID3D12Resource **);
extern "C" HRESULT xs_write_view(IUnknown *,UINT,IUnknown *,const void *,SIZE_T);
extern "C" HRESULT xs_resolve_view(IUnknown *,SIZE_T,UINT,ID3D12DescriptorHeap **,D3D12_GPU_DESCRIPTOR_HANDLE *);
using MakeHeap=HRESULT(WINAPI*)(IUnknown *,const void *,REFIID,void **);
using Start=SIZE_T(WINAPI*)(IUnknown *);
using View=void(WINAPI*)(IUnknown *,IUnknown *,const void *,SIZE_T);
using Query=D3D12_RESOURCE_ALLOCATION_INFO *(WINAPI *)(D3D12_RESOURCE_ALLOCATION_INFO *,IUnknown *,UINT,UINT,const void *);
using Rich=D3D12_RESOURCE_ALLOCATION_INFO *(WINAPI *)(D3D12_RESOURCE_ALLOCATION_INFO *,IUnknown *,UINT,UINT,const void *,D3D12_RESOURCE_ALLOCATION_INFO1 *);
using Place=HRESULT(WINAPI*)(IUnknown *,UINT64,const void *,UINT,const void *,REFIID,void **);
static unsigned load(const char *name,BYTE (&bytes)[8192]) {
    FILE *f=fopen(name,"rb");CK(f);size_t n=fread(bytes,1,sizeof bytes,f);CK(n && feof(f));fclose(f);return n;
}
int main() {
    constexpr UINT span=4<<20; D3D12_RESOURCE_DESC rd={};rd.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;rd.Width=rd.Height=rd.DepthOrArraySize=rd.MipLevels=1;
    rd.Format=DXGI_FORMAT_R8G8B8A8_UNORM;rd.SampleDesc.Count=4;rd.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    DWORD params[20]={};params[0]=0x20004;params[2]=0x100000;params[10]=1;params[15]=0x20;
    IID boot={0x5405c344,0xd457,0x444e,{0xb4,0xdd,0x23,0x66,0xe4,0x5a,0xee,0x39}};
    ComPtr<IUnknown> g;HR(D3D12XboxCreateDevice(nullptr,params,boot,(void**)g.GetAddressOf()));
    auto vt=*(void***)g.Get();struct {UINT64 pre;D3D12_RESOURCE_ALLOCATION_INFO total;D3D12_RESOURCE_ALLOCATION_INFO1 one;UINT64 post;} got={13,{},{},17};
    BYTE *pg=(BYTE*)VirtualAlloc(nullptr,8192,MEM_RESERVE,PAGE_NOACCESS);CK(pg&&VirtualAlloc(pg,4096,MEM_COMMIT,PAGE_READWRITE));BYTE *wire=pg+4096-52;memcpy(wire,&rd,52);
    CK(((Query)vt[0xc8/8])(&got.total,g.Get(),1,1,wire)==&got.total && got.total.Alignment==span && got.total.SizeInBytes==span);
    CK(((Rich)vt[0x360/8])(&got.total,g.Get(),1,1,wire,&got.one)==&got.total && got.pre==13 && got.post==17);
    CK(got.total.Alignment==span && got.total.SizeInBytes==span && got.one.Alignment==span && got.one.SizeInBytes==span && !got.one.Offset);
    DWORD words[13]; memcpy(words,&rd,52);for(UINT j:{0u,2u,3u,4u,5u,6u,7u,8u,9u,10u,11u,12u}) {
        words[j]^=1;CK(FAILED(xs_msaa_allocation_info1(g.Get(),1,1,words,&got.total,&got.one)));words[j]^=1;
    }
    CK(FAILED(xs_msaa_allocation_info1(g.Get(),0,1,wire,&got.total,&got.one)) && FAILED(xs_msaa_allocation_info1(g.Get(),1,2,wire,&got.total,&got.one)) && FAILED(xs_msaa_allocation_info1(g.Get(),1,1,wire+1,&got.total,&got.one)));
    CK(VirtualFree(pg,0,MEM_RELEASE));
    ComPtr<ID3D12Device>d;HR(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&d)));
    D3D12_CLEAR_VALUE clear={};clear.Format=rd.Format;for(float &c:clear.Color)c=1;
    constexpr UINT pool=0x89700000, off=0xe800000;
    auto alloc=(void*(WINAPI*)(void*,SIZE_T,DWORD,UINT64,DWORD))(void*)GetProcAddress(LoadLibraryW(L"xmem.dll"),"XMemVirtualAlloc");CK(alloc);
    BYTE *wc=(BYTE*)alloc(nullptr,pool,0x20003000,0x283,0x2404);CK(wc); memset(wc+off-16,0xa7,span+32);
    scarlett_section_info pin;HR(scarlett_query_section(0x53430005,wc+off,span,&pin));CK(pin.offset==off && pin.view_size==pool);
    auto open=(HRESULT(WINAPI*)(ID3D12Device*,SIZE_T,HANDLE,HANDLE*,ID3D12Heap**))(void*)GetProcAddress(GetModuleHandleW(L"d3d12core.dll"),"ScarlettWcHeapV1");CK(open);
    ComPtr<ID3D12Heap>wcHeap;HR(open(d.Get(),pool,pin.section,nullptr,wcHeap.GetAddressOf()));
    ComPtr<ID3D12Resource> reject;CK((wcHeap->GetDesc().Flags&D3D12_HEAP_FLAG_DENY_RT_DS_TEXTURES)!=0);
    CK(d->CreatePlacedResource(wcHeap.Get(),off,&rd,D3D12_RESOURCE_STATE_RENDER_TARGET,&clear,IID_PPV_ARGS(&reject))==E_INVALIDARG && !reject);
    auto place=(Place)vt[0x428/8];void *guest=(void*)1;
    CK(FAILED(place(g.Get(),(UINT_PTR)wc+off+4096,&rd,6,&clear,__uuidof(ID3D12Resource),&guest)) && !guest);
    clear.Color[2]=0;CK(FAILED(place(g.Get(),(UINT_PTR)wc+off,&rd,6,&clear,__uuidof(ID3D12Resource),&guest)) && !guest);clear.Color[2]=1;
    HRESULT created=place(g.Get(),(UINT_PTR)wc+off,&rd,6,&clear,__uuidof(ID3D12Resource),&guest);
    bool imported=SUCCEEDED(created);printf("+428 hr=%08lx\n",(DWORD)created);
#ifdef TEST_NV_IMPORT
    CK(imported && guest);
#else
    CK(created==DXGI_ERROR_UNSUPPORTED && !guest);
#endif
    ComPtr<IUnknown> slots,rtvs; SIZE_T slot=0,rtSlot=0;
    D3D12_CPU_DESCRIPTOR_HANDLE middle={};
    ID3D12DescriptorHeap *bank=nullptr;D3D12_GPU_DESCRIPTOR_HANDLE gpu={};
    ComPtr<ID3D12Resource> original;
    if (imported) {
        auto obj=(IUnknown*)guest;original.Attach(test_msaa_host(obj));CK(original);
        UINT hd[]={0,4,1,1}; HR(((MakeHeap)vt[0x70/8])(g.Get(),hd,__uuidof(ID3D12DescriptorHeap),(void**)slots.GetAddressOf()));
        slot=((Start)(*(void***)slots.Get())[0x48/8])(slots.Get())+32;
        DWORD ms[]={28,6,0x1688,0,0,0,0,0};
        BYTE *page=(BYTE*)VirtualAlloc(nullptr,8192,MEM_RESERVE,PAGE_NOACCESS);CK(page&&VirtualAlloc(page,4096,MEM_COMMIT,PAGE_READWRITE));
        auto end=page+4096-sizeof ms;memcpy(end,ms,sizeof ms);
        ((View)vt[0x90/8])(g.Get(),obj,end,slot);UINT64 before=*(UINT64*)slot;
        ms[0]=DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;CK(FAILED(xs_write_view(g.Get(),0x90,obj,ms,slot)));ms[0]=28;
        for(unsigned j=0;j<8;j++){ms[j]^=1; CK(FAILED(xs_write_view(g.Get(),0x90,obj,ms,slot)));ms[j]^=1;}
        DWORD single[10]={28,4,0x1688,0,0,1};CK(FAILED(xs_write_view(g.Get(),0x90,obj,single,slot)));
        CK(FAILED(xs_write_view(g.Get(),0x90,g.Get(),ms,slot)) && FAILED(xs_write_view(g.Get(),0x90,obj,ms,slot+1)));
        CK(FAILED(xs_write_view(g.Get(),0x90,obj,end+1,slot)) && *(UINT64*)slot==before);
        *(UINT64*)slot^=1;CK(FAILED(xs_resolve_view(slots.Get(),slot,0x90,&bank,&gpu)) && !bank && !gpu.ptr);*(UINT64*)slot^=1;
        HR(xs_resolve_view(slots.Get(),slot,0x90,&bank,&gpu));CK(bank&&gpu.ptr);
        UINT rh[]={2,4096,0,1};
        HR(((MakeHeap)vt[0x70/8])(g.Get(),rh,__uuidof(ID3D12DescriptorHeap),(void**)rtvs.GetAddressOf()));
        rtSlot=((Start)(*(void***)rtvs.Get())[0x48/8])(rtvs.Get())+2*32;
        DWORD rw[]={28,6,0,0,0,0};
        end=page+4096-sizeof rw;memcpy(end,rw,sizeof rw);
        memset((void*)(rtSlot-32),0xcd,96);
        ((View)vt[0xa0/8])(g.Get(),obj,end,rtSlot);
        UINT64 tag[2];memcpy(tag,(void*)rtSlot,sizeof tag);
        for(auto &w:rw){w^=1;CK(FAILED(xs_write_view(g.Get(),0xa0,obj,rw,rtSlot)));w^=1;}
        rw[1]=4;CK(FAILED(xs_write_view(g.Get(),0xa0,obj,rw,rtSlot)));rw[1]=6;
        CK(FAILED(xs_write_view(g.Get(),0xa0,g.Get(),rw,rtSlot)) && FAILED(xs_write_view(g.Get(),0xa0,obj,rw,slot)));
        CK(FAILED(xs_write_view(g.Get(),0xa0,obj,rw,rtSlot+1)));
        ComPtr<IUnknown> foreign,foreignViews,depth;
        HR(D3D12XboxCreateDevice(nullptr,params,boot,(void**)foreign.GetAddressOf()));
        HR(((MakeHeap)vt[0x70/8])(foreign.Get(),rh,__uuidof(ID3D12DescriptorHeap),(void**)foreignViews.GetAddressOf()));
        auto foreignSlot=((Start)(*(void***)foreignViews.Get())[0x48/8])(foreignViews.Get());
        CK(FAILED(xs_write_view(foreign.Get(),0xa0,obj,rw,foreignSlot)) && FAILED(xs_write_view(g.Get(),0xa0,obj,rw,foreignSlot)));
        auto dr=rd;dr.Format=DXGI_FORMAT_R32G8X24_TYPELESS;dr.Flags=D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        D3D12_CLEAR_VALUE dc={};dc.Format=DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
        HR(place(g.Get(),(UINT_PTR)wc+off+span,&dr,6,&dc,__uuidof(ID3D12Resource),(void**)depth.GetAddressOf()));
        CK(FAILED(xs_write_view(g.Get(),0xa0,depth.Get(),rw,rtSlot)));
        memcpy(end+1,rw,sizeof rw-1);CK(xs_write_view(g.Get(),0xa0,obj,end+1,rtSlot)==E_POINTER);
        CK(VirtualFree(page,0,MEM_RELEASE) && !memcmp(tag,(void*)rtSlot,sizeof tag));
        for(int i=-32;i<64;i++) if(i<0 || i>=16) CK(((BYTE*)rtSlot)[i]==0xcd);
        ComPtr<ID3D12Resource> selected;
        *(UINT64*)rtSlot^=1;CK(FAILED(xs_resolve_color_rtv(rtvs.Get(),rtSlot,&middle,selected.GetAddressOf())) && !middle.ptr && !selected);*(UINT64*)rtSlot^=1;
        HR(xs_resolve_color_rtv(rtvs.Get(),rtSlot,&middle,selected.GetAddressOf()));
        CK(middle.ptr && middle.ptr!=rtSlot && selected.Get()==original.Get());
        selected.Reset();obj->Release();original.Reset();

        CK(FAILED(place(g.Get(),(UINT_PTR)wc+off,&rd,6,&clear,__uuidof(ID3D12Resource),&guest)) && !guest);
    }
    for(UINT i=0;i<span+32;i++) CK((wc+off-16)[i]==0xa7);
    // Independent CPU alias remains valid after release of the title mapping.
    scarlett_section_info alias; HR(scarlett_query_section_handle(0x53430004,nullptr,pool,&alias,pin.section));
    CK(CloseHandle(pin.section));wcHeap.Reset();CK(VirtualFree(wc,0,MEM_RELEASE));
    BYTE *replacement=wc+off;CK(VirtualAlloc(replacement,span,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE)==replacement);memset(replacement,0xad,span);
    guest=(void*)1;CK(FAILED(place(g.Get(),(UINT_PTR)replacement,&rd,6,&clear,__uuidof(ID3D12Resource),&guest)) && !guest);
    puts("PASS guest +c8/+360 4MiB/4MiB, bounded descriptor/outputs; original WC slice and both guards unchanged at creation");
    // Reference rendering on three neighboring slices of a real RT-capable heap.
    D3D12_HEAP_PROPERTIES hp={};hp.Type=D3D12_HEAP_TYPE_DEFAULT;hp.VisibleNodeMask=hp.CreationNodeMask=1;
    D3D12_HEAP_DESC hd={};hd.Alignment=span;hd.SizeInBytes=3*span;hd.Properties=hp;hd.Flags=D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES;
    ComPtr<ID3D12Heap>heap;HR(d->CreateHeap(&hd,IID_PPV_ARGS(&heap)));ComPtr<ID3D12Resource> rt[3],resolved,rb,report;
    for(UINT i=0;i<3;i++) if (i==1 && imported) {
        HR(xs_resolve_color_rtv(rtvs.Get(),rtSlot,&middle,rt[i].GetAddressOf()));
    } else HR(d->CreatePlacedResource(heap.Get(),i*span,&rd,D3D12_RESOURCE_STATE_RENDER_TARGET,&clear,IID_PPV_ARGS(&rt[i])));
    heap.Reset();auto single=rd;single.SampleDesc.Count=1;single.Flags=D3D12_RESOURCE_FLAG_NONE;
    HR(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&single,D3D12_RESOURCE_STATE_RESOLVE_DEST,nullptr,IID_PPV_ARGS(&resolved)));
    D3D12_RESOURCE_DESC buf={};buf.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;buf.Height=buf.DepthOrArraySize=buf.MipLevels=buf.SampleDesc.Count=1;buf.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;buf.Width=16;buf.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    HR(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&buf,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&report)));
    hp.Type=D3D12_HEAP_TYPE_READBACK;buf.Width=2048;buf.Flags=D3D12_RESOURCE_FLAG_NONE;
    HR(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&buf,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&rb)));
    D3D12_DESCRIPTOR_HEAP_DESC dd={D3D12_DESCRIPTOR_HEAP_TYPE_RTV,3,D3D12_DESCRIPTOR_HEAP_FLAG_NONE,1};ComPtr<ID3D12DescriptorHeap>rh,sh;HR(d->CreateDescriptorHeap(&dd,IID_PPV_ARGS(&rh)));
    auto h=rh->GetCPUDescriptorHandleForHeapStart();D3D12_CPU_DESCRIPTOR_HANDLE handles[3];for(UINT i=0;i<3;i++){
        if(i==1 && imported) handles[i]=middle;
        else {handles[i]=h;d->CreateRenderTargetView(rt[i].Get(),nullptr,h);}
        h.ptr+=d->GetDescriptorHandleIncrementSize(dd.Type);
    }
    if (!imported) { dd={D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,1,D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,1};HR(d->CreateDescriptorHeap(&dd,IID_PPV_ARGS(&sh)));d->CreateShaderResourceView(rt[1].Get(),nullptr,sh->GetCPUDescriptorHandleForHeapStart());bank=sh.Get();gpu=sh->GetGPUDescriptorHandleForHeapStart(); }
    D3D12_DESCRIPTOR_RANGE range={D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,0,0,0};D3D12_ROOT_PARAMETER par[2]={};par[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;par[0].DescriptorTable={1,&range};par[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV;
    D3D12_ROOT_SIGNATURE_DESC rs={2,par,0,nullptr,D3D12_ROOT_SIGNATURE_FLAG_NONE};ComPtr<ID3DBlob>blob;HR(D3D12SerializeRootSignature(&rs,D3D_ROOT_SIGNATURE_VERSION_1,&blob,nullptr));ComPtr<ID3D12RootSignature> root;HR(d->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&root)));
    BYTE vs[8192],ps[8192],cs[8192];D3D12_GRAPHICS_PIPELINE_STATE_DESC gp={};gp.pRootSignature=root.Get();gp.VS={vs,load("vs.dxil",vs)};gp.PS={ps,load("ps.dxil",ps)};gp.SampleMask=~0u;gp.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;gp.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;gp.RasterizerState.DepthClipEnable=TRUE;gp.RasterizerState.MultisampleEnable=TRUE;gp.BlendState.RenderTarget[0].RenderTargetWriteMask=15;gp.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;gp.NumRenderTargets=1;gp.RTVFormats[0]=rd.Format;gp.SampleDesc=rd.SampleDesc;
    D3D12_COMPUTE_PIPELINE_STATE_DESC cp={};cp.pRootSignature=root.Get();cp.CS={cs,load("cs.dxil",cs)};ComPtr<ID3D12PipelineState>graphics,compute;HR(d->CreateGraphicsPipelineState(&gp,IID_PPV_ARGS(&graphics)));HR(d->CreateComputePipelineState(&cp,IID_PPV_ARGS(&compute)));
    D3D12_COMMAND_QUEUE_DESC qd={};ComPtr<ID3D12CommandQueue>q;ComPtr<ID3D12CommandAllocator>a;ComPtr<ID3D12GraphicsCommandList>l;HR(d->CreateCommandQueue(&qd,IID_PPV_ARGS(&q)));HR(d->CreateCommandAllocator(qd.Type,IID_PPV_ARGS(&a)));HR(d->CreateCommandList(0,qd.Type,a.Get(),graphics.Get(),IID_PPV_ARGS(&l)));
    auto transition=[&](ID3D12Resource *r,D3D12_RESOURCE_STATES x,D3D12_RESOURCE_STATES y){D3D12_RESOURCE_BARRIER b={};b.Transition={r,0,x,y};l->ResourceBarrier(1,&b);};
    float guards[][4]={{1,0,0,1},{0,0,0,0},{0,1,0,1}};for(UINT i=0;i<3;i++)l->ClearRenderTargetView(handles[i],guards[i],0,nullptr);
    D3D12_VIEWPORT vp={0,0,1,1,0,1};D3D12_RECT rect={0,0,1,1};l->RSSetViewports(1,&vp);l->RSSetScissorRects(1,&rect);l->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);l->SetGraphicsRootSignature(root.Get());l->OMSetRenderTargets(1,handles+1,FALSE,nullptr);l->DrawInstanced(3,1,0,0);
    transition(rt[1].Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);l->SetPipelineState(compute.Get());l->SetComputeRootSignature(root.Get());ID3D12DescriptorHeap *heaps[]={bank};l->SetDescriptorHeaps(1,heaps);l->SetComputeRootDescriptorTable(0,gpu);l->SetComputeRootUnorderedAccessView(1,report->GetGPUVirtualAddress());l->Dispatch(1,1,1);transition(report.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);l->CopyBufferRegion(rb.Get(),0,report.Get(),0,16);
    for(UINT i=0;i<3;i++){
        if(i)transition(resolved.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_RESOLVE_DEST);
        transition(rt[i].Get(),i==1?D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE:D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
        l->ResolveSubresource(resolved.Get(),0,rt[i].Get(),0,rd.Format);transition(resolved.Get(),D3D12_RESOURCE_STATE_RESOLVE_DEST,D3D12_RESOURCE_STATE_COPY_SOURCE);
        D3D12_TEXTURE_COPY_LOCATION src={},dst={};src.pResource=resolved.Get();dst.pResource=rb.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint={512ull*(i+1),{rd.Format,1,1,1,256}};l->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
    }
    HR(l->Close());if (imported) rt[1].Reset();ID3D12CommandList *ls[]={l.Get()};q->ExecuteCommandLists(1,ls);ComPtr<ID3D12Fence>f;HR(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&f)));HR(q->Signal(f.Get(),1));HANDLE e=CreateEventW(nullptr,FALSE,FALSE,nullptr);CK(e);HR(f->SetEventOnCompletion(1,e));CK(WaitForSingleObject(e,10000)==WAIT_OBJECT_0);CloseHandle(e);HR(d->GetDeviceRemovedReason());
    DWORD *m;HR(rb->Map(0,nullptr,(void**)&m));for(UINT s=0;s<4;s++) CK(m[s]==((4+4*s)|((12+8*s)<<8)|((128+12*s)<<16)|((240-16*s)<<24)));
    CK(m[128]==0xff0000ff && m[256]==0xd892180a && m[384]==0xff00ff00);printf("PASS native 4x samples=%08lx,%08lx,%08lx,%08lx resolve=%08lx, adjacent red/green placements intact\n",m[0],m[1],m[2],m[3],m[256]);rb->Unmap(0,nullptr);
    BYTE *base=(BYTE*)alias.base; UINT changed=0;
    for(UINT i=0;i<span;i++) changed+=base[off+i]!=0xa7;
    printf("original bytes changed=%u (imported=%d)\n",changed,imported); CK(changed==(imported?16u:0u));
    if (imported) {
        const UINT offsets[]={0,4,16,20};
        for(UINT s=0;s<4;s++){
            DWORD word;memcpy(&word,base+off+offsets[s],sizeof word);
            CK(word==((4+4*s)|((12+8*s)<<8)|((128+12*s)<<16)|((240-16*s)<<24)));
            printf("PASS original WC sample %u at slice +%#x = %08lx\n",s,offsets[s],word);
        }
        // Test the slot's final Release explicitly, independently of queue/allocator lifetime.
        l.Reset();a.Reset();rh.Reset();q.Reset();void *again=nullptr;
        CK(FAILED(place(g.Get(),(UINT_PTR)base+off,&rd,6,&clear,__uuidof(ID3D12Resource),&again)) && !again);
        DWORD null_srv[10]={42,4,0x1688}; HR(xs_write_view(g.Get(),0x90,nullptr,null_srv,slot));
        CK(FAILED(place(g.Get(),(UINT_PTR)base+off,&rd,6,&clear,__uuidof(ID3D12Resource),&again)) && !again);
        rtvs.Reset();
        HR(place(g.Get(),(UINT_PTR)base+off,&rd,6,&clear,__uuidof(ID3D12Resource),&again));CK(again);
        DWORD ms[]={28,6,0x1688,0,0,0,0,0};
        ((View)vt[0x90/8])(g.Get(),(IUnknown*)again,ms,slot);((IUnknown*)again)->Release();
        CK(FAILED(place(g.Get(),(UINT_PTR)base+off,&rd,6,&clear,__uuidof(ID3D12Resource),&again)) && !again);
        HR(xs_write_view(g.Get(),0x90,nullptr,null_srv,slot));
        HR(place(g.Get(),(UINT_PTR)base+off,&rd,6,&clear,__uuidof(ID3D12Resource),&again));((IUnknown*)again)->Release();
        puts("PASS +a0 Texture2DMS RTV wrote four original WC samples; after fence and SRV overwrite, only RTV heap release freed the slice");
    }
    for(UINT i=0;i<16;i++)CK(base[off-16+i]==0xa7 && base[off+span+i]==0xa7);
    for(UINT i=0;i<span;i++)CK(replacement[i]==0xad);
    CK(VirtualFree(replacement,0,MEM_RELEASE)&&VirtualFree(alias.base,0,MEM_RELEASE));
}
