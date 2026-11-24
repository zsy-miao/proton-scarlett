#include <windows.h>
#include <d3d12.h>
#include <wrl/client.h>
#include <cstdio>
#include "xmem/marked_section.h"
using Microsoft::WRL::ComPtr;
extern "C" HRESULT WINAPI D3D12XboxCreateDevice(void*,const void*,REFIID,void**);
extern "C" HRESULT xs_color_allocation_info(IUnknown*,UINT,UINT,const void*,D3D12_RESOURCE_ALLOCATION_INFO*);
extern "C" HRESULT xs_msaa_allocation_info1(IUnknown*,UINT,UINT,const void*,D3D12_RESOURCE_ALLOCATION_INFO*,D3D12_RESOURCE_ALLOCATION_INFO1*);
#define CK(x) do{if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);return 1;}}while(0)
#define HR(x) CK(SUCCEEDED(x))
int main(){
    constexpr UINT W=1280,H=720,bytes=4*W*H,off=0x10e00000,pool=0x89700000;
    DWORD params[20]={};params[0]=0x20004;params[2]=0x100000;params[10]=1;params[15]=0x20;
    const IID boot={0x5405c344,0xd457,0x444e,{0xb4,0xdd,0x23,0x66,0xe4,0x5a,0xee,0x39}};
    ComPtr<IUnknown> dev;HR(D3D12XboxCreateDevice(nullptr,params,boot,(void**)dev.GetAddressOf()));
    DWORD wire[]={3,0,0,0,W,0,H,0x10001,42,1,0,0x102,0x300004};
    auto page=(BYTE*)VirtualAlloc(nullptr,8192,MEM_RESERVE,PAGE_NOACCESS);CK(page && VirtualAlloc(page,4096,MEM_COMMIT,PAGE_READWRITE));
    auto prefix=page+4096-sizeof wire;memcpy(prefix,wire,sizeof wire);auto vt=*(void***)dev.Get();
    using Q=D3D12_RESOURCE_ALLOCATION_INFO*(WINAPI*)(D3D12_RESOURCE_ALLOCATION_INFO*,IUnknown*,UINT,UINT,const void*);
    using Q1=D3D12_RESOURCE_ALLOCATION_INFO*(WINAPI*)(D3D12_RESOURCE_ALLOCATION_INFO*,IUnknown*,UINT,UINT,const void*,D3D12_RESOURCE_ALLOCATION_INFO1*);
    D3D12_RESOURCE_ALLOCATION_INFO info={};D3D12_RESOURCE_ALLOCATION_INFO1 detail={};
    CK(((Q)vt[0xc8/8])(&info,dev.Get(),1,1,prefix)==&info && info.SizeInBytes==bytes && info.Alignment==256);
    CK(((Q1)vt[0x360/8])(&info,dev.Get(),1,1,prefix,&detail)==&info && !detail.Offset && detail.SizeInBytes==bytes && detail.Alignment==256);
    for(UINT i=0;i<13;i++)if(i!=1){wire[i]^=1;CK(FAILED(xs_color_allocation_info(dev.Get(),1,1,wire,&info)) && FAILED(xs_msaa_allocation_info1(dev.Get(),1,1,wire,&info,&detail)));wire[i]^=1;}
    CK(FAILED(xs_color_allocation_info(dev.Get(),1,1,prefix+1,&info)) && VirtualFree(page,0,MEM_RELEASE));
    using Alloc=void*(WINAPI*)(void*,SIZE_T,DWORD,UINT64,DWORD);auto alloc=(Alloc)(void*)GetProcAddress(LoadLibraryW(L"xmem.dll"),"XMemVirtualAlloc");CK(alloc);
    BYTE *wc=(BYTE*)alloc(nullptr,pool,0x20003000,0x283,0x2404);CK(wc);
    scarlett_section_info pin,alias;HR(scarlett_query_section(0x53430005,wc+off,bytes,&pin));CK(pin.offset==off && pin.view_size==pool && pin.base==wc);
    HR(scarlett_query_section_handle(0x53430004,nullptr,pool,&alias,pin.section));BYTE *old=(BYTE*)alias.base+off;memset(old-65536,0xa7,bytes+131072);
    ComPtr<ID3D12Device> d;HR(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&d)));
    D3D12_RESOURCE_DESC optimal={};memcpy(&optimal,wire,sizeof wire);optimal.Layout=D3D12_TEXTURE_LAYOUT_UNKNOWN;optimal.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    auto host=d->GetResourceAllocationInfo(1,1,&optimal);printf("RTX optimal R32_UINT UAV bytes=%llx align=%llx; guest 384000/100\n",host.SizeInBytes,host.Alignment);
    using Open=HRESULT(WINAPI*)(ID3D12Device*,SIZE_T,HANDLE,HANDLE*,ID3D12Heap**);auto open=(Open)(void*)GetProcAddress(GetModuleHandleW(L"d3d12core.dll"),"ScarlettWcHeapV1");CK(open);
    ComPtr<ID3D12Heap> heap;HR(open(d.Get(),pool,pin.section,nullptr,heap.GetAddressOf()));CloseHandle(pin.section);
    D3D12_RESOURCE_DESC b={};b.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;b.Width=bytes;b.Height=b.DepthOrArraySize=b.MipLevels=b.SampleDesc.Count=1;b.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;b.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    ComPtr<ID3D12Resource> original,linear,read;HR(d->CreatePlacedResource(heap.Get(),off,&b,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&original)));
    D3D12_HEAP_PROPERTIES hp={};hp.Type=D3D12_HEAP_TYPE_DEFAULT;hp.CreationNodeMask=hp.VisibleNodeMask=1;HR(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&b,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&linear)));
    hp.Type=D3D12_HEAP_TYPE_READBACK;b.Flags=D3D12_RESOURCE_FLAG_NONE;HR(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&b,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&read)));
    BYTE dxil[8192];FILE *file=fopen("tile.dxil","rb");CK(file);size_t len=fread(dxil,1,sizeof dxil,file);CK(len && feof(file));fclose(file);
    ComPtr<ID3D12RootSignature> root;HR(d->CreateRootSignature(0,dxil,len,IID_PPV_ARGS(&root)));D3D12_COMPUTE_PIPELINE_STATE_DESC pd={};pd.pRootSignature=root.Get();pd.CS={dxil,len};ComPtr<ID3D12PipelineState> ps;HR(d->CreateComputePipelineState(&pd,IID_PPV_ARGS(&ps)));
    ComPtr<ID3D12CommandQueue> q;ComPtr<ID3D12CommandAllocator> a;ComPtr<ID3D12GraphicsCommandList> l;D3D12_COMMAND_QUEUE_DESC qd={};HR(d->CreateCommandQueue(&qd,IID_PPV_ARGS(&q)));HR(d->CreateCommandAllocator(qd.Type,IID_PPV_ARGS(&a)));HR(d->CreateCommandList(0,qd.Type,a.Get(),nullptr,IID_PPV_ARGS(&l)));
    l->SetComputeRootSignature(root.Get());l->SetPipelineState(ps.Get());l->SetComputeRootUnorderedAccessView(0,original->GetGPUVirtualAddress());l->SetComputeRootUnorderedAccessView(1,linear->GetGPUVirtualAddress());l->Dispatch(W/8,H/8,1);
    D3D12_RESOURCE_BARRIER br={};br.Transition={linear.Get(),0,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE};l->ResourceBarrier(1,&br);l->CopyResource(read.Get(),linear.Get());HR(l->Close());
    ComPtr<ID3D12Fence> gate,end;HR(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&gate)));HR(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&end)));HR(q->Wait(gate.Get(),1));ID3D12CommandList *lists[]={l.Get()};q->ExecuteCommandLists(1,lists);HR(q->Signal(end.Get(),1));
    auto at=[](UINT x,UINT y){return ((y/8)*160+x/8)*64+(x%4)+(y%4)*4+(x%8/4)*16+(y%8/4)*32;};
    for(UINT y=0;y<H;y++)for(UINT x=0;x<W;x++)((UINT*)old)[at(x,y)]=0x85000000+y*W+x;
    CK(VirtualFree(wc,0,MEM_RELEASE));BYTE *reuse=(BYTE*)VirtualAlloc(wc+off,bytes,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);CK(reuse==wc+off);memset(reuse,0xbd,bytes);
    HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);CK(event);HR(end->SetEventOnCompletion(1,event));CK(WaitForSingleObject(event,50)==WAIT_TIMEOUT);MemoryBarrier();HR(gate->Signal(1));CK(WaitForSingleObject(event,10000)==WAIT_OBJECT_0);HR(d->GetDeviceRemovedReason());CloseHandle(event);
    UINT *pixels;HR(read->Map(0,nullptr,(void**)&pixels));for(UINT y=0;y<H;y++)for(UINT x=0;x<W;x++){UINT i=y*W+x,v=0x85000000+i;CK(pixels[i]==v && ((UINT*)old)[at(x,y)]==(v^0xf0e1d2c3) && ((UINT*)reuse)[i]==0xbdbdbdbd);}read->Unmap(0,nullptr);
    for(UINT j=0;j<65536;j++)CK(old[(int)j-65536]==0xa7 && old[bytes+j]==0xa7);
    CK(VirtualFree(reuse,0,MEM_RELEASE) && VirtualFree(alias.base,0,MEM_RELEASE));puts("PASS +c8/+360, original WC 0x102 explicit tiled UAV buffer read/write, 921600 words, gated exact-VA reuse and guards");
}
