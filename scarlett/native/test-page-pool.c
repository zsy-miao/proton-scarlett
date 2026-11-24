/* Build with the bootstrap/staged flags, queue/queue.c, d3d12_vm.c, xmem.dll and -ld3d12. */
#include "d3d12_device.c"
#include "xmem/marked_section.h"
__declspec(dllimport) BOOL WINAPI XMemAllocatePhysicalPages(ULONGLONG,PULONG_PTR,PULONG_PTR);
__declspec(dllimport) BOOL WINAPI XMemFreePhysicalPages(ULONG_PTR,PULONG_PTR);
__declspec(dllimport) void *WINAPI XMemMapPhysicalPages(void *,ULONG_PTR,PULONG_PTR);
__declspec(dllimport) void *WINAPI XMemVirtualAlloc(void *,SIZE_T,DWORD,ULONGLONG,DWORD);
#define CK(x) do { if (!(x)) {fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); ExitProcess(1);} } while(0)
#define HR(x) do { HRESULT h_=(x); if (FAILED(h_)) {fprintf(stderr,"FAIL %d: %s = %#lx\n",__LINE__,#x,h_); ExitProcess(1);} } while(0)

static DWORD handle_count(HANDLE h)
{
    LONG (WINAPI *query)(HANDLE,ULONG,void *,ULONG,ULONG *)=(void *)GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"NtQueryObject");
    DWORD basic[16]={0};
    CK(query && !query(h,0,basic,sizeof basic,NULL) && basic[2]);
    return basic[2]; /* OBJECT_BASIC_INFORMATION.HandleCount */
}

int main(void)
{
    static ULONG_PTR ids[TILE_PAGES];
    ULONG_PTR n=TILE_PAGES;
    const SIZE_T bytes=n<<16,chunk=1ULL<<30;
    IUnknown *owner,*other;
    DWORD params[20]={[0]=0x20004,[2]=0x100000,[10]=1,[15]=0x20};
    HR(D3D12XboxCreateDevice(NULL,params,&bootstrap_iid,(void **)&owner));
    HR(D3D12XboxCreateDevice(NULL,params,&bootstrap_iid,(void **)&other));
    struct xbox_device *d=CONTAINING_RECORD(owner,struct xbox_device,iface);
    struct xbox_device *d2=CONTAINING_RECORD(other,struct xbox_device,iface);
    HRESULT (WINAPI *reg)(IUnknown *,UINT64,UINT,HANDLE *)=((void *const *)owner->lpVtbl)[0x250/8];
    BYTE *va=XMemVirtualAlloc(NULL,bytes,0x20402000,0x23,0x2004);
    UINT64 base=(UINT64)va;
    HANDLE token,out=(HANDLE)1;
    CK(va && reg(owner,base,n,NULL)==E_POINTER && reg(owner,base,n,(HANDLE *)1)==E_POINTER);
    CK(FAILED(reg(owner,base,n,&out)) && !out);
    CK(XMemAllocatePhysicalPages(0x380,&n,ids) && XMemMapPhysicalPages(va,n,ids)==va);
    CK(FAILED(reg(owner,base+1,n,&out)) && FAILED(reg(owner,base+65536,n-1,&out)) && !out);

    struct scarlett_section_info orig[6],info;
    DWORD counts[6];
    for (unsigned i=0;i<6;++i) {
        HR(scarlett_query_section(0x5343000c,va+i*chunk,tile_bytes(i),orig+i));
        counts[i]=handle_count(orig[i].section);
    }
    /* Substitute the wrong, identically sized WB section late in a live run.
     * The book still says 'six mapped'; failure must close the captured prefix. */
    BYTE *at=va+4*chunk;
    HR(scarlett_query_section(0x53430007,at,chunk,&info));
    HR(scarlett_query_section_handle(0x5343000d,at,chunk,&info,orig[0].section));
    CK(FAILED(reg(owner,base,n,&out)) && !out && !d->page_pools);
    HR(scarlett_query_section(0x53430007,at,chunk,&info));
    HR(scarlett_query_section_handle(0x5343000d,at,chunk,&info,orig[4].section));
    for (unsigned i=0;i<6;++i) CK(handle_count(orig[i].section)==counts[i]);
    HR(reg(owner,base,n,&token));
    DWORD flags;
    CK(token && GetHandleInformation(token,&flags) && (flags&HANDLE_FLAG_PROTECT_FROM_CLOSE));
    CK(token!=va && token!=d->page_pools && token!=orig[0].section);
    CK(FAILED(pool_retire(d2,token)) && FAILED(pool_retire(d,orig[0].section)));
    for (unsigned i=0;i<6;++i) counts[i]=handle_count(orig[i].section);
    CK(FAILED(reg(owner,base,n,&out)) && !out && !d->page_pools->next);
    for (unsigned i=0;i<6;++i) CK(handle_count(orig[i].section)==counts[i]);
    HR(pool_retire(d,token));
    CK(!GetHandleInformation(token,&flags) && !d->page_pools && FAILED(pool_retire(d,token)));
    HR(reg(owner,base,n,&token));

    /* Dirty the original after registration, so a registration-time snapshot fails. */
    UINT pages[12];
    for (unsigned i=0;i<6;++i) {
        pages[2*i]=(UINT)(i*chunk>>16);
        pages[2*i+1]=(UINT)((i*chunk+tile_bytes(i))>>16)-1;
    }
    for (unsigned j=0;j<12;++j) *(volatile DWORD *)(va+((SIZE_T)pages[j]<<16))=0x1ced0000+j;
    MemoryBarrier();
    CK(XMemMapPhysicalPages(va,n,NULL)==va && VirtualFree(va,0,MEM_RELEASE));
    CK(XMemFreePhysicalPages(n,ids));
    CK(VirtualAlloc(va,bytes,MEM_RESERVE,PAGE_READWRITE)==va);
    for (unsigned j=0;j<12;++j) {
        BYTE *page=va+((SIZE_T)pages[j]<<16);
        CK(VirtualAlloc(page,65536,MEM_COMMIT,PAGE_READWRITE)==page);
        *(volatile DWORD *)page=0xdeaddead;
    }
    CK(FAILED(reg(other,base,n,&out)) && !out && !d2->page_pools);
    CK(!IUnknown_Release(other));

    ID3D12Device *host=d->host;
    ID3D12Device_AddRef(host);
    ID3D12Heap *heap;
    UINT64 offset;
    CK(FAILED(pool_resolve(d,base+65536,n,0,&heap,&offset)) && !heap);
    CK(FAILED(pool_resolve(d,base,n-1,0,&heap,&offset)) && !heap);
    CK(FAILED(pool_resolve(d,base,n,n,&heap,&offset)) && !heap);
    D3D12_RESOURCE_DESC desc={.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER,.Width=65536,.Height=1,
        .DepthOrArraySize=1,.MipLevels=1,.SampleDesc={1,0},.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR};
    ID3D12Resource *src[12],*result;
    for (unsigned j=0;j<12;++j) {
        HR(pool_resolve(d,base,n,pages[j],&heap,&offset));
        HR(ID3D12Device_CreatePlacedResource(host,heap,offset,&desc,D3D12_RESOURCE_STATE_COMMON,NULL,&IID_ID3D12Resource,(void **)&src[j]));
        ID3D12Heap_Release(heap);
    }
    CK(!IUnknown_Release(owner) && !GetHandleInformation(token,&flags));
    for (unsigned i=0;i<6;++i) CK(CloseHandle(orig[i].section));
    D3D12_HEAP_PROPERTIES hp={.Type=D3D12_HEAP_TYPE_READBACK,.CreationNodeMask=1,.VisibleNodeMask=1};
    desc.Width=sizeof pages;
    HR(ID3D12Device_CreateCommittedResource(host,&hp,0,&desc,D3D12_RESOURCE_STATE_COPY_DEST,NULL,&IID_ID3D12Resource,(void **)&result));
    ID3D12CommandQueue *q;
    ID3D12GraphicsCommandList *cl;
    ID3D12CommandAllocator *ca;
    ID3D12Fence *f;
    D3D12_COMMAND_QUEUE_DESC qd={.Type=D3D12_COMMAND_LIST_TYPE_COPY};
    HR(ID3D12Device_CreateCommandQueue(host,&qd,&IID_ID3D12CommandQueue,(void **)&q));
    HR(ID3D12Device_CreateCommandAllocator(host,qd.Type,&IID_ID3D12CommandAllocator,(void **)&ca));
    HR(ID3D12Device_CreateCommandList(host,0,qd.Type,ca,NULL,&IID_ID3D12GraphicsCommandList,(void **)&cl));
    HR(ID3D12Device_CreateFence(host,0,0,&IID_ID3D12Fence,(void **)&f));
    for (unsigned j=0;j<12;++j) ID3D12GraphicsCommandList_CopyBufferRegion(cl,result,4*j,src[j],0,4);
    HR(ID3D12GraphicsCommandList_Close(cl));
    ID3D12CommandList *lists[]={(ID3D12CommandList *)cl};
    ID3D12CommandQueue_ExecuteCommandLists(q,1,lists);
    HR(ID3D12CommandQueue_Signal(q,f,1));
    HANDLE done=CreateEventW(NULL,FALSE,FALSE,NULL); CK(done);
    HR(ID3D12Fence_SetEventOnCompletion(f,1,done)); CK(WaitForSingleObject(done,15000)==WAIT_OBJECT_0);
    DWORD *words;
    HR(ID3D12Resource_Map(result,0,NULL,(void **)&words));
    for (unsigned j=0;j<12;++j) CK(words[j]==0x1ced0000+j && *(volatile DWORD *)(va+((SIZE_T)pages[j]<<16))==0xdeaddead);
    ID3D12Resource_Unmap(result,0,NULL);
    CK(VirtualFree(va,0,MEM_RELEASE) && CloseHandle(done));
    ID3D12Fence_Release(f); ID3D12GraphicsCommandList_Release(cl);
    ID3D12CommandAllocator_Release(ca); ID3D12CommandQueue_Release(q);
    ID3D12Resource_Release(result);
    for (unsigned j=0;j<12;++j) ID3D12Resource_Release(src[j]);
    HR(ID3D12Device_GetDeviceRemovedReason(host)); ID3D12Device_Release(host);
    puts("PASS: +250 six original WB heaps; impostor and duplicate rollback; 12 fenced reads after VA reuse and owner teardown");
    return 0;
}
