#include <d3d12.h>
#include <wrl/client.h>
#include <cstdio>
#include <cstring>
#include "xmem/marked_section.h"
extern "C" HRESULT xs_write_view(IUnknown*,UINT,IUnknown*,const void*,SIZE_T);
extern "C" HRESULT xs_resolve_view(IUnknown*,SIZE_T,UINT,ID3D12DescriptorHeap**,D3D12_GPU_DESCRIPTOR_HANDLE*);
extern "C" HRESULT xs_resolve_color_rtv(IUnknown*,SIZE_T,D3D12_CPU_DESCRIPTOR_HANDLE*,ID3D12Resource**);
using Microsoft::WRL::ComPtr;
extern "C" HRESULT WINAPI D3D12XboxCreateDevice(void *,const void *,REFIID,void **);
extern "C" HRESULT xs_color_allocation_info(IUnknown *,UINT,UINT,const void *,D3D12_RESOURCE_ALLOCATION_INFO *);
extern "C" HRESULT xs_msaa_allocation_info1(IUnknown *,UINT,UINT,const void *,D3D12_RESOURCE_ALLOCATION_INFO *,D3D12_RESOURCE_ALLOCATION_INFO1 *);
#define CK(x) do{if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);return 1;}}while(0)
#define HR(x) CK(SUCCEEDED(x))
#include "test-float-neighbors.hpp"
static int check(UINT off) {
    DWORD params[20]={};params[0]=0x20004;params[2]=0x100000;params[10]=1;params[15]=0x20;
    const IID boot={0x5405c344,0xd457,0x444e,{0xb4,0xdd,0x23,0x66,0xe4,0x5a,0xee,0x39}};
    ComPtr<IUnknown> dev;HR(D3D12XboxCreateDevice(nullptr,params,boot,(void**)dev.GetAddressOf()));
    auto vt=*(void***)dev.Get();
    constexpr SIZE_T pool=0x89700000,span=0x1e00000;
    using Alloc=void*(WINAPI*)(void*,SIZE_T,DWORD,UINT64,DWORD);
    auto alloc=(Alloc)(void*)GetProcAddress(LoadLibraryW(L"xmem.dll"),"XMemVirtualAlloc");CK(alloc);
    auto wc=(BYTE*)alloc(nullptr,pool,0x20003000,0x283,0x2404);CK(wc);
    if(off==0xf800000)CK(!check_neighbors(dev.Get(),wc,false));
    scarlett_section_info pin,alias;HR(scarlett_query_section(0x53430005,wc+off,span,&pin));CK(pin.offset==off && pin.view_size==pool);
    HR(scarlett_query_section_handle(0x53430004,nullptr,pool,&alias,pin.section));CloseHandle(pin.section);
    BYTE *old=(BYTE*)alias.base+off;memset(wc+off-65536,0xa7,span+2*65536);
    using Place=HRESULT(WINAPI*)(IUnknown*,UINT64,const void*,UINT,const void*,REFIID,void**);
    DWORD w[]={3,0,0,0,2560,0,1440,0x10001,10,1,0,0,0x300005};D3D12_CLEAR_VALUE cv={};cv.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;
    auto place=[&](void* where,UINT state,REFIID iid,IUnknown** out){return ((Place)vt[0x428/8])(dev.Get(),(UINT_PTR)where,w,state,&cv,iid,(void**)out);};
    ComPtr<IUnknown> obj,rtvHeap,srvHeap; IUnknown *reject=(IUnknown*)1;
    CK(FAILED(place(wc+off+65536,3,IID_ID3D12Resource,&reject))&&!reject);
    CK(FAILED(place(wc+off,3,IID_IUnknown,&reject))&&!reject);
    CK(FAILED(place(wc+off,5,IID_ID3D12Resource,&reject))&&!reject);
    cv.Color[3]=1;CK(FAILED(place(wc+off,3,IID_ID3D12Resource,&reject))&&!reject);cv.Color[3]=0;
    for(UINT k=0;k<13;k++)if(k!=1){w[k]^=1;CK(FAILED(place(wc+off,3,IID_ID3D12Resource,&reject))&&!reject);w[k]^=1;}
    HR(place(wc+off,3,IID_ID3D12Resource,obj.GetAddressOf()));
    CK(FAILED(place(wc+off,3,IID_ID3D12Resource,&reject))&&!reject);
    using Heap=HRESULT(WINAPI*)(IUnknown*,const void*,REFIID,void**);using Start=SIZE_T(WINAPI*)(IUnknown*);
    using Writer=void(WINAPI*)(IUnknown*,IUnknown*,const void*,SIZE_T);
    UINT r[]={2,4096,0,1},sh[]={0,4,1,1};
    HR(((Heap)vt[0x70/8])(dev.Get(),r,IID_ID3D12DescriptorHeap,(void**)rtvHeap.GetAddressOf()));
    HR(((Heap)vt[0x70/8])(dev.Get(),sh,IID_ID3D12DescriptorHeap,(void**)srvHeap.GetAddressOf()));
    SIZE_T rt=((Start)((*(void***)rtvHeap.Get())[0x48/8]))(rtvHeap.Get())+32;
    SIZE_T ss=((Start)((*(void***)srvHeap.Get())[0x48/8]))(srvHeap.Get())+32;
    DWORD rw[]={10,4,0,0},sw[]={10,4,0x1688,0,0,1,0,0};
    ((Writer)vt[0xa0/8])(dev.Get(),obj.Get(),rw,rt);((Writer)vt[0x90/8])(dev.Get(),obj.Get(),sw,ss);
    UINT64 stamp[2];memcpy(stamp,(void*)ss,16);
    for(auto &x:sw){x^=1;CK(FAILED(xs_write_view(dev.Get(),0x90,obj.Get(),sw,ss)));x^=1;}
    CK(!memcmp(stamp,(void*)ss,16));memcpy(stamp,(void*)rt,16);
    for(auto &x:rw){x^=1;CK(FAILED(xs_write_view(dev.Get(),0xa0,obj.Get(),rw,rt)));x^=1;}
    CK(!memcmp(stamp,(void*)rt,16));
    UINT64 before=*(UINT64*)ss;sw[0]=28;CK(FAILED(xs_write_view(dev.Get(),0x90,obj.Get(),sw,ss))&&before==*(UINT64*)ss);sw[0]=10;
    rw[0]=28;CK(FAILED(xs_write_view(dev.Get(),0xa0,obj.Get(),rw,rt)));rw[0]=10;
    SIZE_T us=ss+32;DWORD uw[]={10,4,0,0};using Uav=void(WINAPI*)(IUnknown*,IUnknown*,IUnknown*,const void*,SIZE_T);
    ((Uav)vt[0x98/8])(dev.Get(),obj.Get(),nullptr,uw,us);UINT64 utag[2];memcpy(utag,(void*)us,16);
    for(auto &x:uw){x^=1;CK(FAILED(xs_write_view(dev.Get(),0x98,obj.Get(),uw,us)));x^=1;}CK(!memcmp(utag,(void*)us,16));
    ID3D12DescriptorHeap *ub;D3D12_GPU_DESCRIPTOR_HANDLE ug;HR(xs_resolve_view(srvHeap.Get(),us,0x98,&ub,&ug));
    CK(!memcmp((void*)ss,&before,8) && memcmp((void*)us,(void*)ss,16)); // UAV write left adjacent SRV intact
    ID3D12DescriptorHeap *bank;D3D12_GPU_DESCRIPTOR_HANDLE gpu;HR(xs_resolve_view(srvHeap.Get(),ss,0x90,&bank,&gpu));
    ComPtr<ID3D12Resource> image,rb;D3D12_CPU_DESCRIPTOR_HANDLE view;HR(xs_resolve_color_rtv(rtvHeap.Get(),rt,&view,image.GetAddressOf()));
    ComPtr<ID3D12Device>d;HR(image->GetDevice(IID_PPV_ARGS(&d)));obj.Reset();CK(ub==bank);
    for(int j=-65536;j<(int)span+65536;j++)CK(old[j]==0xa7);
    CK(VirtualFree(wc,0,MEM_RELEASE));auto replacement=(BYTE*)VirtualAlloc(wc+off,span,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    CK(replacement==wc+off);memset(replacement,0xbd,span);CK(FAILED(place(replacement,3,IID_ID3D12Resource,&reject))&&!reject);
    D3D12_RESOURCE_DESC buf={};buf.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;buf.Width=256;
    buf.Height=buf.DepthOrArraySize=buf.MipLevels=buf.SampleDesc.Count=1;buf.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    D3D12_HEAP_PROPERTIES hp={};hp.VisibleNodeMask=hp.CreationNodeMask=1;hp.Type=D3D12_HEAP_TYPE_READBACK;HR(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&buf,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&rb)));
    ComPtr<ID3D12CommandQueue> q;ComPtr<ID3D12CommandAllocator> a;ComPtr<ID3D12GraphicsCommandList> l;D3D12_COMMAND_QUEUE_DESC qd={};
    HR(d->CreateCommandQueue(&qd,IID_PPV_ARGS(&q)));HR(d->CreateCommandAllocator(qd.Type,IID_PPV_ARGS(&a)));HR(d->CreateCommandList(0,qd.Type,a.Get(),nullptr,IID_PPV_ARGS(&l)));
    float dark[]={1,0,1,0.25f},bright[]={0,1,0,0.75f};D3D12_RECT corner={2559,1439,2560,1440};
    D3D12_RESOURCE_BARRIER init={};init.Transition={image.Get(),0,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_RENDER_TARGET};l->ResourceBarrier(1,&init);
    l->ClearRenderTargetView(view,dark,0,nullptr);l->ClearRenderTargetView(view,bright,1,&corner);
    BYTE dxil[8192];auto file=fopen("fp.dxil","rb");CK(file);auto bytes=fread(dxil,1,sizeof dxil,file);CK(bytes && feof(file));fclose(file);
    ComPtr<ID3D12RootSignature> root;HR(d->CreateRootSignature(0,dxil,bytes,IID_PPV_ARGS(&root)));
    D3D12_COMPUTE_PIPELINE_STATE_DESC pd={};pd.pRootSignature=root.Get();pd.CS={dxil,bytes};ComPtr<ID3D12PipelineState> pso;HR(d->CreateComputePipelineState(&pd,IID_PPV_ARGS(&pso)));
    hp.Type=D3D12_HEAP_TYPE_DEFAULT;buf.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;ComPtr<ID3D12Resource> result;
    HR(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&buf,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&result)));
    D3D12_RESOURCE_BARRIER b={};b.Transition={image.Get(),0,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_UNORDERED_ACCESS};l->ResourceBarrier(1,&b);
    BYTE blob[8192];auto uf=fopen("fp-uav.dxil","rb");CK(uf);auto nb=fread(blob,1,sizeof blob,uf);CK(nb && feof(uf));fclose(uf);
    ComPtr<ID3D12RootSignature> ur;HR(d->CreateRootSignature(0,blob,nb,IID_PPV_ARGS(&ur)));ComPtr<ID3D12PipelineState> up;
    pd.pRootSignature=ur.Get();pd.CS={blob,nb};HR(d->CreateComputePipelineState(&pd,IID_PPV_ARGS(&up)));
    l->SetDescriptorHeaps(1,&bank);l->SetComputeRootSignature(ur.Get());l->SetPipelineState(up.Get());l->SetComputeRootDescriptorTable(0,ug);l->Dispatch(1,1,1);
    b.Transition={image.Get(),0,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE};l->ResourceBarrier(1,&b);
    l->SetDescriptorHeaps(1,&bank);l->SetComputeRootSignature(root.Get());l->SetPipelineState(pso.Get());l->SetComputeRootDescriptorTable(0,gpu);l->SetComputeRootUnorderedAccessView(1,result->GetGPUVirtualAddress());l->Dispatch(1,1,1);
    b.Transition={result.Get(),0,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE};l->ResourceBarrier(1,&b);l->CopyBufferRegion(rb.Get(),64,result.Get(),0,8);
    b.Transition={image.Get(),0,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE};l->ResourceBarrier(1,&b);
    D3D12_TEXTURE_COPY_LOCATION src={},dst={};src.pResource=image.Get();dst.pResource=rb.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint.Footprint={DXGI_FORMAT_R16G16B16A16_FLOAT,2,1,1,256};D3D12_BOX box={2558,1439,0,2560,1440,1};
    l->CopyTextureRegion(&dst,0,0,0,&src,&box);HR(l->Close());ID3D12CommandList *lists[]={l.Get()};q->ExecuteCommandLists(1,lists);
    ComPtr<ID3D12Fence> fence;HR(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));HR(q->Signal(fence.Get(),1));
    HANDLE e=CreateEventW(nullptr,FALSE,FALSE,nullptr);CK(e);HR(fence->SetEventOnCompletion(1,e));CK(WaitForSingleObject(e,10000)==WAIT_OBJECT_0);CloseHandle(e);
    BYTE *p;HR(rb->Map(0,nullptr,(void**)&p));UINT64 darkbits=0x34003c0000003c00ull,brightbits=0x38003c003c000000ull;
    CK(((UINT64*)p)[0]==darkbits && ((UINT64*)p)[1]==brightbits && ((DWORD*)(p+64))[0]==0x40ff00ff && ((DWORD*)(p+64))[1]==0x80ffff00);rb->Unmap(0,nullptr);
    UINT hits=0,lights=0;for(UINT j=0;j<span/8;j++){CK(((UINT64*)replacement)[j]==0xbdbdbdbdbdbdbdbdull);hits+=((UINT64*)old)[j]==darkbits;lights+=((UINT64*)old)[j]==brightbits;}
    printf("FLOAT1440 mapped color counts dark=%u bright=%u pixels=%u\n",hits,lights,2560*1440);CK(hits==2560u*1440u-1 && lights==1);
    for(UINT j=0;j<65536;j++)CK(old[(int)j-65536]==0xa7 && old[span+j]==0xa7);
    CK(VirtualFree(replacement,0,MEM_RELEASE) && VirtualFree(alias.base,0,MEM_RELEASE));printf("PASS FLOAT1440 +%x original WC, post-VA-reuse RTV/UAV/shader SRV, adjacent guards, replacement and overlap rejection\n",off);return 0;
}
int main(){return check(0xf000000) || check(0xf800000) || check(0x10800000) || check(0x11800000);}
