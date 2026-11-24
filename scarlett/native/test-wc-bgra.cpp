#include <vector>
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
#define CK(x) do{if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);return 1;}}while(0)
#define HR(x) CK(SUCCEEDED(x))
int main() {
    DWORD params[20]={};params[0]=0x20004;params[2]=0x100000;params[10]=1;params[15]=0x20;
    const IID boot={0x5405c344,0xd457,0x444e,{0xb4,0xdd,0x23,0x66,0xe4,0x5a,0xee,0x39}};
    constexpr SIZE_T pool=0x89700000,guard=65536;
#ifdef TEST_RADV
    constexpr SIZE_T off=0xf880000,span=0x1010000;
#else
    constexpr SIZE_T off=0xf800000,span=0x1000000;
#endif
    using Alloc=void*(WINAPI*)(void*,SIZE_T,DWORD,UINT64,DWORD);
    auto alloc=(Alloc)(void*)GetProcAddress(LoadLibraryW(L"xmem.dll"),"XMemVirtualAlloc");CK(alloc);
    auto wc=(BYTE*)alloc(nullptr,pool,0x20003000,0x283,0x2404);CK(wc);
    ComPtr<IUnknown> dev;HR(D3D12XboxCreateDevice(nullptr,params,boot,(void**)dev.GetAddressOf()));
    auto vt=*(void***)dev.Get();
#ifdef TEST_RADV
    ComPtr<ID3D12Device> owner;HR(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&owner)));
    auto take=(HRESULT(WINAPI*)(ID3D12Device*,const void*,SIZE_T,ID3D12Heap**,ID3D12Resource**,UINT*))(void*)GetProcAddress(GetModuleHandleW(L"xmem.dll"),"ScarlettAcquireNativePoolV1");
    UINT offset=0;ComPtr<ID3D12Resource> backing;
    CK(take && take(owner.Get(),wc+off,2*span,nullptr,backing.GetAddressOf(),&offset)==S_OK && offset==off);
    BYTE *old=wc+off;
#else
    scarlett_section_info pin,alias;HR(scarlett_query_section(0x53430005,wc+off,2*span,&pin));CK(pin.offset==off && pin.view_size==pool);
    HR(scarlett_query_section_handle(0x53430004,nullptr,pool,&alias,pin.section));CloseHandle(pin.section);
    BYTE *old=(BYTE*)alias.base+off;
#endif
    memset(wc+off-guard,0xa7,2*span+2*guard);MemoryBarrier();
    using Place=HRESULT(WINAPI*)(IUnknown*,UINT64,const void*,UINT,const void*,REFIID,void**);
    DWORD w[]={3,0,0,0,2048,0,2048,0x10001,87,1,0,0,0x300001};D3D12_CLEAR_VALUE cv={};cv.Format=DXGI_FORMAT_B8G8R8A8_UNORM;
    auto place=[&](void* where,UINT state,REFIID iid,IUnknown** out){return ((Place)vt[0x428/8])(dev.Get(),(UINT_PTR)where,w,state,&cv,iid,(void**)out);};
    ComPtr<IUnknown> rtvHeap,srvHeap,obj[2];IUnknown *reject=nullptr;
    CK(FAILED(place(wc+off+guard,6,IID_ID3D12Resource,&reject)) && !reject);
    CK(FAILED(place(wc+off,5,IID_ID3D12Resource,&reject)) && !reject);
    cv.Color[2]=1;CK(FAILED(place(wc+off,6,IID_ID3D12Resource,&reject)) && !reject);cv.Color[2]=0;
    using Heap=HRESULT(WINAPI*)(IUnknown*,const void*,REFIID,void**);using Start=SIZE_T(WINAPI*)(IUnknown*);
    using Writer=void(WINAPI*)(IUnknown*,IUnknown*,const void*,SIZE_T);
    UINT r[]={2,4096,0,1},sh[]={0,4,1,1};
    HR(((Heap)vt[0x70/8])(dev.Get(),r,IID_ID3D12DescriptorHeap,(void**)rtvHeap.GetAddressOf()));
    HR(((Heap)vt[0x70/8])(dev.Get(),sh,IID_ID3D12DescriptorHeap,(void**)srvHeap.GetAddressOf()));
    SIZE_T rt=((Start)((*(void***)rtvHeap.Get())[0x48/8]))(rtvHeap.Get()), ss=((Start)((*(void***)srvHeap.Get())[0x48/8]))(srvHeap.Get());
    ComPtr<ID3D12Resource> image[2],rb;D3D12_CPU_DESCRIPTOR_HANDLE view[2];D3D12_GPU_DESCRIPTOR_HANDLE gpu[2];ID3D12DescriptorHeap *bank=nullptr;
    for(UINT j=0;j<2;j++) {
        HR(place(wc+off+j*span,6,IID_ID3D12Resource,obj[j].GetAddressOf()));
        CK(FAILED(place(wc+off+j*span,6,IID_ID3D12Resource,&reject)) && !reject);
        DWORD rw[]={87,4,0,0},sw[]={87,4,0x1688,0,0,1,0,0};
        ((Writer)vt[0xa0/8])(dev.Get(),obj[j].Get(),rw,rt+32*j);((Writer)vt[0x90/8])(dev.Get(),obj[j].Get(),sw,ss+32*j);
        HR(xs_resolve_view(srvHeap.Get(),ss+32*j,0x90,&bank,gpu+j));HR(xs_resolve_color_rtv(rtvHeap.Get(),rt+32*j,view+j,image[j].GetAddressOf()));
        sw[0]=65;CK(FAILED(xs_write_view(dev.Get(),0x90,obj[j].Get(),sw,ss+32*j)));
        rw[0]=28;CK(FAILED(xs_write_view(dev.Get(),0xa0,obj[j].Get(),rw,rt+32*j)));obj[j].Reset();
    }
    CK(image[0].Get()!=image[1].Get());ComPtr<ID3D12Device>d;HR(image[0]->GetDevice(IID_PPV_ARGS(&d)));
    constexpr UINT pixels=2048*2048,rawAt=512+pixels*4,whole=2*span+2*guard;
    for(UINT k=0;k<whole;k++)CK((old-guard)[k]==0xa7);
    CK(VirtualFree(wc,0,MEM_RELEASE) && VirtualAlloc(wc,pool,MEM_RESERVE,PAGE_NOACCESS)==wc);
    auto replacement=wc+off-guard;CK(VirtualAlloc(replacement,whole,MEM_COMMIT,PAGE_READWRITE)==replacement);
    memset(replacement,0xbd,whole);for(UINT j=0;j<2;j++)CK(FAILED(place(wc+off+j*span,6,IID_ID3D12Resource,&reject)) && !reject);
    D3D12_RESOURCE_DESC buf={};buf.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;buf.Width=rawAt+whole;
    buf.Height=buf.DepthOrArraySize=buf.MipLevels=buf.SampleDesc.Count=1;buf.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    D3D12_HEAP_PROPERTIES hp={};hp.VisibleNodeMask=hp.CreationNodeMask=1;hp.Type=D3D12_HEAP_TYPE_READBACK;HR(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&buf,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&rb)));
    ComPtr<ID3D12CommandQueue> q;ComPtr<ID3D12CommandAllocator> a;ComPtr<ID3D12GraphicsCommandList> l;D3D12_COMMAND_QUEUE_DESC qd={};
    HR(d->CreateCommandQueue(&qd,IID_PPV_ARGS(&q)));HR(d->CreateCommandAllocator(qd.Type,IID_PPV_ARGS(&a)));HR(d->CreateCommandList(0,qd.Type,a.Get(),nullptr,IID_PPV_ARGS(&l)));
    BYTE dxil[8192];auto file=fopen("bgra.dxil","rb");CK(file);auto bytes=fread(dxil,1,sizeof dxil,file);CK(bytes && feof(file));fclose(file);
    ComPtr<ID3D12RootSignature> root;HR(d->CreateRootSignature(0,dxil,bytes,IID_PPV_ARGS(&root)));
    D3D12_COMPUTE_PIPELINE_STATE_DESC pd={};pd.pRootSignature=root.Get();pd.CS={dxil,bytes};ComPtr<ID3D12PipelineState> pso;HR(d->CreateComputePipelineState(&pd,IID_PPV_ARGS(&pso)));
    hp.Type=D3D12_HEAP_TYPE_DEFAULT;buf.Width=256;buf.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;ComPtr<ID3D12Resource> result;
    HR(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&buf,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&result)));
    ComPtr<ID3D12Fence> fence;HR(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));
    HANDLE e=CreateEventW(nullptr,FALSE,FALSE,nullptr);CK(e);std::vector<BYTE> first(span);
    const float colors[2][4]={{1,0,0.25f,0.25f},{0,1,0.75f,0.75f}};
    const UINT rgba[]={0x404000ff,0xbfbfff00},bgra[]={0x40ff0040,0xbf00ffbf};
    for(UINT j=0;j<2;j++) {
        if(j){HR(a->Reset());HR(l->Reset(a.Get(),nullptr));}
        D3D12_RECT corner={2047,2047,2048,2048};l->ClearRenderTargetView(view[j],colors[j],0,nullptr);l->ClearRenderTargetView(view[j],colors[1-j],1,&corner);
        D3D12_RESOURCE_BARRIER b={};b.Transition={image[j].Get(),0,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE};l->ResourceBarrier(1,&b);
        if(j){b.Transition={result.Get(),0,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS};l->ResourceBarrier(1,&b);}
        l->SetDescriptorHeaps(1,&bank);l->SetComputeRootSignature(root.Get());l->SetPipelineState(pso.Get());l->SetComputeRootDescriptorTable(0,gpu[j]);l->SetComputeRootUnorderedAccessView(1,result->GetGPUVirtualAddress());l->Dispatch(1,1,1);
        b.Transition={result.Get(),0,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE};l->ResourceBarrier(1,&b);l->CopyBufferRegion(rb.Get(),0,result.Get(),0,8);
        b.Transition={image[j].Get(),0,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE};l->ResourceBarrier(1,&b);
        D3D12_TEXTURE_COPY_LOCATION src={},dst={};src.pResource=image[j].Get();dst.pResource=rb.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dst.PlacedFootprint={512,{DXGI_FORMAT_B8G8R8A8_UNORM,2048,2048,1,8192}};l->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
#ifdef TEST_RADV
        b={};b.Type=D3D12_RESOURCE_BARRIER_TYPE_ALIASING;b.Aliasing={image[j].Get(),backing.Get()};l->ResourceBarrier(1,&b);
        l->CopyBufferRegion(rb.Get(),rawAt,backing.Get(),off-guard,whole);
        if(!j){b.Aliasing={backing.Get(),image[1].Get()};l->ResourceBarrier(1,&b);}
#endif
        HR(l->Close());ID3D12CommandList *ls[]={l.Get()};q->ExecuteCommandLists(1,ls);HR(q->Signal(fence.Get(),j+1));HR(fence->SetEventOnCompletion(j+1,e));CK(WaitForSingleObject(e,10000)==WAIT_OBJECT_0);HR(d->GetDeviceRemovedReason());
        BYTE *p;HR(rb->Map(0,nullptr,(void**)&p));UINT *shaders=(UINT*)p,*texels=(UINT*)(p+512);
        CK(shaders[0]==rgba[j] && shaders[1]==rgba[1-j]);for(UINT k=0;k<pixels;k++)CK(texels[k]==(k==pixels-1?bgra[1-j]:bgra[j]));
#ifdef TEST_RADV
        old=p+rawAt+guard;
#endif
        if(!j){for(UINT k=0;k<span;k++)CK(old[span+k]==0xa7);memcpy(first.data(),old,span);}else CK(!memcmp(first.data(),old,span));
        for(UINT k=0;k<guard;k++) CK((old-guard)[k]==0xa7 && old[span*2+k]==0xa7);
        for(UINT k=0;k<whole;k++)CK(replacement[k]==0xbd);
        UINT changed=0,hits=0;for(UINT k=0;k<span;k++)changed+=old[j*span+k]!=0xa7;
        for(UINT k=0;k<span/4;k++)hits+=((UINT*)(old+j*span))[k]==bgra[j];
#ifdef TEST_RADV
        CK(changed>2048 && hits>0);
#else
        CK(hits==pixels-1);
#endif
        printf("PASS BGRA8 slot %u off=%zx bytes=%08x/%08x shader=%08x/%08x rawChanged=%u rawColor=%u; full sibling, outer guards and exact VA verified\n",j,off+j*span,texels[0],texels[pixels-1],shaders[0],shaders[1],changed,hits);rb->Unmap(0,nullptr);
    }
    CloseHandle(e);CK(VirtualFree(wc,0,MEM_RELEASE));
#ifndef TEST_RADV
    CK(VirtualFree(alias.base,0,MEM_RELEASE));
#endif
}
