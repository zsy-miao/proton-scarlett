#include <d3d12.h>
#include <winternl.h>
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
#ifdef TEST_NV_DEPTH
extern "C" HRESULT xs_resolve_depth_dsv(IUnknown *,SIZE_T,D3D12_CPU_DESCRIPTOR_HANDLE *,ID3D12Resource **);
extern "C" HRESULT xs_resolve_view(IUnknown *,SIZE_T,UINT,ID3D12DescriptorHeap **,D3D12_GPU_DESCRIPTOR_HANDLE *);
extern "C" HRESULT xs_write_view(IUnknown *,UINT,IUnknown *,const void *,SIZE_T);
#endif
using Query=D3D12_RESOURCE_ALLOCATION_INFO *(WINAPI *)(D3D12_RESOURCE_ALLOCATION_INFO *,IUnknown *,UINT,UINT,const void *);
using Rich=D3D12_RESOURCE_ALLOCATION_INFO *(WINAPI *)(D3D12_RESOURCE_ALLOCATION_INFO *,IUnknown *,UINT,UINT,const void *,D3D12_RESOURCE_ALLOCATION_INFO1 *);
using Place=HRESULT(WINAPI*)(IUnknown *,UINT64,const void *,UINT,const void *,REFIID,void **);
static unsigned load(const char *name,BYTE (&bytes)[8192]) {
    FILE *f=fopen(name,"rb");CK(f);size_t n=fread(bytes,1,sizeof bytes,f);CK(n && feof(f));fclose(f);return n;
}
static ComPtr<IUnknown> checked_device(const D3D12_RESOURCE_DESC &rd) {
    constexpr UINT span=4<<20;
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
    return g;
}

int main() {
    constexpr UINT span=4<<20, pool=0x89700000,
#ifdef TEST_NV_DEPTH
        off=0xec00000;
#else
        off=0xe800000;
#endif
    D3D12_RESOURCE_DESC rd={};rd.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width=rd.Height=rd.DepthOrArraySize=rd.MipLevels=1;rd.SampleDesc.Count=4;
    rd.Format=DXGI_FORMAT_R32G8X24_TYPELESS;rd.Flags=D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    D3D12_CLEAR_VALUE cv={};cv.Format=DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
#ifdef TEST_NATIVE_POOL
    auto alloc=(void*(WINAPI*)(void*,SIZE_T,DWORD,UINT64,DWORD))(void*)GetProcAddress(LoadLibraryW(L"xmem.dll"),"XMemVirtualAlloc");CK(alloc);
    BYTE *wc=(BYTE*)alloc(nullptr,0x89700000,0x20003000,0x283,0x2404);CK(wc);
#endif
    auto g=checked_device(rd);auto place=(Place)(*(void***)g.Get())[0x428/8];
    ComPtr<ID3D12Device>d;HR(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&d)));
#ifndef TEST_NATIVE_POOL
    auto alloc=(void*(WINAPI*)(void*,SIZE_T,DWORD,UINT64,DWORD))(void*)GetProcAddress(LoadLibraryW(L"xmem.dll"),"XMemVirtualAlloc");CK(alloc);
    BYTE *wc=(BYTE*)alloc(nullptr,pool,0x20003000,0x283,0x2404);CK(wc);
#endif
    memset(wc+off-16,0xac,span+32);
#ifdef TEST_NATIVE_POOL
    auto get=(HRESULT(WINAPI*)(ID3D12Device*,const void*,SIZE_T,ID3D12Heap**,ID3D12Resource**,UINT*))(void*)GetProcAddress(GetModuleHandleW(L"xmem.dll"),"ScarlettAcquireNativePoolV1");
    UINT offset;ComPtr<ID3D12Heap>wcHeap;ComPtr<ID3D12Resource> backing;CK(get && get(d.Get(),wc+off,span,&wcHeap,&backing,&offset)==S_OK && offset==off);
    CK(wcHeap->GetDesc().SizeInBytes==pool && backing->GetDesc().Width==pool);
#else
    scarlett_section_info pin;HR(scarlett_query_section(0x53430005,wc+off,span,&pin));CK(pin.offset==off && pin.view_size==pool);
    auto open=(HRESULT(WINAPI*)(ID3D12Device*,SIZE_T,HANDLE,HANDLE*,ID3D12Heap**))(void*)GetProcAddress(GetModuleHandleW(L"d3d12core.dll"),"ScarlettWcHeapV1");CK(open);
    ComPtr<ID3D12Heap>wcHeap;HR(open(d.Get(),pool,pin.section,nullptr,wcHeap.GetAddressOf()));CK(wcHeap->GetDesc().Flags&D3D12_HEAP_FLAG_DENY_RT_DS_TEXTURES);
#endif
    ComPtr<ID3D12Resource>rejected;CK(d->CreatePlacedResource(wcHeap.Get(),off,&rd,D3D12_RESOURCE_STATE_DEPTH_WRITE,&cv,IID_PPV_ARGS(&rejected))==E_INVALIDARG && !rejected);
    BYTE *pg=(BYTE*)VirtualAlloc(nullptr,8192,MEM_RESERVE,PAGE_NOACCESS);CK(pg&&VirtualAlloc(pg,4096,MEM_COMMIT,PAGE_READWRITE));
    constexpr UINT n=offsetof(D3D12_CLEAR_VALUE,DepthStencil.Stencil)+1;BYTE *clear=pg+4096-n;memcpy(clear,&cv,n);
    auto attempt=[&](BYTE *va,UINT state,const void *c){void *obj=(void*)1;HRESULT hr=place(g.Get(),(UINT_PTR)va,&rd,state,c,__uuidof(ID3D12Resource),&obj);CK(!obj);return hr;};
    CK(attempt(wc+off+65536,6,clear)==E_INVALIDARG && attempt(wc+off,6,clear+1)==E_POINTER && attempt(wc+off,5,clear)==E_NOTIMPL);
#ifndef TEST_NATIVE_POOL
    auto query=(decltype(&NtQueryObject))(void*)GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"NtQueryObject");CK(query);
    auto count=[&](){OBJECT_BASIC_INFORMATION info={};CK(query(pin.section,ObjectBasicInformation,&info,sizeof info,nullptr)==0);return info.HandleCount;};
    ULONG before=count();HANDLE extra;CK(before && DuplicateHandle(GetCurrentProcess(),pin.section,GetCurrentProcess(),&extra,0,FALSE,DUPLICATE_SAME_ACCESS));
    CK(count()==before+1 && CloseHandle(extra) && count()==before);
#endif
#ifdef TEST_NV_DEPTH
    void *wrapper=nullptr; HR(place(g.Get(),(UINT_PTR)wc+off,&rd,6,clear,__uuidof(ID3D12Resource),&wrapper)); CK(wrapper);
    using Make=HRESULT(WINAPI*)(IUnknown*,const void*,REFIID,void**); using Start=SIZE_T(WINAPI*)(IUnknown*);
    using Srv=void(WINAPI*)(IUnknown*,IUnknown*,const void*,SIZE_T);
    UINT hd0[]={0,6,1,1};
    auto make_slots=[&](IUnknown *device){ComPtr<IUnknown> h; HR(((Make)(*(void***)device)[0x70/8])(device,hd0,__uuidof(ID3D12DescriptorHeap),(void**)h.GetAddressOf()));return h;};
    auto start=[](IUnknown *h){return ((Start)(*(void***)h)[0x48/8])(h);};
    auto slots=make_slots(g.Get()); SIZE_T slot=start(slots.Get())+2*32;
    DWORD view[8]={DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS,6,0x1688};
    BYTE *end=pg+4096-sizeof view;memcpy(end,view,sizeof view);
    auto obj=(IUnknown*)wrapper;
    ((Srv)(*(void***)g.Get())[0x90/8])(g.Get(),obj,end,slot);
    UINT64 tag[2];memcpy(tag,(void*)slot,sizeof tag);
    for(DWORD fmt:{19u,20u,22u,28u}) {view[0]=fmt;CK(FAILED(xs_write_view(g.Get(),0x90,obj,view,slot)));}
    view[0]=DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
    for(UINT i=1;i<8;i++) {view[i]^=(i==1?2:1);CK(FAILED(xs_write_view(g.Get(),0x90,obj,view,slot)));view[i]^=(i==1?2:1);}
    CK(xs_write_view(g.Get(),0x90,obj,end+1,slot)==E_POINTER && FAILED(xs_write_view(g.Get(),0x90,obj,view,slot+1)));
    auto foreign=checked_device(rd);auto foreign_slots=make_slots(foreign.Get());
    CK(FAILED(xs_write_view(foreign.Get(),0x90,obj,view,start(foreign_slots.Get()))));
    CK(FAILED(xs_write_view(g.Get(),0x90,obj,view,start(foreign_slots.Get()))) && !memcmp(tag,(void*)slot,sizeof tag));
    ID3D12DescriptorHeap *bank=nullptr;D3D12_GPU_DESCRIPTOR_HANDLE guest_gpu={};
    *(UINT64*)slot^=1;CK(FAILED(xs_resolve_view(slots.Get(),slot,0x90,&bank,&guest_gpu)) && !bank && !guest_gpu.ptr);*(UINT64*)slot^=1;
    HR(xs_resolve_view(slots.Get(),slot,0x90,&bank,&guest_gpu));CK(bank && guest_gpu.ptr);
    auto attachment_heap=[&](IUnknown *device,UINT type){UINT desc[]={type,type==3?2048u:4096u,0,1};ComPtr<IUnknown> h;
        HR(((Make)(*(void***)device)[0x70/8])(device,desc,__uuidof(ID3D12DescriptorHeap),(void**)h.GetAddressOf()));return h;};
    auto dsvs=attachment_heap(g.Get(),3),rtvs=attachment_heap(g.Get(),2),foreign_dsvs=attachment_heap(foreign.Get(),3);
    SIZE_T dsSlot=start(dsvs.Get())+32;DWORD dw[]={20,5,0,0,0,0};end=pg+4096-sizeof dw;memcpy(end,dw,sizeof dw);
    memset((void*)(dsSlot-32),0xcd,96);((Srv)(*(void***)g.Get())[0xa8/8])(g.Get(),obj,end,dsSlot);
    memcpy(tag,(void*)dsSlot,sizeof tag);
    for(auto &w:dw){w^=1;CK(FAILED(xs_write_view(g.Get(),0xa8,obj,dw,dsSlot)));w^=1;}
    for(DWORD fmt:{19u,21u,22u,28u}){dw[0]=fmt;CK(FAILED(xs_write_view(g.Get(),0xa8,obj,dw,dsSlot)));}dw[0]=20;
    CK(xs_write_view(g.Get(),0xa8,obj,end+1,dsSlot)==E_POINTER);
    CK(FAILED(xs_write_view(g.Get(),0xa8,obj,dw,dsSlot+1)) && FAILED(xs_write_view(g.Get(),0xa8,obj,dw,start(dsvs.Get())+2048*32)));
    CK(FAILED(xs_write_view(g.Get(),0xa8,obj,dw,slot)) && FAILED(xs_write_view(g.Get(),0xa8,obj,dw,start(rtvs.Get()))));
    CK(FAILED(xs_write_view(g.Get(),0xa8,obj,dw,start(foreign_dsvs.Get()))) && FAILED(xs_write_view(foreign.Get(),0xa8,obj,dw,start(foreign_dsvs.Get()))));
    auto cr=rd;cr.Format=DXGI_FORMAT_R8G8B8A8_UNORM;cr.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    D3D12_CLEAR_VALUE white={};white.Format=cr.Format;for(auto &c:white.Color)c=1;
    ComPtr<IUnknown> color;HR(place(g.Get(),(UINT_PTR)wc+off-span,&cr,6,&white,__uuidof(ID3D12Resource),(void**)color.GetAddressOf()));
    CK(FAILED(xs_write_view(g.Get(),0xa8,color.Get(),dw,dsSlot)));color.Reset();
    CK(!memcmp(tag,(void*)dsSlot,sizeof tag));for(int i=-32;i<64;i++)if(i<0 || i>=16)CK(((BYTE*)dsSlot)[i]==0xcd);
    D3D12_CPU_DESCRIPTOR_HANDLE middle={};ComPtr<ID3D12Resource> selected;
    *(UINT64*)dsSlot^=1;CK(FAILED(xs_resolve_depth_dsv(dsvs.Get(),dsSlot,&middle,selected.GetAddressOf())) && !middle.ptr && !selected);*(UINT64*)dsSlot^=1;
    memcpy(clear,&cv,n); // Clear and view prefixes share the guarded page.
    ((IUnknown*)wrapper)->Release(); CK(FAILED(attempt(wc+off,6,clear)));
    for(UINT i=0;i<span+32;i++) CK((wc+off-16)[i]==0xac);
#else
    for(unsigned i=0;i<64;i++)CK(attempt(wc+off,6,clear)==DXGI_ERROR_UNSUPPORTED);
    CK(count()==before);
#endif
#ifdef TEST_NATIVE_POOL
    wcHeap.Reset();CK(VirtualFree(wc,0,MEM_RELEASE));
#else
    scarlett_section_info alias;HR(scarlett_query_section_handle(0x53430004,nullptr,pool,&alias,pin.section));
    CK(CloseHandle(pin.section));wcHeap.Reset();CK(VirtualFree(wc,0,MEM_RELEASE));
#endif
    BYTE *reuse=wc+off;CK(VirtualAlloc(reuse,span,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE)==reuse);memset(reuse,0x37,span);
    CK(FAILED(attempt(reuse,6,clear)));CK(VirtualFree(pg,0,MEM_RELEASE));
#ifdef TEST_NV_DEPTH
    puts("PASS guest +428 D32/S8 import, duplicate refused, wrapper released, original WC intact at creation; stale VA refused");
#else
    puts("PASS +c8/+360 depth 4MiB/4MiB detail offset 0; marked +428 unsupported/null x64, stable handle count; reused VA refused");
#endif

    D3D12_HEAP_PROPERTIES hp={};hp.Type=D3D12_HEAP_TYPE_DEFAULT;hp.CreationNodeMask=hp.VisibleNodeMask=1;
    D3D12_HEAP_DESC hd={};hd.Properties=hp;hd.Alignment=span;hd.SizeInBytes=3*span;hd.Flags=D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES;
    ComPtr<ID3D12Heap> heap;HR(d->CreateHeap(&hd,IID_PPV_ARGS(&heap)));ComPtr<ID3D12Resource> ds[3],report,rb;
    for(UINT i=0;i<3;i++) {
#ifdef TEST_NV_DEPTH
        if(i==1) {HR(xs_resolve_depth_dsv(dsvs.Get(),dsSlot,&middle,ds[i].GetAddressOf()));CK(ds[i] && middle.ptr && middle.ptr!=dsSlot);auto desc=ds[i]->GetDesc();
            CK(desc.Format==rd.Format && desc.Flags==rd.Flags && desc.SampleDesc.Count==4);continue;}
#endif
        HR(d->CreatePlacedResource(heap.Get(),i*span,&rd,D3D12_RESOURCE_STATE_DEPTH_WRITE,&cv,IID_PPV_ARGS(&ds[i])));
    }
    heap.Reset();
    D3D12_DESCRIPTOR_HEAP_DESC dd={D3D12_DESCRIPTOR_HEAP_TYPE_DSV,3,D3D12_DESCRIPTOR_HEAP_FLAG_NONE,1};ComPtr<ID3D12DescriptorHeap> dh,sh;
    HR(d->CreateDescriptorHeap(&dd,IID_PPV_ARGS(&dh)));dd={D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,6,D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,1};
#ifdef TEST_NV_DEPTH
    sh=bank;
#else
    HR(d->CreateDescriptorHeap(&dd,IID_PPV_ARGS(&sh)));
#endif
    auto h=dh->GetCPUDescriptorHandleForHeapStart(),t=sh->GetCPUDescriptorHandleForHeapStart();UINT stride=d->GetDescriptorHandleIncrementSize(dd.Type);
    D3D12_CPU_DESCRIPTOR_HANDLE dsv[3];for(UINT i=0;i<3;i++) {
        D3D12_DEPTH_STENCIL_VIEW_DESC view={};view.Format=cv.Format;view.ViewDimension=D3D12_DSV_DIMENSION_TEXTURE2DMS;
#ifdef TEST_NV_DEPTH
        if(i==1)dsv[i]=middle;else
#endif
        {dsv[i]=h;d->CreateDepthStencilView(ds[i].Get(),&view,h);}h.ptr+=d->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
        D3D12_SHADER_RESOURCE_VIEW_DESC srv={};srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2DMS;srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        for(auto fmt:{DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS,DXGI_FORMAT_X32_TYPELESS_G8X24_UINT}) {
#ifdef TEST_NV_DEPTH
            if(i==1 && fmt==DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS) {t.ptr+=stride;continue;} // Already written by guest +90.
#endif
            srv.Format=fmt;d->CreateShaderResourceView(ds[i].Get(),&srv,t);t.ptr+=stride;
        }
    }
    D3D12_RESOURCE_DESC buf={};buf.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;buf.Height=buf.DepthOrArraySize=buf.MipLevels=buf.SampleDesc.Count=1;buf.Width=128;buf.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;buf.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    HR(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&buf,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&report)));hp.Type=D3D12_HEAP_TYPE_READBACK;buf.Flags=D3D12_RESOURCE_FLAG_NONE;
    HR(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&buf,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&rb)));
    D3D12_DESCRIPTOR_RANGE range={D3D12_DESCRIPTOR_RANGE_TYPE_SRV,2,0,0,0};D3D12_ROOT_PARAMETER par[2]={};par[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;par[0].DescriptorTable={1,&range};par[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV;
    D3D12_ROOT_SIGNATURE_DESC rs={2,par,0,nullptr,D3D12_ROOT_SIGNATURE_FLAG_NONE};ComPtr<ID3DBlob>blob;HR(D3D12SerializeRootSignature(&rs,D3D_ROOT_SIGNATURE_VERSION_1,&blob,nullptr));ComPtr<ID3D12RootSignature>root;HR(d->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&root)));
    BYTE vs[8192],ps[8192],cs[8192];D3D12_GRAPHICS_PIPELINE_STATE_DESC gp={};gp.pRootSignature=root.Get();gp.VS={vs,load("vs.dxil",vs)};gp.PS={ps,load("ps.dxil",ps)};
    gp.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;gp.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;gp.RasterizerState.DepthClipEnable=TRUE;gp.RasterizerState.MultisampleEnable=TRUE;
    gp.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;gp.SampleDesc=rd.SampleDesc;gp.DSVFormat=cv.Format;
    auto &st=gp.DepthStencilState;st.DepthEnable=st.StencilEnable=TRUE;st.DepthWriteMask=D3D12_DEPTH_WRITE_MASK_ALL;st.DepthFunc=D3D12_COMPARISON_FUNC_ALWAYS;st.StencilReadMask=st.StencilWriteMask=0xff;st.FrontFace=st.BackFace={D3D12_STENCIL_OP_KEEP,D3D12_STENCIL_OP_KEEP,D3D12_STENCIL_OP_REPLACE,D3D12_COMPARISON_FUNC_ALWAYS};
    ComPtr<ID3D12PipelineState> draws[4],compute;for(UINT s=0;s<4;s++){gp.SampleMask=1u<<s;HR(d->CreateGraphicsPipelineState(&gp,IID_PPV_ARGS(&draws[s])));}
#ifdef TEST_NV_DEPTH
    ComPtr<ID3D12PipelineState> mixed,never;gp.SampleMask=15;st.DepthFunc=D3D12_COMPARISON_FUNC_LESS;
    HR(d->CreateGraphicsPipelineState(&gp,IID_PPV_ARGS(&mixed)));st.DepthFunc=D3D12_COMPARISON_FUNC_ALWAYS;
    st.FrontFace.StencilFunc=st.BackFace.StencilFunc=D3D12_COMPARISON_FUNC_NEVER;HR(d->CreateGraphicsPipelineState(&gp,IID_PPV_ARGS(&never)));
#endif
    D3D12_COMPUTE_PIPELINE_STATE_DESC cp={};cp.pRootSignature=root.Get();cp.CS={cs,load("cs.dxil",cs)};HR(d->CreateComputePipelineState(&cp,IID_PPV_ARGS(&compute)));
    D3D12_COMMAND_QUEUE_DESC qd={};ComPtr<ID3D12CommandQueue>q;ComPtr<ID3D12CommandAllocator>a;ComPtr<ID3D12GraphicsCommandList>l;HR(d->CreateCommandQueue(&qd,IID_PPV_ARGS(&q)));HR(d->CreateCommandAllocator(qd.Type,IID_PPV_ARGS(&a)));HR(d->CreateCommandList(0,qd.Type,a.Get(),nullptr,IID_PPV_ARGS(&l)));
    float depths[]={1,0,0.25f};UINT stencils[]={0x71,0,0xe6};for(UINT i=0;i<3;i++) l->ClearDepthStencilView(dsv[i],D3D12_CLEAR_FLAG_DEPTH|D3D12_CLEAR_FLAG_STENCIL,depths[i],stencils[i],0,nullptr);
    D3D12_VIEWPORT vp={0,0,1,1,0,1};D3D12_RECT rect={0,0,1,1};l->RSSetViewports(1,&vp);l->RSSetScissorRects(1,&rect);l->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);l->SetGraphicsRootSignature(root.Get());l->OMSetRenderTargets(0,nullptr,FALSE,&dsv[1]);
    auto transition=[&](ID3D12Resource *r,D3D12_RESOURCE_STATES x,D3D12_RESOURCE_STATES y){D3D12_RESOURCE_BARRIER b={};b.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,x,y};l->ResourceBarrier(1,&b);};
#ifdef TEST_NV_DEPTH
    // LESS against 5/16 accepts samples 0,1 only. A stencil-failing draw must preserve all four.
    l->ClearDepthStencilView(middle,D3D12_CLEAR_FLAG_DEPTH|D3D12_CLEAR_FLAG_STENCIL,0.3125f,0x66,0,nullptr);
    l->SetPipelineState(mixed.Get());l->OMSetStencilRef(0xa2);l->DrawInstanced(3,1,0,0);
    l->SetPipelineState(never.Get());l->OMSetStencilRef(0xf3);l->DrawInstanced(3,1,0,0);
    l->OMSetRenderTargets(0,nullptr,FALSE,nullptr);transition(ds[1].Get(),D3D12_RESOURCE_STATE_DEPTH_WRITE,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    l->SetPipelineState(compute.Get());l->SetComputeRootSignature(root.Get());l->SetDescriptorHeaps(1,&bank);l->SetComputeRootDescriptorTable(0,guest_gpu);
    l->SetComputeRootUnorderedAccessView(1,report->GetGPUVirtualAddress()+96);l->Dispatch(1,1,1);
    transition(ds[1].Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_DEPTH_WRITE);l->OMSetRenderTargets(0,nullptr,FALSE,&middle);
#endif
    for(UINT s=0;s<4;s++){l->SetPipelineState(draws[s].Get());l->OMSetStencilRef(0x31+s*11);l->DrawInstanced(3,1,0,0);}

    l->SetPipelineState(compute.Get());l->SetComputeRootSignature(root.Get());ID3D12DescriptorHeap *heaps[]={sh.Get()};l->SetDescriptorHeaps(1,heaps);auto gpu=sh->GetGPUDescriptorHandleForHeapStart();
    for(UINT i=0;i<3;i++){
#ifdef TEST_NV_DEPTH
        if(i==1){CK(gpu.ptr==guest_gpu.ptr);gpu=guest_gpu;}
#endif
        transition(ds[i].Get(),D3D12_RESOURCE_STATE_DEPTH_WRITE,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);l->SetComputeRootDescriptorTable(0,gpu);gpu.ptr+=2*stride;l->SetComputeRootUnorderedAccessView(1,report->GetGPUVirtualAddress()+32*i);l->Dispatch(1,1,1);}
#ifdef TEST_NV_DEPTH
    ds[1].Reset(); // The guest descriptor now owns the image through submission and retirement.
#endif
    transition(report.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);l->CopyBufferRegion(rb.Get(),0,report.Get(),0,buf.Width);HR(l->Close());ID3D12CommandList *ls[]={l.Get()};q->ExecuteCommandLists(1,ls);
    ComPtr<ID3D12Fence>f;HR(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&f)));HR(q->Signal(f.Get(),1));HANDLE e=CreateEventW(nullptr,FALSE,FALSE,nullptr);CK(e);HR(f->SetEventOnCompletion(1,e));CK(WaitForSingleObject(e,10000)==WAIT_OBJECT_0);CloseHandle(e);HR(d->GetDeviceRemovedReason());
    DWORD *m;HR(rb->Map(0,nullptr,(void**)&m));for(UINT i=0;i<3;i++) for(UINT s=0;s<4;s++) {DWORD bits=m[8*i+2*s],stencil=m[8*i+2*s+1];float depth;memcpy(&depth,&bits,4);printf("placement %u sample %u depth=%g stencil=%02lx\n",i,s,depth,stencil);CK(depth==(i==1?(s+1)*0.125f:depths[i]) && stencil==(i==1?0x31+s*11:stencils[i]));}
#ifdef TEST_NV_DEPTH
    for(UINT s=0;s<4;s++){float z;memcpy(&z,m+24+s*2,4);DWORD st=m[25+s*2];printf("mixed sample %u depth=%g stencil=%02lx\n",s,z,st);
        CK(z==(s<2?(s+1)*0.125f:0.3125f) && st==(s<2?0xa2u:0x66u));}
#endif
    rb->Unmap(0,nullptr);
#ifdef TEST_NATIVE_POOL
    /* The retained buffer reads the actual pool, independent of the released title VA. */
    ComPtr<ID3D12Resource> snapshot;buf.Width=span+32;HR(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&buf,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&snapshot)));
    HR(a->Reset());HR(l->Reset(a.Get(),nullptr));l->CopyBufferRegion(snapshot.Get(),0,backing.Get(),off-16,span+32);HR(l->Close());q->ExecuteCommandLists(1,ls);HR(q->Signal(f.Get(),2));HR(f->SetEventOnCompletion(2,nullptr));HR(d->GetDeviceRemovedReason());
    BYTE *raw;HR(snapshot->Map(0,nullptr,(void**)&raw));UINT changed=0;for(UINT i=0;i<16;i++)CK(raw[i]==0xac && raw[16+span+i]==0xac);
    for(UINT i=0;i<span;i++)changed+=raw[16+i]!=0xac;
    CK(changed>=16);printf("PASS retained native pool GPU readback after public release: %u bytes changed; both guards intact\n",changed);snapshot->Unmap(0,nullptr);
#elif defined(TEST_NV_DEPTH)
    BYTE *slice=(BYTE*)alias.base+off;
    for(UINT i=0;i<16;i++)CK(slice[(int)i-16]==0xac && slice[span+i]==0xac);
    auto verify_alias=[&](){
        BYTE expected[0x206];memset(expected,0xac,sizeof expected);
        for(UINT s=0;s<4;s++) {UINT x=(s/2)*4+s%2;float z=(s+1)*0.125f;memcpy(expected+4*x,&z,4);expected[0x200+x]=0x31+s*11;}
        CK(!memcmp(slice,expected,sizeof expected));for(UINT i=sizeof expected;i<span;i++)CK(slice[i]==0xac);
    };
    verify_alias();puts("PASS original 4MiB alias: four exact float words at +0,4,10,14 (hex), stencil at +200,201,204,205; all other bytes unchanged");
    // End every native consumer before attempting to reuse the same physical slice.
    sh.Reset();dh.Reset();l.Reset();a.Reset();q.Reset();void *again=nullptr;
    CK(FAILED(attempt(slice,6,&cv)));
    DWORD null_srv[10]={42,4,0x1688};HR(xs_write_view(g.Get(),0x90,nullptr,null_srv,slot));
    CK(FAILED(attempt(slice,6,&cv)));dsvs.Reset();
    HR(place(g.Get(),(UINT_PTR)slice,&rd,6,&cv,__uuidof(ID3D12Resource),&again));CK(again);((IUnknown*)again)->Release();
    verify_alias();puts("PASS guest +a8 DSV retained original after fence and SRV overwrite; DSV heap release allowed recreation with all four samples preserved");
#else
    for(UINT i=0;i<span+32;i++)CK(((BYTE*)alias.base)[off-16+i]==0xac);
#endif
    for(UINT i=0;i<span;i++)CK(reuse[i]==0x37);
    CK(VirtualFree(reuse,0,MEM_RELEASE));
#ifndef TEST_NATIVE_POOL
    CK(VirtualFree(alias.base,0,MEM_RELEASE));
#endif
    puts("PASS native per-sample depth/stencil and both neighboring placements; original allocation guards and replacement VA intact");
}
