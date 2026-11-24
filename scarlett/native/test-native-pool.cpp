#include <d3d12.h>
#include <cstdio>
#include <cstring>
#include <wrl/client.h>
#include "xmem/native_pool.h"
using Microsoft::WRL::ComPtr;
#define C(x) do{if(!(x)){printf("FAIL %d %s err=%lu\n",__LINE__,#x,GetLastError());return 1;}}while(0)
int main(){setbuf(stdout,nullptr);auto xm=LoadLibraryW(L"xmem.dll");C(xm);using Alloc=void*(WINAPI*)(void*,SIZE_T,DWORD,UINT64,DWORD);using Get=HRESULT(WINAPI*)(ID3D12Device*,const void*,SIZE_T,ID3D12Heap**,ID3D12Resource**,UINT*);auto alloc=(Alloc)(void*)GetProcAddress(xm,"XMemVirtualAlloc");auto take=(Get)(void*)GetProcAddress(xm,"ScarlettAcquireNativePoolV1");C(alloc&&take);BYTE *va=(BYTE*)alloc(nullptr,0x89700000,0x20003000,0x283,0x2404);C(va);ComPtr<ID3D12Device5>d;C(SUCCEEDED(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&d))));UINT off;ComPtr<ID3D12Resource>b;C(take(d.Get(),va+0x1b80,160,nullptr,&b,&off)==S_OK && off==0x1b80 && b->GetGPUVirtualAddress());ComPtr<ID3D12Heap> h;C(take(d.Get(),va+0x896ff000,8192,&h,nullptr,&off)==E_INVALIDARG && !h);
void *cpu=nullptr;C(b->Map(0,nullptr,&cpu)==E_INVALIDARG && !cpu);
struct {UINT64 id;UINT word;} s;C(!native_pool_ctl(0x53430103,va,&s.id,8));s.word=0x3ce94a65;C(!native_pool_ctl(0x53430104,va+0x2000,&s,12));C(*(UINT*)(va+0x2000)==s.word);printf("PASS XMem pool id=%llu same-device buffer=1 staged write=%08x\n",s.id,s.word);
// This vkd3d RemoveDevice is a stub. Its validation rejects an open list before submission and marks the device removed.
D3D12_COMMAND_QUEUE_DESC qd={};ComPtr<ID3D12CommandQueue> q;ComPtr<ID3D12CommandAllocator> a;ComPtr<ID3D12GraphicsCommandList> l;
C(SUCCEEDED(d->CreateCommandQueue(&qd,IID_PPV_ARGS(&q))) && SUCCEEDED(d->CreateCommandAllocator(qd.Type,IID_PPV_ARGS(&a))) && SUCCEEDED(d->CreateCommandList(0,qd.Type,a.Get(),nullptr,IID_PPV_ARGS(&l))));
ID3D12CommandList *open=l.Get();q->ExecuteCommandLists(1,&open);
C(FAILED(d->GetDeviceRemovedReason()));C(take(d.Get(),va+0x1000,4,&h,nullptr,&off)==d->GetDeviceRemovedReason()&&!h);C(VirtualFree(va,0,MEM_RELEASE));C(VirtualAlloc(va,65536,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE)==va);C(native_pool_ctl(0x53430104,va+0x2000,&s,12)<0);C(take(d.Get(),va,4,&h,nullptr,&off)==S_FALSE && !h);b.Reset();d.Reset();C(*(UINT*)(va+0x2000)==0);C(VirtualFree(va,0,MEM_RELEASE));puts("PASS device removed reason propagated, public release/stale write refused, VA reused safely");}
