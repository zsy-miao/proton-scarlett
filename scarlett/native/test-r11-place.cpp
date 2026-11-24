#include <windows.h>
#include <d3d12.h>
#include <wrl/client.h>
#include <cstdio>
#include "xg/r11_mips.h"
#include <vector>
#include "xmem/marked_section.h"
using Microsoft::WRL::ComPtr;
extern "C" HRESULT xs_r11_mip_table(IUnknown*,IUnknown*,SIZE_T,ID3D12DescriptorHeap**);
extern "C" HRESULT xs_copy_wc_view(IUnknown*,SIZE_T,SIZE_T);
extern "C" HRESULT xs_resolve_view(IUnknown*,SIZE_T,UINT,ID3D12DescriptorHeap**,D3D12_GPU_DESCRIPTOR_HANDLE*);
extern "C" ID3D12Resource *test_r11_backing(IUnknown*);
extern "C" HRESULT xs_write_view(IUnknown*,UINT,IUnknown*,const void*,SIZE_T);
extern "C" HRESULT WINAPI D3D12XboxCreateDevice(void*,const void*,REFIID,void**);
#define CK(x) do{if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);return 1;}}while(0)
#define HR(x) CK(SUCCEEDED(x))
int main(){
    constexpr UINT W=1280,H=720,bytes=0x4b1400,pool=0x89700000;
#ifdef TEST_RADV
    constexpr UINT off=0x13c88000;
#else
    constexpr UINT off=0x13d14000;
#endif
    using Alloc=void*(WINAPI*)(void*,SIZE_T,DWORD,UINT64,DWORD);auto alloc=(Alloc)(void*)GetProcAddress(LoadLibraryW(L"xmem.dll"),"XMemVirtualAlloc");CK(alloc);
    BYTE *wc=(BYTE*)alloc(nullptr,pool,0x20003000,0x283,0x2404);CK(wc);
    DWORD params[20]={};params[0]=0x20004;params[2]=0x100000;params[10]=1;params[15]=0x20;
    const IID boot={0x5405c344,0xd457,0x444e,{0xb4,0xdd,0x23,0x66,0xe4,0x5a,0xee,0x39}};
    ComPtr<IUnknown> dev;HR(D3D12XboxCreateDevice(nullptr,params,boot,(void**)dev.GetAddressOf()));
    DWORD wire[]={3,0,0,0,W,0,H,0x50001,26,1,0,0x102,0x300004};
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
    auto core=GetModuleHandleW(L"d3d12core.dll");
#ifdef TEST_RADV
    using Image=HRESULT(WINAPI*)(ID3D12Device*,ID3D12Heap*,SIZE_T,BOOL,ID3D12Resource**);
    auto ms=(Image)(void*)GetProcAddress(core,"ScarlettNativeImageV1");CK(ms);
    auto collide=[&](ID3D12Resource **r){return ms(d.Get(),native.Get(),0x14000000,FALSE,r);};
#else
    using Image=HRESULT(WINAPI*)(ID3D12Device*,HANDLE,SIZE_T,SIZE_T,ID3D12Resource**);
    auto ms=(Image)(void*)GetProcAddress(core,"ScarlettMsaaColorV1");CK(ms);
    auto collide=[&](ID3D12Resource **r){return ms(d.Get(),section,pool,0x14000000,r);};
#endif
    ComPtr<ID3D12Resource> conflict;HR(collide(conflict.GetAddressOf()));
    CK(place(wc+off,3,nullptr,IID_ID3D12Resource,&bad)==DXGI_ERROR_INVALID_CALL && !bad);conflict.Reset();
    CK(FAILED(place(wc+off+256,3,nullptr,IID_ID3D12Resource,&bad)) && !bad);
    CK(FAILED(place(wc+off,6,nullptr,IID_ID3D12Resource,&bad)) && !bad);
    CK(FAILED(place(wc+off,3,wire,IID_ID3D12Resource,&bad)) && !bad);
    ComPtr<IUnknown> object;HR(place(wc+off,3,nullptr,IID_ID3D12Resource,object.GetAddressOf()));
    ComPtr<ID3D12Resource> original,linear,read;original.Attach(test_r11_backing(object.Get()));CK(original);
    using Heap=HRESULT(WINAPI*)(IUnknown*,const void*,REFIID,void**);using Start=SIZE_T(WINAPI*)(IUnknown*);
    ComPtr<IUnknown> views;UINT hd[]={0,128,1,1};HR(((Heap)vt[0x70/8])(dev.Get(),hd,IID_ID3D12DescriptorHeap,(void**)views.GetAddressOf()));
    auto start=((Start)((*(void***)views.Get())[0x48/8]))(views.Get());
    ComPtr<IUnknown> staging; hd[1]=0xc38; hd[2]=0;
    HR(((Heap)vt[0x70/8])(dev.Get(),hd,IID_ID3D12DescriptorHeap,(void**)staging.GetAddressOf()));
    SIZE_T src=((Start)((*(void***)staging.Get())[0x48/8]))(staging.Get()), dst=start+59*32;
    DWORD sw[]={26,4,0x1688,0,0,5,0,0},uw[]={26,4,0,0};
    for(UINT i=0;i<11;i++){
        sw[4]=uw[2]=i?(i-1)%5:0; sw[5]=i?1:5;
        HR(xs_write_view(dev.Get(),i<6?0x90:0x98,object.Get(),i<6?sw:uw,src+32*i));
        HR(xs_copy_wc_view(dev.Get(),dst+32*i,src+32*i));
        ID3D12DescriptorHeap *host=nullptr;D3D12_GPU_DESCRIPTOR_HANDLE gpu={};
        CK(FAILED(xs_resolve_view(views.Get(),dst+32*i,i<6?0x90:0x98,&host,&gpu)) && !host && !gpu.ptr);
    }
    uw[2]=5;CK(FAILED(xs_write_view(dev.Get(),0x98,object.Get(),uw,src)));sw[5]=2;CK(FAILED(xs_write_view(dev.Get(),0x90,object.Get(),sw,src)));
    ComPtr<ID3D12DescriptorHeap> typed;HR(xs_copy_wc_view(dev.Get(),dst+64,src+32));
    CK(FAILED(xs_r11_mip_table(dev.Get(),views.Get(),dst,typed.GetAddressOf())) && !typed);
    HR(xs_copy_wc_view(dev.Get(),dst+64,src+64));HR(xs_r11_mip_table(dev.Get(),views.Get(),dst,typed.GetAddressOf()));
    staging.Reset();views.Reset();object.Reset();
    auto table=typed->GetGPUDescriptorHandleForHeapStart();
    D3D12_RESOURCE_DESC b={};b.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;b.Width=bytes;b.Height=b.DepthOrArraySize=b.MipLevels=b.SampleDesc.Count=1;b.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;b.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
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
    for(UINT pass=0;pass<2;pass++){
        if(pass)transition(D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        l->SetPipelineState(pipelines[pass?0:1].Get());UINT linearAt=0;
        for(UINT m=0;m<5;m++){UINT constants[]={m,r11_end[4-m]/4,linearAt};l->SetComputeRoot32BitConstants(2,3,constants,0);
            l->Dispatch(((W>>m)+7)/8,((H>>m)+7)/8,1);linearAt+=(W>>m)*(H>>m);}
    }
    D3D12_RESOURCE_BARRIER br={};br.Transition={linear.Get(),0,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE};l->ResourceBarrier(1,&br);l->CopyBufferRegion(read.Get(),0,linear.Get(),0,bytes);
    if(poolBuffer){D3D12_RESOURCE_BARRIER aliasing={};aliasing.Type=D3D12_RESOURCE_BARRIER_TYPE_ALIASING;aliasing.Aliasing={original.Get(),poolBuffer.Get()};l->ResourceBarrier(1,&aliasing);
        l->CopyBufferRegion(read.Get(),bytes,poolBuffer.Get(),off-65536,bytes+131072);}
    HR(l->Close());
    ComPtr<ID3D12Fence> gate,end;HR(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&gate)));HR(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&end)));HR(q->Wait(gate.Get(),1));ID3D12CommandList *lists[]={l.Get()};q->ExecuteCommandLists(1,lists);HR(q->Signal(end.Get(),1));
    std::vector<UINT> expected(bytes/4,0xa7a7a7a7);
    for(UINT m=0;m<5;m++)for(UINT y=0;y<(H>>m);y++)for(UINT x=0;x<(W>>m);x++){
        UINT j=r11_mip_byte(5,m,x,y)/4,i=y*(W>>m)+x,k=i+13*m;
        ((UINT*)old)[j]=i^(m<<24);expected[j]=((k&1)?0x3c0:0)|((k&2)?(0x3c0u<<11):0)|((k&4)?(0x1e0u<<22):0);
    }
    MemoryBarrier();original.Reset();CK(VirtualFree(wc,0,MEM_RELEASE));
    CK(VirtualAlloc(wc,pool,MEM_RESERVE,PAGE_NOACCESS)==wc);BYTE *reuse=(BYTE*)VirtualAlloc(wc+off,bytes,MEM_COMMIT,PAGE_READWRITE);CK(reuse==wc+off);memset(reuse,0xbd,bytes);
    CK(FAILED(place(reuse,3,nullptr,IID_ID3D12Resource,&bad)) && !bad);
    HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);CK(event);HR(end->SetEventOnCompletion(1,event));CK(WaitForSingleObject(event,50)==WAIT_TIMEOUT);
    CK(collide(conflict.GetAddressOf())==DXGI_ERROR_INVALID_CALL && !conflict);
    HR(gate->Signal(1));CK(WaitForSingleObject(event,10000)==WAIT_OBJECT_0);HR(d->GetDeviceRemovedReason());CloseHandle(event);
    UINT *pixels;HR(read->Map(0,nullptr,(void**)&pixels));UINT linearAt=0;
    for(UINT m=0;m<5;m++)for(UINT i=0;i<(W>>m)*(H>>m);i++){CK(pixels[linearAt++]==((i+13*m)&7));}
    if(poolBuffer)old=(BYTE*)pixels+bytes+65536;
    CK(!memcmp(old,expected.data(),bytes));
    for(UINT j=0;j<bytes;j++)CK(reuse[j]==0xbd);
    for(UINT j=0;j<65536;j++)CK(old[(int)j-65536]==0xa7 && old[bytes+j]==0xa7);
    read->Unmap(0,nullptr);l.Reset();typed.Reset();HR(collide(conflict.GetAddressOf()));conflict.Reset();
#ifndef TEST_RADV
    CloseHandle(section);CK(VirtualFree(alias.base,0,MEM_RELEASE));
#endif
    CK(VirtualFree(wc,0,MEM_RELEASE));
    puts("PASS R11 original five-mip WC: 11 retained views, all 1227600 tiled UAV writes and decoded full/slice SRV loads, padding, exact VA recycle and partial MSAA conflict through fence");
}
