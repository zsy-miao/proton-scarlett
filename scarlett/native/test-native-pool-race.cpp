#include <windows.h>
#include <d3d12.h>
#include <wrl/client.h>
#include <cstdio>
#include "xmem/native_pool.h"
using Microsoft::WRL::ComPtr;
using Take=HRESULT(WINAPI*)(ID3D12Device*,const void*,SIZE_T,ID3D12Heap**,ID3D12Resource**,UINT*);
struct Race {Take take;ID3D12Device *d;BYTE *va;HANDLE start,ready;UINT64 id;LONG hits,misses;};
static DWORD WINAPI race(void *arg){Race *r=(Race*)arg;struct {UINT64 id;UINT word;} write={r->id,0x921e9a};UINT off;ComPtr<ID3D12Resource> pinned;
    if(r->take(r->d,r->va+8192,4,nullptr,&pinned,&off)!=S_OK || off!=8192)return 1;
    SetEvent(r->ready);WaitForSingleObject(r->start,INFINITE);
    for(UINT i=0;i<512;i++){ID3D12Resource *b=nullptr;HRESULT hr=r->take(r->d,r->va+8192,4,nullptr,&b,&off);if(hr==S_OK){if(b!=pinned.Get() || off!=8192)return 2;b->Release();InterlockedIncrement(&r->hits);}else if(hr!=S_FALSE || b)return 3;else InterlockedIncrement(&r->misses);
        native_pool_ctl(0x53430104,r->va+8192,&write,12);
    }return 0;
}
#define CK(x) do{if(!(x)){fprintf(stderr,"FAIL %d %s\n",__LINE__,#x);return 1;}}while(0)
int main(){auto xm=LoadLibraryW(L"xmem.dll");auto alloc=(void*(WINAPI*)(void*,SIZE_T,DWORD,UINT64,DWORD))(void*)GetProcAddress(xm,"XMemVirtualAlloc");Race gen={};gen.take=(Take)(void*)GetProcAddress(xm,"ScarlettAcquireNativePoolV1");CK(gen.take && alloc);gen.va=(BYTE*)alloc(nullptr,0x89700000,0x20003000,0x283,0x2404);CK(gen.va && !native_pool_ctl(0x53430103,gen.va,&gen.id,8));ComPtr<ID3D12Device>d;CK(SUCCEEDED(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&d))));gen.d=d.Get();gen.start=CreateEventW(nullptr,TRUE,FALSE,nullptr);Race r[8];HANDLE threads[8];for(UINT i=0;i<8;i++){r[i]=gen;r[i].ready=CreateEventW(nullptr,TRUE,FALSE,nullptr);threads[i]=CreateThread(nullptr,0,race,&r[i],0,nullptr);CK(threads[i]&&WaitForSingleObject(r[i].ready,5000)==WAIT_OBJECT_0);}SetEvent(gen.start);CK(VirtualFree(gen.va,0,MEM_RELEASE));CK(VirtualAlloc(gen.va,65536,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE)==gen.va);*(UINT*)(gen.va+8192)=0xb712ccdb;LONG hits=0,misses=0;for(UINT i=0;i<8;i++){DWORD exit;CK(WaitForSingleObject(threads[i],10000)==WAIT_OBJECT_0 && GetExitCodeThread(threads[i],&exit)&&!exit);CloseHandle(threads[i]);CloseHandle(r[i].ready);hits+=r[i].hits;misses+=r[i].misses;}CloseHandle(gen.start);CK(hits+misses==4096 && misses && *(UINT*)(gen.va+8192)==0xb712ccdb);CK(VirtualFree(gen.va,0,MEM_RELEASE));printf("PASS concurrent acquire/free/staged write: %ld live, %ld stale, replacement unchanged; eight pre-free pins retired\n",hits,misses);}
