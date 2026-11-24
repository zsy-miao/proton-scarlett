#include <windows.h>
#include <d3d12.h>
#include <wrl/client.h>
#include <cstdio>
#include <cmath>
#include "xmem/marked_section.h"
using Microsoft::WRL::ComPtr;
extern "C" HRESULT xs_r32_texel_table(IUnknown*,IUnknown*,SIZE_T,ID3D12DescriptorHeap**);
extern "C" HRESULT xs_r32_packed_uint_table(IUnknown*,IUnknown*,SIZE_T,ID3D12DescriptorHeap**);
extern "C" HRESULT xs_write_address_srv(IUnknown*,const void*,const void*,SIZE_T);
extern "C" HRESULT xs_copy_wc_view(IUnknown*,SIZE_T,SIZE_T);
extern "C" HRESULT xs_resolve_view(IUnknown*,SIZE_T,UINT,ID3D12DescriptorHeap**,D3D12_GPU_DESCRIPTOR_HANDLE*);
extern "C" HRESULT xs_descriptor_range(IUnknown*,SIZE_T,UINT,ID3D12DescriptorHeap**,D3D12_CPU_DESCRIPTOR_HANDLE*,D3D12_GPU_DESCRIPTOR_HANDLE*);
extern "C" ID3D12Resource *test_r32_backing(IUnknown*);
extern "C" HRESULT xs_write_view(IUnknown*,UINT,IUnknown*,const void*,SIZE_T);
extern "C" HRESULT WINAPI D3D12XboxCreateDevice(void*,const void*,REFIID,void**);
extern "C" HRESULT xs_color_allocation_info(IUnknown*,UINT,UINT,const void*,D3D12_RESOURCE_ALLOCATION_INFO*);
extern "C" HRESULT xs_msaa_allocation_info1(IUnknown*,UINT,UINT,const void*,D3D12_RESOURCE_ALLOCATION_INFO*,D3D12_RESOURCE_ALLOCATION_INFO1*);
#define CK(x) do{if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);return 1;}}while(0)
#define HR(x) CK(SUCCEEDED(x))
int main(){
    constexpr UINT W=1280,H=720,bytes=4*W*H,pool=0x89700000;
#ifdef TEST_RADV
    constexpr UINT off=0x13580000;auto table_fn=xs_r32_packed_uint_table, wrong_fn=xs_r32_texel_table;
#else
    constexpr UINT off=0x13600000;auto table_fn=xs_r32_texel_table, wrong_fn=xs_r32_packed_uint_table;
#endif
    using Alloc=void*(WINAPI*)(void*,SIZE_T,DWORD,UINT64,DWORD);auto alloc=(Alloc)(void*)GetProcAddress(LoadLibraryW(L"xmem.dll"),"XMemVirtualAlloc");CK(alloc);
    BYTE *wc=(BYTE*)alloc(nullptr,pool,0x20003000,0x283,0x2404);CK(wc);
    DWORD params[20]={};params[0]=0x20004;params[2]=0x100000;params[10]=1;params[15]=0x20;
    const IID boot={0x5405c344,0xd457,0x444e,{0xb4,0xdd,0x23,0x66,0xe4,0x5a,0xee,0x39}};
    ComPtr<IUnknown> dev;HR(D3D12XboxCreateDevice(nullptr,params,boot,(void**)dev.GetAddressOf()));
    DWORD wire[]={3,0,0,0,W,0,H,0x10001,42,1,0,0x102,0x300004};
    auto vt=*(void***)dev.Get();
    ComPtr<ID3D12Heap> native;ComPtr<ID3D12Resource> poolBuffer;HANDLE section=nullptr;
#ifdef TEST_RADV
    ComPtr<ID3D12Device> owner;HR(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&owner)));
    auto take=(HRESULT(WINAPI*)(ID3D12Device*,const void*,SIZE_T,ID3D12Heap**,ID3D12Resource**,UINT*))(void*)GetProcAddress(GetModuleHandleW(L"xmem.dll"),"ScarlettAcquireNativePoolV1");UINT actual=0;
    CK(take && take(owner.Get(),wc+off,bytes,native.GetAddressOf(),poolBuffer.GetAddressOf(),&actual)==S_OK && actual==off);
    BYTE *old=wc+off;memset(old-65536,0xa7,bytes+131072);
#else
    scarlett_section_info pin,alias;HR(scarlett_query_section(0x53430005,wc+off,bytes,&pin));CK(pin.offset==off && pin.view_size==pool && pin.base==wc);
    HR(scarlett_query_section_handle(0x53430004,nullptr,pool,&alias,pin.section));BYTE *old=(BYTE*)alias.base+off;memset(old-65536,0xa7,bytes+131072);
    section=pin.section;
#endif
    using Place=HRESULT(WINAPI*)(IUnknown*,UINT64,const void*,UINT,const void*,REFIID,void**);
    auto place=[&](BYTE *va,UINT state,const void *clear,REFIID iid,IUnknown **out){return ((Place)vt[0x428/8])(dev.Get(),(UINT_PTR)va,wire,state,clear,iid,(void**)out);};
    IUnknown *bad=nullptr;
    ComPtr<IUnknown> object;HR(place(wc+off,3,nullptr,IID_ID3D12Resource,object.GetAddressOf()));
    ComPtr<ID3D12Resource> original,linear,read;original.Attach(test_r32_backing(object.Get()));CK(original);
    using Heap=HRESULT(WINAPI*)(IUnknown*,const void*,REFIID,void**);using Start=SIZE_T(WINAPI*)(IUnknown*);
    ComPtr<IUnknown> views;UINT hd[]={0,128,1,1};HR(((Heap)vt[0x70/8])(dev.Get(),hd,IID_ID3D12DescriptorHeap,(void**)views.GetAddressOf()));
    auto start=((Start)((*(void***)views.Get())[0x48/8]))(views.Get());
    ComPtr<IUnknown> staging; hd[1]=0xc38; hd[2]=0;
    HR(((Heap)vt[0x70/8])(dev.Get(),hd,IID_ID3D12DescriptorHeap,(void**)staging.GetAddressOf()));
    SIZE_T src=((Start)((*(void***)staging.Get())[0x48/8]))(staging.Get()), dst=start+59*32;
    DWORD sw[]={42,4,0x1688,0,0,1,0,0},uw[]={42,4,0,0},aw[]={67,4,0x1688,0,0,1,0,0,0,0,0,0,0,0,0,3};
    SIZE_T va=(SIZE_T)wc+off;memcpy(aw+10,&va,8);DWORD rgb[13];memcpy(rgb,wire,sizeof rgb);rgb[8]=67;rgb[12]=0x300000;
    HR(xs_write_view(dev.Get(),0x90,object.Get(),sw,src));HR(xs_write_view(dev.Get(),0x98,object.Get(),uw,src+32));
    HR(xs_write_address_srv(dev.Get(),rgb,aw,src+64));
    auto saved=*(UINT64*)src;
    uw[2]=1;CK(FAILED(xs_write_view(dev.Get(),0x98,object.Get(),uw,src)));uw[2]=0;
    aw[10]+=256;CK(FAILED(xs_write_address_srv(dev.Get(),rgb,aw,src)));aw[10]-=256;
    aw[15]=0;CK(FAILED(xs_write_address_srv(dev.Get(),rgb,aw,src)) && *(UINT64*)src==saved);aw[15]=3;
    for(UINT j=0;j<3;j++){
        HR(xs_copy_wc_view(dev.Get(),dst+32*j,src+32*j));CK(*(UINT64*)(dst+32*j)!=*(UINT64*)(src+32*j));
        ID3D12DescriptorHeap *host=(ID3D12DescriptorHeap*)1;D3D12_GPU_DESCRIPTOR_HANDLE g={1};
        CK(xs_resolve_view(views.Get(),dst+32*j,j==1?0x98:0x90,&host,&g)==E_NOTIMPL && !host && !g.ptr);
    }
    ID3D12DescriptorHeap *host=nullptr;D3D12_GPU_DESCRIPTOR_HANDLE g={};D3D12_CPU_DESCRIPTOR_HANDLE cpu={};
    CK(xs_descriptor_range(views.Get(),dst-32,2,&host,&cpu,&g)==E_NOTIMPL && !host && !cpu.ptr && !g.ptr);
    HR(xs_descriptor_range(views.Get(),dst-32,1,&host,&cpu,&g));CK(host && cpu.ptr && g.ptr);
    *(UINT64*)src^=1;CK(FAILED(xs_copy_wc_view(dev.Get(),dst,src)));*(UINT64*)src^=1;
    ComPtr<ID3D12Device> d;HR(original->GetDevice(IID_PPV_ARGS(&d)));
    ID3D12DescriptorHeap *mixed=nullptr;
    ComPtr<IUnknown> sibling;HR(place(wc+off+bytes,3,nullptr,IID_ID3D12Resource,sibling.GetAddressOf()));
    SIZE_T peer=va+bytes;memcpy(aw+10,&peer,8);HR(xs_write_address_srv(dev.Get(),rgb,aw,dst+64));
    CK(FAILED(table_fn(dev.Get(),views.Get(),dst,&mixed)) && !mixed);
    memcpy(aw+10,&va,8);HR(xs_copy_wc_view(dev.Get(),dst+64,src+64));sibling.Reset();staging.Reset();
    ID3D12DescriptorHeap *wrong=(ID3D12DescriptorHeap*)1;*(UINT64*)dst^=1;
    CK(FAILED(table_fn(dev.Get(),views.Get(),dst,&wrong)) && !wrong);*(UINT64*)dst^=1;
    CK(FAILED(wrong_fn(dev.Get(),views.Get(),dst,&wrong)) && !wrong);
    D3D12_FEATURE_DATA_FORMAT_SUPPORT rgbCaps={};rgbCaps.Format=DXGI_FORMAT_R9G9B9E5_SHAREDEXP;HR(d->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT,&rgbCaps,sizeof rgbCaps));
    printf("RGB9E5 support1=%x support2=%x BUFFER=%u\n",rgbCaps.Support1,rgbCaps.Support2,!!(rgbCaps.Support1&D3D12_FORMAT_SUPPORT1_BUFFER));
    CK(!!(rgbCaps.Support1&D3D12_FORMAT_SUPPORT1_BUFFER)==!poolBuffer);
    ComPtr<ID3D12DescriptorHeap> typed;HR(table_fn(dev.Get(),views.Get(),dst,typed.GetAddressOf()));
    DWORD nullcbv[4]={};HR(xs_write_view(dev.Get(),0x88,nullptr,nullcbv,dst));
    CK(FAILED(table_fn(dev.Get(),views.Get(),dst,&wrong)) && !wrong);
    HR(xs_resolve_view(views.Get(),dst,0x88,&host,&g));
    puts("PASS tiled records: original identity, independent copies, mixed-resource and stale-tag rejection, slot reuse and ordinary bind guards");
    auto table=typed->GetGPUDescriptorHandleForHeapStart();
    D3D12_RESOURCE_DESC b={};b.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;b.Width=bytes*10;b.Height=b.DepthOrArraySize=b.MipLevels=b.SampleDesc.Count=1;b.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;b.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    D3D12_HEAP_PROPERTIES hp={};hp.Type=D3D12_HEAP_TYPE_DEFAULT;hp.CreationNodeMask=hp.VisibleNodeMask=1;HR(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&b,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&linear)));
    hp.Type=D3D12_HEAP_TYPE_READBACK;if(poolBuffer)b.Width+=bytes+131072;b.Flags=D3D12_RESOURCE_FLAG_NONE;HR(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&b,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&read)));
    ComPtr<ID3D12RootSignature> root;ComPtr<ID3D12PipelineState> pipelines[2];
    for(UINT i=0;i<2;i++) {BYTE code[8192];FILE *file=fopen(i?"write.dxil":"tile.dxil","rb");CK(file);size_t len=fread(code,1,sizeof code,file);CK(len && feof(file));fclose(file);
        if(!i)HR(d->CreateRootSignature(0,code,len,IID_PPV_ARGS(&root)));
        D3D12_COMPUTE_PIPELINE_STATE_DESC pd={};pd.pRootSignature=root.Get();pd.CS={code,len};HR(d->CreateComputePipelineState(&pd,IID_PPV_ARGS(&pipelines[i])));}
    ComPtr<ID3D12CommandQueue> q;ComPtr<ID3D12CommandAllocator> a;ComPtr<ID3D12GraphicsCommandList> l;D3D12_COMMAND_QUEUE_DESC qd={};HR(d->CreateCommandQueue(&qd,IID_PPV_ARGS(&q)));HR(d->CreateCommandAllocator(qd.Type,IID_PPV_ARGS(&a)));HR(d->CreateCommandList(0,qd.Type,a.Get(),nullptr,IID_PPV_ARGS(&l)));
    ID3D12DescriptorHeap *banks[]={typed.Get()};l->SetDescriptorHeaps(1,banks);l->SetComputeRootSignature(root.Get());
    l->SetComputeRootDescriptorTable(0,table);l->SetComputeRootUnorderedAccessView(1,linear->GetGPUVirtualAddress());
    auto transition=[&](D3D12_RESOURCE_STATES from,D3D12_RESOURCE_STATES to){D3D12_RESOURCE_BARRIER barrier={};barrier.Transition={original.Get(),0,from,to};l->ResourceBarrier(1,&barrier);};
    for(UINT pass=0;pass<2;pass++) {
        transition(D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        l->SetPipelineState(pipelines[0].Get());l->SetComputeRoot32BitConstant(2,pass,0);l->Dispatch(W/8,H/8,1);
        if(!pass) {transition(D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);l->SetPipelineState(pipelines[1].Get());l->Dispatch(W/8,H/8,1);}
    }
    D3D12_RESOURCE_BARRIER br={};br.Transition={linear.Get(),0,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE};l->ResourceBarrier(1,&br);l->CopyBufferRegion(read.Get(),0,linear.Get(),0,bytes*10);
    if(poolBuffer){D3D12_RESOURCE_BARRIER aliasing={};aliasing.Type=D3D12_RESOURCE_BARRIER_TYPE_ALIASING;aliasing.Aliasing={original.Get(),poolBuffer.Get()};l->ResourceBarrier(1,&aliasing);
        l->CopyBufferRegion(read.Get(),bytes*10,poolBuffer.Get(),off-65536,bytes+131072);}
    HR(l->Close());
    ComPtr<ID3D12Fence> gate,end;HR(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&gate)));HR(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&end)));HR(q->Wait(gate.Get(),1));ID3D12CommandList *lists[]={l.Get()};q->ExecuteCommandLists(1,lists);HR(q->Signal(end.Get(),1));
    auto at=[](UINT x,UINT y){return ((y/8)*160+x/8)*64+(x%4)+(y%4)*4+(x%8/4)*16+(y%8/4)*32;};
    for(UINT y=0;y<H;y++)for(UINT x=0;x<W;x++)((UINT*)old)[at(x,y)]=(y*W+x)*0x9e3779b9u;
    MemoryBarrier();original.Reset();CK(VirtualFree(wc,0,MEM_RELEASE));BYTE *reuse=(BYTE*)VirtualAlloc(wc+off,bytes,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);CK(reuse==wc+off);memset(reuse,0xbd,bytes);CK(FAILED(place(reuse,3,nullptr,IID_ID3D12Resource,&bad)) && !bad);
    CK(FAILED(xs_write_view(dev.Get(),0x90,object.Get(),sw,dst)) && FAILED(xs_write_address_srv(dev.Get(),rgb,aw,dst)));
    object.Reset();views.Reset();
    HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);CK(event);HR(end->SetEventOnCompletion(1,event));CK(WaitForSingleObject(event,50)==WAIT_TIMEOUT);
    using PlaceCore=HRESULT(WINAPI*)(ID3D12Device*,ID3D12Heap*,HANDLE,SIZE_T,SIZE_T,ID3D12Resource**);
    auto reserve=(PlaceCore)(void*)GetProcAddress(GetModuleHandleW(L"d3d12core.dll"),"ScarlettTiledR32BufferV1");ID3D12Resource *other=nullptr;
    CK(reserve && reserve(d.Get(),native.Get(),section,pool,off,&other)==DXGI_ERROR_INVALID_CALL && !other);
    MemoryBarrier();HR(gate->Signal(1));CK(WaitForSingleObject(event,10000)==WAIT_OBJECT_0);HR(d->GetDeviceRemovedReason());CloseHandle(event);
    UINT *pixels;HR(read->Map(0,nullptr,(void**)&pixels));
    if(poolBuffer)old=(BYTE*)pixels+bytes*10+65536;
    for(UINT y=0;y<H;y++)for(UINT x=0;x<W;x++){UINT i=y*W+x,initial=i*0x9e3779b9u;
        for(UINT pass=0;pass<2;pass++){UINT v=initial^(pass?0xf0e1d2c3:0);const UINT *p=pixels+5*(pass*W*H+i);
            CK(p[0]==v);float rgba[4];memcpy(rgba,p+1,sizeof rgba);for(UINT c=0;c<3;c++)CK(rgba[c]==std::ldexp(float((v>>(9*c))&511),int(v>>27)-24));CK(rgba[3]==1.0f);}
        CK(((UINT*)old)[at(x,y)]==(initial^0xf0e1d2c3) && ((UINT*)reuse)[i]==0xbdbdbdbd);}
    for(UINT j=0;j<65536;j++)CK(old[(int)j-65536]==0xa7 && old[bytes+j]==0xa7);
    read->Unmap(0,nullptr);CK(end->GetCompletedValue()==1);l.Reset();typed.Reset();
    HR(reserve(d.Get(),native.Get(),section,pool,off,&other));other->Release();
#ifndef TEST_RADV
    CloseHandle(section);CK(VirtualFree(alias.base,0,MEM_RELEASE));
#endif
    CK(VirtualFree(reuse,0,MEM_RELEASE));printf("PASS %s 0032 typed aliases: 921600 R32 and RGB9E5 loads before/after typed UAV rewrite, exact WC VA reuse, guards and fence-held lease\n",poolBuffer?"RADV":"RTX");
}
