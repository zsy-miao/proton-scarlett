#include <d3d12.h>
#include <wrl/client.h>
#include <cstdio>
#include "xmem/marked_section.h"
using Microsoft::WRL::ComPtr;
#define CK(x) do{if(!(x)){fprintf(stderr,"FAIL line %u %s\n",__LINE__,#x);return 1;}}while(0)
#define HR(x) do{HRESULT h=(x);if(FAILED(h)){fprintf(stderr,"FAIL line %u hr=%lx %s\n",__LINE__,h,#x);return 1;}}while(0)
int main(){
#ifdef TEST_RADV
    constexpr bool native=true;
#else
    constexpr bool native=false;
#endif
    constexpr UINT pool=0x89700000,span=0x384000,off=native?0x12570000:0x12600000;
    auto xm=LoadLibraryW(L"xmem.dll");CK(xm);using Alloc=void*(WINAPI*)(void*,SIZE_T,DWORD,UINT64,DWORD);
    auto alloc=(Alloc)(void*)GetProcAddress(xm,"XMemVirtualAlloc");CK(alloc);BYTE *base[2];
    ComPtr<ID3D12Device>d;
    if(!native){HR(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&d)));}
    for(auto &b:base){b=(BYTE*)alloc(nullptr,pool,0x20003000,0x283,0x2404);CK(b);fprintf(stderr,"TRACE32 fixture allocation base=%p\n",b);}
    if(native){HR(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&d)));}
    using Acquire=HRESULT(WINAPI*)(ID3D12Device*,const void*,SIZE_T,ID3D12Heap**,ID3D12Resource**,UINT*);
    using Place=HRESULT(WINAPI*)(ID3D12Device*,ID3D12Heap*,HANDLE,SIZE_T,SIZE_T,ID3D12Resource**);
    using ImN=HRESULT(WINAPI*)(ID3D12Device*,ID3D12Heap*,SIZE_T,ID3D12Resource**);
    using ImX=HRESULT(WINAPI*)(ID3D12Device*,HANDLE,SIZE_T,SIZE_T,ID3D12Resource**);
    auto core=GetModuleHandleW(L"d3d12core.dll");auto place=(Place)(void*)GetProcAddress(core,"ScarlettTiledR32BufferV1");
    auto in=(ImN)(void*)GetProcAddress(core,"ScarlettNativeBgraImageV1");auto ix=(ImX)(void*)GetProcAddress(core,"ScarlettBgraImageV1");CK(in&&ix&&place);
    ComPtr<ID3D12Heap> heap[2];HANDLE section[2]={},dup=NULL;
    for(UINT i=0;i<2;i++)if(native){auto take=(Acquire)(void*)GetProcAddress(xm,"ScarlettAcquireNativePoolV1");UINT at=0;CK(take && take(d.Get(),base[i]+off,span,heap[i].GetAddressOf(),nullptr,&at)==S_OK && at==off);}else{scarlett_section_info info;HR(scarlett_query_section(0x53430005,base[i]+off,span,&info));CK(info.base==base[i] && info.offset==off && info.view_size==pool);section[i]=info.section;}
    if(!native)CK(DuplicateHandle(GetCurrentProcess(),section[0],GetCurrentProcess(),&dup,0,FALSE,DUPLICATE_SAME_ACCESS));
    auto r32=[&](UINT id,UINT ofs,ID3D12Resource **out){return place(d.Get(),heap[id].Get(),section[id],pool,ofs,out);};
    auto bg=[&](ID3D12Resource **out){return native?in(d.Get(),heap[0].Get(),off,out):ix(d.Get(),dup,pool,off,out);};
    ComPtr<ID3D12Resource> a,neighbor,other;ID3D12Resource *bad=(ID3D12Resource*)1;
    CK(FAILED(r32(0,off+0x100,&bad)) && !bad);CK(FAILED(place(d.Get(),nullptr,nullptr,pool,off,&bad)) && !bad);
    HR(bg(a.GetAddressOf()));CK(r32(0,off,&bad)==DXGI_ERROR_INVALID_CALL && !bad);CK(r32(0,off+span,&bad)==DXGI_ERROR_INVALID_CALL && !bad);a.Reset();
    HR(r32(0,off,a.GetAddressOf()));HR(r32(0,off+span,neighbor.GetAddressOf()));HR(r32(1,off,other.GetAddressOf()));
    CK(bg(&bad)==DXGI_ERROR_INVALID_CALL && !bad);CK(r32(0,off,&bad)==DXGI_ERROR_INVALID_CALL && !bad);neighbor.Reset();
    auto p=(UINT*)(base[0]+off);for(UINT i=0;i<span/4;i++)p[i]=0x45320000u+i;MemoryBarrier();
    auto rd=a->GetDesc();CK(rd.Width==span && rd.Flags==D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);rd.Flags=D3D12_RESOURCE_FLAG_NONE;
    D3D12_HEAP_PROPERTIES hp={};hp.Type=D3D12_HEAP_TYPE_READBACK;hp.CreationNodeMask=hp.VisibleNodeMask=1;ComPtr<ID3D12Resource> rb;HR(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&rb)));
    D3D12_COMMAND_QUEUE_DESC qd={};ComPtr<ID3D12CommandQueue> q;ComPtr<ID3D12CommandAllocator> al;ComPtr<ID3D12GraphicsCommandList> l;HR(d->CreateCommandQueue(&qd,IID_PPV_ARGS(&q)));HR(d->CreateCommandAllocator(qd.Type,IID_PPV_ARGS(&al)));HR(d->CreateCommandList(0,qd.Type,al.Get(),nullptr,IID_PPV_ARGS(&l)));
    D3D12_RESOURCE_BARRIER b={};b.Transition={a.Get(),0,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE};l->ResourceBarrier(1,&b);l->CopyBufferRegion(rb.Get(),0,a.Get(),0,span);HR(l->Close());
    ComPtr<ID3D12Fence> gate,end;HR(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&gate)));HR(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&end)));HR(q->Wait(gate.Get(),1));ID3D12CommandList *ls[]={l.Get()};q->ExecuteCommandLists(1,ls);HR(q->Signal(end.Get(),1));a.Reset();
    HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);CK(event);HR(end->SetEventOnCompletion(1,event));CK(WaitForSingleObject(event,50)==WAIT_TIMEOUT);CK(bg(&bad)==DXGI_ERROR_INVALID_CALL && !bad);CK(r32(0,off,&bad)==DXGI_ERROR_INVALID_CALL && !bad);
    HR(gate->Signal(1));CK(WaitForSingleObject(event,10000)==WAIT_OBJECT_0);HR(d->GetDeviceRemovedReason());UINT *raw;HR(rb->Map(0,nullptr,(void**)&raw));for(UINT i=0;i<span/4;i++)CK(raw[i]==0x45320000u+i);rb->Unmap(0,nullptr);
    HRESULT hr;ULONGLONG deadline=GetTickCount64()+2000;do{hr=bg(a.GetAddressOf());if(hr!=DXGI_ERROR_INVALID_CALL)break;SwitchToThread();}while(GetTickCount64()<deadline);HR(hr);a.Reset();other.Reset();CloseHandle(event);if(dup)CloseHandle(dup);for(auto s:section)if(s)CloseHandle(s);for(auto addr:base)CK(VirtualFree(addr,0,MEM_RELEASE));
    printf("PASS %s +%x span=%x: original readback 921600 words, identical allocation/duplicate-handle collision, different allocation allowed, sibling adjacency, BGRA/R32 both directions and partial overlap; last app release before gate, collision until GPU retirement, successful reallocation\n",native?"RADV":"RTX",off,span);
}
