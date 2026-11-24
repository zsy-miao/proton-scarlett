#include <windows.h>
#include <d3d12.h>
#include <wrl/client.h>
#include <cstdio>

#include <vector>
#include "xmem/marked_section.h"
using Microsoft::WRL::ComPtr;
extern "C" HRESULT xs_write_address_srv(IUnknown*,const void*,const void*,SIZE_T);
extern "C" HRESULT xs_r32_texel_table(IUnknown*,IUnknown*,SIZE_T,ID3D12DescriptorHeap**);
extern "C" HRESULT xs_r32_packed_uint_table(IUnknown*,IUnknown*,SIZE_T,ID3D12DescriptorHeap**);
extern "C" HRESULT xs_color_allocation_info(IUnknown*,UINT,UINT,const void*,D3D12_RESOURCE_ALLOCATION_INFO*);
extern "C" HRESULT xs_msaa_allocation_info1(IUnknown*,UINT,UINT,const void*,D3D12_RESOURCE_ALLOCATION_INFO*,D3D12_RESOURCE_ALLOCATION_INFO1*);
extern "C" HRESULT xs_copy_wc_view(IUnknown*,SIZE_T,SIZE_T);
extern "C" HRESULT xs_resolve_view(IUnknown*,SIZE_T,UINT,ID3D12DescriptorHeap**,D3D12_GPU_DESCRIPTOR_HANDLE*);
extern "C" ID3D12Resource *test_small_tile_backing(IUnknown*,SIZE_T,DXGI_FORMAT);
extern "C" HRESULT xs_write_view(IUnknown*,UINT,IUnknown*,const void*,SIZE_T);
extern "C" HRESULT WINAPI D3D12XboxCreateDevice(void*,const void*,REFIID,void**);
#define CK(x) do{if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);return 1;}}while(0)
#define HR(x) CK(SUCCEEDED(x))
static int check(DXGI_FORMAT fmt){
    bool rg=fmt==DXGI_FORMAT_R32G32_FLOAT, ui=fmt==DXGI_FORMAT_R32_UINT;
    constexpr UINT W=160,H=90,pool=0x89700000;UINT bytes=rg?0x1cc00:0xf000,word=rg?8:4;
#ifdef TEST_RADV
    UINT off=rg?0x15440000:ui?0x1545cc00:0xe6c0800;
#else
    UINT off=rg?0x15fd0000:ui?0x15fecc00:0xe6c0800;
#endif
    using Alloc=void*(WINAPI*)(void*,SIZE_T,DWORD,UINT64,DWORD);auto alloc=(Alloc)(void*)GetProcAddress(LoadLibraryW(L"xmem.dll"),"XMemVirtualAlloc");CK(alloc);
    BYTE *wc=(BYTE*)alloc(nullptr,pool,0x20003000,0x283,0x2404);CK(wc);
    DWORD params[20]={};params[0]=0x20004;params[2]=0x100000;params[10]=1;params[15]=0x20;
    const IID boot={0x5405c344,0xd457,0x444e,{0xb4,0xdd,0x23,0x66,0xe4,0x5a,0xee,0x39}};
    ComPtr<IUnknown> dev;HR(D3D12XboxCreateDevice(nullptr,params,boot,(void**)dev.GetAddressOf()));
    DWORD wire[]={3,0,0,0,W,0,H,0x10001,(DWORD)fmt,1,0,0x102,0x300004};
    auto vt=*(void***)dev.Get();
    ComPtr<ID3D12Device> d;HR(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&d)));
    ComPtr<ID3D12Resource> poolBuffer;
#ifdef TEST_RADV
    auto take=(HRESULT(WINAPI*)(ID3D12Device*,const void*,SIZE_T,ID3D12Heap**,ID3D12Resource**,UINT*))(void*)GetProcAddress(GetModuleHandleW(L"xmem.dll"),"ScarlettAcquireNativePoolV1");UINT actual=0;ComPtr<ID3D12Heap> native;
    CK(take && take(d.Get(),wc+off,bytes,native.GetAddressOf(),poolBuffer.GetAddressOf(),&actual)==S_OK && actual==off && poolBuffer->GetDesc().Width==pool);
    BYTE *old=wc+off;memset(old-65536,0xa7,bytes+131072);
#else
    scarlett_section_info pin,alias;HR(scarlett_query_section(0x53430005,wc+off,bytes,&pin));CK(pin.offset==off && pin.view_size==pool && pin.base==wc);
    HR(scarlett_query_section_handle(0x53430004,nullptr,pool,&alias,pin.section));BYTE *old=(BYTE*)alias.base+off;memset(old-65536,0xa7,bytes+131072);
    HANDLE section=pin.section;
#endif
    using Place=HRESULT(WINAPI*)(IUnknown*,UINT64,const void*,UINT,const void*,REFIID,void**);
    auto place=[&](BYTE *va,UINT state,const void *clear,REFIID iid,IUnknown **out){return ((Place)vt[0x428/8])(dev.Get(),(UINT_PTR)va,wire,state,clear,iid,(void**)out);};
    IUnknown *bad=nullptr;
    using Q=D3D12_RESOURCE_ALLOCATION_INFO*(WINAPI*)(D3D12_RESOURCE_ALLOCATION_INFO*,IUnknown*,UINT,UINT,const void*);
    using Q1=D3D12_RESOURCE_ALLOCATION_INFO*(WINAPI*)(D3D12_RESOURCE_ALLOCATION_INFO*,IUnknown*,UINT,UINT,const void*,D3D12_RESOURCE_ALLOCATION_INFO1*);
    for(UINT k=0;k<(rg?1u:2u);k++){
        wire[2]=4096*k;D3D12_RESOURCE_ALLOCATION_INFO a={};D3D12_RESOURCE_ALLOCATION_INFO1 b={};b.Offset=~0ull;
        CK(((Q)vt[0xc8/8])(&a,dev.Get(),1,1,wire)==&a && a.SizeInBytes==bytes && a.Alignment==256);
        CK(((Q1)vt[0x360/8])(&a,dev.Get(),1,1,wire,&b)==&a && b.SizeInBytes==bytes && b.Alignment==256 && !b.Offset);
        if(ui){
            wire[8]=DXGI_FORMAT_R8_UINT;
            CK(((Q)vt[0xc8/8])(&a,dev.Get(),1,1,wire)==&a && a.SizeInBytes==0x3c00 && a.Alignment==256);
            CK(FAILED(xs_msaa_allocation_info1(dev.Get(),1,1,wire,&a,&b)));
            CK(FAILED(place(wc+off,3,nullptr,IID_ID3D12Resource,&bad)) && !bad);
            wire[6]++;CK(FAILED(xs_color_allocation_info(dev.Get(),1,1,wire,&a)));wire[6]--;
            wire[8]=DXGI_FORMAT_R8G8B8A8_SNORM;CK(FAILED(xs_color_allocation_info(dev.Get(),1,1,wire,&a)));
            wire[8]=fmt;
        }
    }
    wire[2]=4096;CK(FAILED(place(wc+off,3,nullptr,IID_ID3D12Resource,&bad)) && !bad);wire[2]=0;
    wire[12]^=4;CK(FAILED(place(wc+off,3,nullptr,IID_ID3D12Resource,&bad)) && !bad);wire[12]^=4;
    if(ui)CK(FAILED(place(wc+0xe6c0800,3,nullptr,IID_ID3D12Resource,&bad)) && !bad);
    CK(FAILED(place(wc+off+256,3,nullptr,IID_ID3D12Resource,&bad)) && !bad);
    CK(FAILED(place(wc+off,6,nullptr,IID_ID3D12Resource,&bad)) && !bad);
    CK(FAILED(place(wc+off,3,wire,IID_ID3D12Resource,&bad)) && !bad);
    ComPtr<IUnknown> object;HR(place(wc+off,3,nullptr,IID_ID3D12Resource,object.GetAddressOf()));
    ComPtr<ID3D12Resource> original,upload,read;
    using Heap=HRESULT(WINAPI*)(IUnknown*,const void*,REFIID,void**);using Start=SIZE_T(WINAPI*)(IUnknown*);
    ComPtr<IUnknown> views;UINT hd[]={0,128,1,1};HR(((Heap)vt[0x70/8])(dev.Get(),hd,IID_ID3D12DescriptorHeap,(void**)views.GetAddressOf()));
    auto start=((Start)((*(void***)views.Get())[0x48/8]))(views.Get());
    ComPtr<IUnknown> staging; hd[1]=0xc38; hd[2]=0;
    HR(((Heap)vt[0x70/8])(dev.Get(),hd,IID_ID3D12DescriptorHeap,(void**)staging.GetAddressOf()));
    SIZE_T src=((Start)((*(void***)staging.Get())[0x48/8]))(staging.Get()), dst=start+59*32;
    DWORD sw[]={(DWORD)fmt,4,0x1688,0,0,1,0,0},uw[]={(DWORD)fmt,4,0,0};
    for(UINT i=0;i<2;i++){
        DWORD *w=i?uw:sw;UINT kind=i?0x98:0x90;SIZE_T at=src+32*i;
        HR(xs_write_view(dev.Get(),kind,object.Get(),w,at));UINT64 tag[2];memcpy(tag,(void*)at,16);
        for(UINT j=0;j<(i?4u:8u);j++){w[j]^=1;CK(FAILED(xs_write_view(dev.Get(),kind,object.Get(),w,at)));w[j]^=1;}
        w[0]=rg?41:16;CK(FAILED(xs_write_view(dev.Get(),kind,object.Get(),w,at)));w[0]=fmt;CK(!memcmp(tag,(void*)at,16));
        HR(xs_copy_wc_view(dev.Get(),dst+32*i,at));ID3D12DescriptorHeap *host=nullptr;D3D12_GPU_DESCRIPTOR_HANDLE g={};
        CK(xs_resolve_view(views.Get(),dst+32*i,kind,&host,&g)==E_NOTIMPL && !host && !g.ptr);
    }
    if(ui){
        DWORD rgb[]={3,0,0,0,1280,0,720,0x10001,67,1,0,0x102,0x300000};
        DWORD aw[]={67,4,0x1688,0,0,1,0,0,0,0,0,0,0,0,0,3};SIZE_T va=(SIZE_T)(wc+off);memcpy(aw+10,&va,8);
        CK(FAILED(xs_write_address_srv(dev.Get(),rgb,aw,dst+64)));
        ID3D12DescriptorHeap *table=nullptr;
        CK(FAILED(xs_r32_texel_table(dev.Get(),views.Get(),dst,&table)) && !table);
        CK(FAILED(xs_r32_packed_uint_table(dev.Get(),views.Get(),dst,&table)) && !table);
    }
    staging.Reset();object.Reset();CK(place(wc+off,3,nullptr,IID_ID3D12Resource,&bad)==DXGI_ERROR_INVALID_CALL && !bad);
    original.Attach(test_small_tile_backing(views.Get(),dst,fmt));CK(original && original->GetDesc().Width==bytes);views.Reset();
    D3D12_RESOURCE_DESC b={};b.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;b.Width=bytes;b.Height=b.DepthOrArraySize=b.MipLevels=b.SampleDesc.Count=1;b.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    D3D12_HEAP_PROPERTIES hp={};hp.Type=D3D12_HEAP_TYPE_UPLOAD;hp.CreationNodeMask=hp.VisibleNodeMask=1;HR(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&b,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&upload)));
    hp.Type=D3D12_HEAP_TYPE_READBACK;b.Width=bytes*2+131072;HR(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&b,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&read)));
    BYTE *u;HR(upload->Map(0,nullptr,(void**)&u));std::vector<BYTE> expected(bytes,0xa7),input(bytes,0xa7);
    UINT64 hash=14695981039346656037ull;
    for(UINT y=0;y<H;y++)for(UINT x=0;x<W;x++){
        UINT tile=y/(rg?4:8)*(W/8)+x/8;
        UINT lane=rg?((x&1)|((y&1)<<1)|((x&6)<<1)|((y&2)<<3)):((x&3)|((y&3)<<2)|((x&4)<<2)|((y&4)<<3));
        UINT at=tile*256+word*lane;hash=(hash^at)*1099511628211ull;
        for(UINT j=0;j<word/4;j++){UINT val=(j?0xbf000000:0x3f000000)+y*W+x;memcpy(input.data()+at+4*j,&val,4);val^=0x12345;memcpy(expected.data()+at+4*j,&val,4);}
    }
    CK(hash==(rg?0x634c736d7da5f325ull:0x003d5566177d7e25ull));memcpy(u,expected.data(),bytes);upload->Unmap(0,nullptr);
    ComPtr<ID3D12CommandQueue> q;ComPtr<ID3D12CommandAllocator> a;ComPtr<ID3D12GraphicsCommandList> l;D3D12_COMMAND_QUEUE_DESC qd={};HR(d->CreateCommandQueue(&qd,IID_PPV_ARGS(&q)));HR(d->CreateCommandAllocator(qd.Type,IID_PPV_ARGS(&a)));HR(d->CreateCommandList(0,qd.Type,a.Get(),nullptr,IID_PPV_ARGS(&l)));
    auto transition=[&](D3D12_RESOURCE_STATES from,D3D12_RESOURCE_STATES to){D3D12_RESOURCE_BARRIER b={};b.Transition={original.Get(),0,from,to};l->ResourceBarrier(1,&b);};
    transition(D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);l->CopyBufferRegion(read.Get(),0,original.Get(),0,bytes);
    transition(D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_COPY_DEST);l->CopyBufferRegion(original.Get(),0,upload.Get(),0,bytes);
    if(poolBuffer){D3D12_RESOURCE_BARRIER b={};b.Type=D3D12_RESOURCE_BARRIER_TYPE_ALIASING;b.Aliasing={original.Get(),poolBuffer.Get()};l->ResourceBarrier(1,&b);
        l->CopyBufferRegion(read.Get(),bytes,poolBuffer.Get(),off-65536,bytes+131072);}
    HR(l->Close());
    ComPtr<ID3D12Fence> gate,end;HR(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&gate)));HR(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&end)));HR(q->Wait(gate.Get(),1));ID3D12CommandList *lists[]={l.Get()};q->ExecuteCommandLists(1,lists);HR(q->Signal(end.Get(),1));
    memcpy(old,input.data(),bytes);MemoryBarrier();original.Reset();CK(VirtualFree(wc,0,MEM_RELEASE));
    CK(VirtualAlloc(wc,pool,MEM_RESERVE,PAGE_NOACCESS)==wc);BYTE *reuse=wc+off;CK(VirtualAlloc(reuse,bytes,MEM_COMMIT,PAGE_READWRITE));memset(reuse,0xbd,bytes);
    CK(FAILED(place(reuse,3,nullptr,IID_ID3D12Resource,&bad)) && !bad);
    HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);CK(event);HR(end->SetEventOnCompletion(1,event));CK(WaitForSingleObject(event,50)==WAIT_TIMEOUT);

    HR(gate->Signal(1));CK(WaitForSingleObject(event,10000)==WAIT_OBJECT_0);HR(d->GetDeviceRemovedReason());CloseHandle(event);
    BYTE *pixels;HR(read->Map(0,nullptr,(void**)&pixels));CK(!memcmp(pixels,input.data(),bytes));
    if(poolBuffer)old=(BYTE*)pixels+bytes+65536;
    CK(!memcmp(old,expected.data(),bytes));
    for(UINT j=0;j<bytes;j++)CK(reuse[j]==0xbd);
    for(UINT j=0;j<65536;j++)CK(old[(int)j-65536]==0xa7 && old[bytes+j]==0xa7);
    read->Unmap(0,nullptr);l.Reset();
#ifndef TEST_RADV
    CloseHandle(section);CK(VirtualFree(alias.base,0,MEM_RELEASE));
#endif
    CK(VirtualFree(wc,0,MEM_RELEASE));
    printf("PASS tiled fmt=%u bytes=%x off=%x: 14400 texels, padding and guards, copied lease, gated original bytes both directions after VA reuse, inert shader slots\n",fmt,bytes,off);return 0;
}
int main(){return check(DXGI_FORMAT_R32G32_FLOAT) || check(DXGI_FORMAT_R32_FLOAT) || check(DXGI_FORMAT_R32_UINT);}
