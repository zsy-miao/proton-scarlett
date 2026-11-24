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
    constexpr SIZE_T pool=0x89700000,off=0xf000000,guard=65536;
#ifdef TEST_RADV
    constexpr SIZE_T span=0x880000;
#else
    constexpr SIZE_T span=0x800000;
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
    CK(take && take(owner.Get(),wc+off,span,nullptr,backing.GetAddressOf(),&offset)==S_OK && offset==off);
    BYTE *old=wc+off;
#else
    scarlett_section_info pin,alias;HR(scarlett_query_section(0x53430005,wc+off,span,&pin));CK(pin.offset==off && pin.view_size==pool);
    HR(scarlett_query_section_handle(0x53430004,nullptr,pool,&alias,pin.section));CloseHandle(pin.section);
    BYTE *old=(BYTE*)alias.base+off;
#endif
    memset(wc+off-guard,0xa7,span+2*guard);MemoryBarrier();
    using Place=HRESULT(WINAPI*)(IUnknown*,UINT64,const void*,UINT,const void*,REFIID,void**);
    DWORD w[]={3,0,0,0,3840,0,2160,0x10001,65,1,0,0,0x300001};D3D12_CLEAR_VALUE cv={};cv.Format=DXGI_FORMAT_A8_UNORM;
    auto place=[&](void* where,UINT state,REFIID iid,IUnknown** out){return ((Place)vt[0x428/8])(dev.Get(),(UINT_PTR)where,w,state,&cv,iid,(void**)out);};
    ComPtr<IUnknown> obj,rtvHeap,srvHeap; IUnknown *reject=(IUnknown*)1;
    CK(FAILED(place(wc+off+65536,6,IID_ID3D12Resource,&reject))&&!reject);
    CK(FAILED(place(wc+off,5,IID_ID3D12Resource,&reject))&&!reject);
    cv.Color[3]=1;CK(FAILED(place(wc+off,6,IID_ID3D12Resource,&reject))&&!reject);cv.Color[3]=0;
    for(UINT k=0;k<13;k++)if(k!=1){w[k]^=1;CK(FAILED(place(wc+off,6,IID_ID3D12Resource,&reject))&&!reject);w[k]^=1;}
    HR(place(wc+off,6,IID_ID3D12Resource,obj.GetAddressOf()));
    CK(FAILED(place(wc+off,6,IID_ID3D12Resource,&reject))&&!reject);
    using Heap=HRESULT(WINAPI*)(IUnknown*,const void*,REFIID,void**);using Start=SIZE_T(WINAPI*)(IUnknown*);
    using Writer=void(WINAPI*)(IUnknown*,IUnknown*,const void*,SIZE_T);
    UINT r[]={2,4096,0,1},sh[]={0,4,1,1};
    HR(((Heap)vt[0x70/8])(dev.Get(),r,IID_ID3D12DescriptorHeap,(void**)rtvHeap.GetAddressOf()));
    HR(((Heap)vt[0x70/8])(dev.Get(),sh,IID_ID3D12DescriptorHeap,(void**)srvHeap.GetAddressOf()));
    SIZE_T rt=((Start)((*(void***)rtvHeap.Get())[0x48/8]))(rtvHeap.Get())+32;
    SIZE_T ss=((Start)((*(void***)srvHeap.Get())[0x48/8]))(srvHeap.Get())+32;
    DWORD rw[]={65,4,0,0},sw[]={65,4,0x1688,0,0,1,0,0};
    ((Writer)vt[0xa0/8])(dev.Get(),obj.Get(),rw,rt);((Writer)vt[0x90/8])(dev.Get(),obj.Get(),sw,ss);
    UINT64 stamp[2];memcpy(stamp,(void*)ss,16);
    for(auto &x:sw){x^=1;CK(FAILED(xs_write_view(dev.Get(),0x90,obj.Get(),sw,ss)));x^=1;}
    CK(!memcmp(stamp,(void*)ss,16));memcpy(stamp,(void*)rt,16);
    for(auto &x:rw){x^=1;CK(FAILED(xs_write_view(dev.Get(),0xa0,obj.Get(),rw,rt)));x^=1;}
    CK(!memcmp(stamp,(void*)rt,16));
    UINT64 before=*(UINT64*)ss;sw[0]=28;CK(FAILED(xs_write_view(dev.Get(),0x90,obj.Get(),sw,ss))&&before==*(UINT64*)ss);sw[0]=65;
    rw[0]=28;CK(FAILED(xs_write_view(dev.Get(),0xa0,obj.Get(),rw,rt)));rw[0]=65;
    ID3D12DescriptorHeap *bank;D3D12_GPU_DESCRIPTOR_HANDLE gpu;HR(xs_resolve_view(srvHeap.Get(),ss,0x90,&bank,&gpu));
    ComPtr<ID3D12Resource> image,rb;D3D12_CPU_DESCRIPTOR_HANDLE view;HR(xs_resolve_color_rtv(rtvHeap.Get(),rt,&view,image.GetAddressOf()));
    ComPtr<ID3D12Device>d;HR(image->GetDevice(IID_PPV_ARGS(&d)));obj.Reset();
    for(int j=-65536;j<(int)span+65536;j++)CK(old[j]==0xa7);
    CK(VirtualFree(wc,0,MEM_RELEASE));CK(VirtualAlloc(wc,pool,MEM_RESERVE,PAGE_NOACCESS)==wc);
    auto replacement=(BYTE*)VirtualAlloc(wc+off-guard,span+2*guard,MEM_COMMIT,PAGE_READWRITE);
    CK(replacement==wc+off-guard);memset(replacement,0xbd,span+2*guard);CK(FAILED(place(wc+off,6,IID_ID3D12Resource,&reject))&&!reject);
    constexpr UINT pixels=3840*2160,rawStart=512+pixels;
    D3D12_RESOURCE_DESC buf={};buf.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;buf.Width=rawStart+2*(span+2*guard);
    buf.Height=buf.DepthOrArraySize=buf.MipLevels=buf.SampleDesc.Count=1;buf.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    D3D12_HEAP_PROPERTIES hp={};hp.VisibleNodeMask=hp.CreationNodeMask=1;hp.Type=D3D12_HEAP_TYPE_READBACK;HR(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&buf,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&rb)));
    ComPtr<ID3D12CommandQueue> q;ComPtr<ID3D12CommandAllocator> a;ComPtr<ID3D12GraphicsCommandList> l;D3D12_COMMAND_QUEUE_DESC qd={};
    HR(d->CreateCommandQueue(&qd,IID_PPV_ARGS(&q)));HR(d->CreateCommandAllocator(qd.Type,IID_PPV_ARGS(&a)));HR(d->CreateCommandList(0,qd.Type,a.Get(),nullptr,IID_PPV_ARGS(&l)));
#ifdef TEST_RADV
    l->CopyBufferRegion(rb.Get(),rawStart+span+2*guard,backing.Get(),off-guard,span+2*guard);
    D3D12_RESOURCE_BARRIER aliasBarrier={};aliasBarrier.Type=D3D12_RESOURCE_BARRIER_TYPE_ALIASING;
    aliasBarrier.Aliasing={backing.Get(),image.Get()};l->ResourceBarrier(1,&aliasBarrier);
#endif
    float dark[]={1,0,1,0.25f},bright[]={0,1,0,0.75f};D3D12_RECT corner={3839,2159,3840,2160};
    l->ClearRenderTargetView(view,dark,0,nullptr);l->ClearRenderTargetView(view,bright,1,&corner);
    BYTE dxil[8192];auto file=fopen("a8.dxil","rb");CK(file);auto bytes=fread(dxil,1,sizeof dxil,file);CK(bytes && feof(file));fclose(file);
    ComPtr<ID3D12RootSignature> root;HR(d->CreateRootSignature(0,dxil,bytes,IID_PPV_ARGS(&root)));
    D3D12_COMPUTE_PIPELINE_STATE_DESC pd={};pd.pRootSignature=root.Get();pd.CS={dxil,bytes};ComPtr<ID3D12PipelineState> pso;HR(d->CreateComputePipelineState(&pd,IID_PPV_ARGS(&pso)));
    hp.Type=D3D12_HEAP_TYPE_DEFAULT;buf.Width=256;buf.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;ComPtr<ID3D12Resource> result;
    HR(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&buf,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&result)));
    D3D12_RESOURCE_BARRIER b={};b.Transition={image.Get(),0,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE};l->ResourceBarrier(1,&b);
    l->SetDescriptorHeaps(1,&bank);l->SetComputeRootSignature(root.Get());l->SetPipelineState(pso.Get());l->SetComputeRootDescriptorTable(0,gpu);l->SetComputeRootUnorderedAccessView(1,result->GetGPUVirtualAddress());l->Dispatch(1,1,1);
    b.Transition={result.Get(),0,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE};l->ResourceBarrier(1,&b);l->CopyBufferRegion(rb.Get(),64,result.Get(),0,8);
    b.Transition={image.Get(),0,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE};l->ResourceBarrier(1,&b);
    D3D12_TEXTURE_COPY_LOCATION src={},dst={};src.pResource=image.Get();dst.pResource=rb.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint.Footprint={DXGI_FORMAT_A8_UNORM,2,1,1,256};D3D12_BOX box={3838,2159,0,3840,2160,1};
    l->CopyTextureRegion(&dst,0,0,0,&src,&box);
    dst.PlacedFootprint={512,{DXGI_FORMAT_A8_UNORM,3840,2160,1,3840}};
    l->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
#ifdef TEST_RADV
    aliasBarrier.Aliasing={image.Get(),backing.Get()};l->ResourceBarrier(1,&aliasBarrier);
    l->CopyBufferRegion(rb.Get(),rawStart,backing.Get(),off-guard,span+2*guard);
#endif
    HR(l->Close());ID3D12CommandList *lists[]={l.Get()};q->ExecuteCommandLists(1,lists);
    ComPtr<ID3D12Fence> fence;HR(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));HR(q->Signal(fence.Get(),1));
    HANDLE e=CreateEventW(nullptr,FALSE,FALSE,nullptr);CK(e);HR(fence->SetEventOnCompletion(1,e));CK(WaitForSingleObject(e,10000)==WAIT_OBJECT_0);CloseHandle(e);
    BYTE *p;HR(rb->Map(0,nullptr,(void**)&p));printf("A8 shader RGBA %08lx/%08lx\n",((DWORD*)(p+64))[0],((DWORD*)(p+64))[1]);CK(p[0]==64 && p[1]==191 && ((DWORD*)(p+64))[0]==0x40000000 && ((DWORD*)(p+64))[1]==0xbf000000);
    for(UINT j=0;j<pixels;j++)CK(p[512+j]==(j==pixels-1?191:64));
#ifdef TEST_RADV
    old=p+rawStart+guard;
    for(UINT j=0;j<span+2*guard;j++)CK(p[rawStart+span+2*guard+j]==0xa7);
#endif
    UINT hits=0,lights=0,changed=0;for(UINT j=0;j<span;j++){hits+=old[j]==64;lights+=old[j]==191;changed+=old[j]!=0xa7;}
    printf("original +f000000 span=%zx changed=%u raw 40=%u bf=%u\n",span,changed,hits,lights);
#ifdef TEST_RADV
    CK(changed>=3840 && hits>=3840);for(UINT j=0x87c000;j<span;j++)CK(old[j]==0xa7);
#else
    CK(hits==0x7f8000-1 && lights==1);for(UINT j=0x7f8000;j<span;j++)CK(old[j]==0xa7);
#endif
    for(UINT j=0;j<span+2*guard;j++)CK(replacement[j]==0xbd);
    for(UINT j=0;j<65536;j++)CK(old[(int)j-65536]==0xa7 && old[span+j]==0xa7);
    rb->Unmap(0,nullptr);CK(VirtualFree(wc,0,MEM_RELEASE));
#ifndef TEST_RADV
    CK(VirtualFree(alias.base,0,MEM_RELEASE));
#endif
    puts("PASS A8 +428: full image, alpha-only shader read, original WC backing, slot padding, both 64KiB guards and exact-VA replacement");
}
