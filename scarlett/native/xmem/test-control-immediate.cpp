#include "gpu_buffer.hpp"
#include <wrl/client.h>
#include <cstdio>
using Microsoft::WRL::ComPtr;
#define CK(x) do { if (!(x)) {printf("FAIL %d: %s\n",__LINE__,#x);return 1;} } while(0)
#define HR(x) CK(SUCCEEDED(x))
int main()
{
    const UINT size=65536, words=size/4;
    auto reserve=(void *(WINAPI*)(void*,SIZE_T,DWORD,UINT64,DWORD))(void*)GetProcAddress(LoadLibraryW(L"xmem.dll"),"XMemVirtualAlloc");
    CK(reserve);
    auto *p=(UINT*)reserve(nullptr,size,0x20401000,0x283,0x42404);CK(p);
    ComPtr<ID3D12Device3> d;
    HR(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&d)));
    ImportedBuffer original;
    SIZE_T off=0;HR(original.acquire(d.Get(),p+2,4,&off));CK(off==8);
    for(UINT j=0;j<words;j++)p[j]=0x76100000+j;
    MemoryBarrier();
    auto old=(volatile UINT*)original.cpu();
    CK(old[2]==0x76100002);
    D3D12_FEATURE_DATA_D3D12_OPTIONS3 caps={};
    HR(d->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS3,&caps,sizeof caps));
    CK(caps.WriteBufferImmediateSupportFlags&D3D12_COMMAND_LIST_SUPPORT_FLAG_DIRECT);
    D3D12_COMMAND_QUEUE_DESC desc={};ComPtr<ID3D12CommandQueue> q;ComPtr<ID3D12CommandAllocator>a;
    HR(d->CreateCommandQueue(&desc,IID_PPV_ARGS(&q)));HR(d->CreateCommandAllocator(desc.Type,IID_PPV_ARGS(&a)));
    ComPtr<ID3D12GraphicsCommandList2> l; HR(d->CreateCommandList(0,desc.Type,a.Get(),nullptr,IID_PPV_ARGS(&l)));
    D3D12_WRITEBUFFERIMMEDIATE_PARAMETER marker={original.resource()->GetGPUVirtualAddress()+off,0};
    l->WriteBufferImmediate(1,&marker,nullptr);HR(l->Close());
    ComPtr<ID3D12Fence> gate,done;HR(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&gate)));
    HR(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&done)));
    HR(q->Wait(gate.Get(),1));ID3D12CommandList* batch[]={l.Get()};q->ExecuteCommandLists(1,batch);HR(q->Signal(done.Get(),1));
    HANDLE ev=CreateEventW(nullptr,FALSE,FALSE,nullptr);CK(ev);HR(done->SetEventOnCompletion(1,ev));
    CK(WaitForSingleObject(ev,50)==WAIT_TIMEOUT && old[2]==0x76100002);
    CK(VirtualFree(p,size,MEM_RELEASE)); CK(VirtualAlloc(p,size,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE)==p);
    for(UINT j=0;j<words;j++)p[j]=0x89500000+j;
    MemoryBarrier();CK(old[2]==0x76100002);
    HR(gate->Signal(1));CK(WaitForSingleObject(ev,10000)==WAIT_OBJECT_0); HR(d->GetDeviceRemovedReason());
    for(UINT j=0;j<words;j++)CK(old[j]==(j==2?0:0x76100000+j) && p[j]==0x89500000+j);
    CK(VirtualFree(p,0,MEM_RELEASE) && CloseHandle(ev));
    puts("PASS RADV native immediate: original 64 KiB control backing unchanged behind gate; only page+8 changed from 76100002 to zero after fence; exact VA replacement unchanged");
}
