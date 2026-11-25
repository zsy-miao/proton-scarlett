/* Standalone RADV control with the production WB import and two real queries. */
#define SCARLETT_PAIR_SUBMIT
#define COBJMACROS
#include <windows.h>
#include <d3d12.h>
#include <stdio.h>
#include "../xmem/marked_section.h"
#include "system_timestamp.h"
#define CK(v) do{if(!(v)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#v);return 1;}}while(0)
#define HR(v) do{HRESULT h_=(v);if(FAILED(h_)){fprintf(stderr,"FAIL line %d: %s = %08lx\n",__LINE__,#v,h_);return 1;}}while(0)
int main(void)
{
    const SIZE_T span=0x49400000,win=0x4190000,off=0x419aa30,ro=0x3fb3050;
    HMODULE xm=LoadLibraryW(L"xmem.dll");CK(xm);
    void *(WINAPI *alloc)(void*,SIZE_T,DWORD,UINT64,DWORD)=(void*)GetProcAddress(xm,"XMemVirtualAlloc");CK(alloc);
    BYTE *pool=alloc(NULL,span,0x20003000,0x183,0x22004);CK(pool);
    UINT64 addr=(UINT_PTR)(pool+off),array=addr,used=array+0x4010;
    DWORD *w=(DWORD*)(pool+ro);memset(w,0,96);w[2]=0x800;w[3]=8;w[14]=2<<16;
    memcpy(w+18,&used,8);memcpy(w+20,&array,8);memcpy(w+22,&array,8);
    struct scarlett_section_info section,alias;HR(scarlett_query_section(0x5343000c,(void*)addr,8,&section));
    HR(scarlett_query_section_handle(0x5343000b,NULL,span,&alias,section.section));
    CK(alias.base!=pool && alias.view_size==span);CloseHandle(section.section);
    BYTE *original=(BYTE*)alias.base+win; memset(original,0xad,65536);
    volatile UINT64 *ticks=(volatile UINT64*)(original+off-win);const UINT64 empty=0xadadadadadadadadull;
    ID3D12Device *d;HR(D3D12CreateDevice(NULL,D3D_FEATURE_LEVEL_12_0,&IID_ID3D12Device,(void**)&d));
    D3D12_COMMAND_QUEUE_DESC qd={0};ID3D12CommandQueue *q;
    HR(ID3D12Device_CreateCommandQueue(d,&qd,&IID_ID3D12CommandQueue,(void**)&q));
    ID3D12CommandAllocator *a[3];ID3D12GraphicsCommandList7 *l[3];
    for(UINT j=0;j<3;j++){
        HR(ID3D12Device_CreateCommandAllocator(d,qd.Type,&IID_ID3D12CommandAllocator,(void**)&a[j]));
        HR(ID3D12Device_CreateCommandList(d,0,qd.Type,a[j],NULL,&IID_ID3D12GraphicsCommandList7,(void**)&l[j]));
    }
    struct system_timestamp *t=NULL; HR(record_timestamp(d,l[0],addr+8,TRUE,&t));
    CK(t->successor_bank && t->retained && t->section && !t->tail_recorded);
    CK(FAILED(record_tail_timestamp(t,l[2],addr))); /* Not a same-list primary release. */
    UINT_PTR owner=(UINT_PTR)w;
    CK(FAILED(successor_target(t,addr,1,1,owner)) && FAILED(successor_target(t,addr,2,1ull<<32|1,owner)) &&
       FAILED(successor_target(t,addr,2,1,owner+8)) && FAILED(successor_target(t,addr+8,2,1,owner)));
    struct system_timestamp stale=*t;
    stale.section=CreateFileMappingW(INVALID_HANDLE_VALUE,NULL,PAGE_READWRITE,0,65536,NULL);CK(stale.section);
    CK(FAILED(successor_target(&stale,addr,2,1,owner)));CloseHandle(stale.section);
    w[3]=16;CK(FAILED(successor_target(t,addr,2,1,owner)));w[3]=8;
    HR(successor_target(t,addr,2,1,owner));
    ID3D12Resource *rt;D3D12_RESOURCE_DESC rd={.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D,.Width=4,.Height=4,
        .MipLevels=1,.DepthOrArraySize=1,.Format=DXGI_FORMAT_R8G8B8A8_UNORM,.SampleDesc={1,0},.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET};
    D3D12_HEAP_PROPERTIES hp={.Type=D3D12_HEAP_TYPE_DEFAULT,.CreationNodeMask=1,.VisibleNodeMask=1};
    HR(ID3D12Device_CreateCommittedResource(d,&hp,0,&rd,D3D12_RESOURCE_STATE_RENDER_TARGET,NULL,&IID_ID3D12Resource,(void**)&rt));
    D3D12_TEXTURE_BARRIER b={.pResource=rt,.SyncBefore=D3D12_BARRIER_SYNC_RENDER_TARGET,.SyncAfter=D3D12_BARRIER_SYNC_NONE,
        .AccessBefore=D3D12_BARRIER_ACCESS_RENDER_TARGET,.AccessAfter=D3D12_BARRIER_ACCESS_NO_ACCESS,
        .LayoutBefore=D3D12_BARRIER_LAYOUT_RENDER_TARGET,.LayoutAfter=D3D12_BARRIER_LAYOUT_COMMON,.Subresources={UINT_MAX,0,0,0,0,0}};
    D3D12_BARRIER_GROUP g={.Type=D3D12_BARRIER_TYPE_TEXTURE,.NumBarriers=1,.pTextureBarriers=&b};
    ID3D12GraphicsCommandList7_Barrier(l[1],1,&g);emit_successor_timestamp(t,l[2]);
    for(UINT j=0;j<3;j++)HR(ID3D12GraphicsCommandList7_Close(l[j]));
    ID3D12Fence *gate,*done;HR(ID3D12Device_CreateFence(d,0,0,&IID_ID3D12Fence,(void**)&gate));
    HR(ID3D12Device_CreateFence(d,0,0,&IID_ID3D12Fence,(void**)&done));
    ID3D12CommandList *ls[]={(ID3D12CommandList*)l[0],(ID3D12CommandList*)l[1],(ID3D12CommandList*)l[2]};
    ID3D12CommandQueue_ExecuteCommandLists(q,1,ls);HR(ID3D12CommandQueue_Signal(q,done,1));
    HR(ID3D12CommandQueue_Wait(q,gate,1));ID3D12CommandQueue_ExecuteCommandLists(q,2,ls+1);HR(ID3D12CommandQueue_Signal(q,done,2));
    HANDLE ev=CreateEventW(NULL,FALSE,FALSE,NULL);CK(ev);HR(ID3D12Fence_SetEventOnCompletion(done,1,ev));CK(WaitForSingleObject(ev,10000)==WAIT_OBJECT_0);
    CK(ticks[1]!=empty && ticks[1] && ticks[0]==empty);
    CK(VirtualFree(pool,0,MEM_RELEASE));BYTE *reuse=VirtualAlloc(pool+win,65536,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);CK(reuse==pool+win);
    memset(reuse,0x49,65536);MemoryBarrier();HR(ID3D12Fence_SetEventOnCompletion(done,2,ev));CK(WaitForSingleObject(ev,50)==WAIT_TIMEOUT);
    UINT64 low,cpu,high;HR(ID3D12CommandQueue_GetClockCalibration(q,&low,&cpu));HR(ID3D12Fence_Signal(gate,1));
    CK(WaitForSingleObject(ev,10000)==WAIT_OBJECT_0 && ID3D12Fence_GetCompletedValue(done)==2);
    HR(ID3D12Device_GetDeviceRemovedReason(d));HR(ID3D12CommandQueue_GetClockCalibration(q,&high,&cpu));
    CK(ticks[0]>=low && ticks[0]<=high && ticks[0]>=ticks[1] && ticks[0]!=empty);
    for(UINT j=0;j<65536;j++){CK(reuse[j]==0x49);if(j<off-win||j>=off-win+16)CK(original[j]==0xad);}
    printf("PASS successor +240: release then EndQuery/Resolve index 1, original WB after exact-VA reuse; head=%llu tail=%llu\n",ticks[1],ticks[0]);
    for(UINT j=0;j<3;j++){ID3D12GraphicsCommandList7_Release(l[j]);ID3D12CommandAllocator_Release(a[j]);}
    drop_timestamp(t);ID3D12Resource_Release(rt);ID3D12Fence_Release(gate);ID3D12Fence_Release(done);
    ID3D12CommandQueue_Release(q);ID3D12Device_Release(d);CloseHandle(ev);
    CK(VirtualFree(reuse,0,MEM_RELEASE) && VirtualFree(alias.base,0,MEM_RELEASE));FreeLibrary(xm);return 0;
}
