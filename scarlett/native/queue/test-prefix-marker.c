#define COBJMACROS
#define CONST_VTABLE
#include <windows.h>
/* In this fixture only, park the first +238 worker between its real store and fence signal. */
enum { post_value=0x29a5ee15 };
static HANDLE stored,resume,at_post,allow_store,post_stored,allow_tail;
static LONG test_exchange(volatile LONG *word,LONG value)
{
    if(value==post_value && (!SetEvent(at_post) || WaitForSingleObject(allow_store,10000)!=WAIT_OBJECT_0)) ExitProcess(3);
    LONG old=InterlockedExchange(word,value);
    if(value==post_value && (!SetEvent(post_stored) || WaitForSingleObject(allow_tail,10000)!=WAIT_OBJECT_0)) ExitProcess(4);
    if (!value && (!SetEvent(stored) || WaitForSingleObject(resume,10000)!=WAIT_OBJECT_0)) ExitProcess(2);
    return old;
}
#undef InterlockedExchange
#define InterlockedExchange test_exchange
#include "queue.c"
#define CK(c) do { if (!(c)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#c); return 1; } } while (0)
#define HR(c) CK(SUCCEEDED(c))
int main(void)
{
    ID3D12Device *d; IUnknown *a,*b,*list,*q,*gate,*done;
    HANDLE event=CreateEventW(NULL,FALSE,FALSE,NULL); CK(event);
    stored=CreateEventW(NULL,FALSE,FALSE,NULL);resume=CreateEventW(NULL,FALSE,FALSE,NULL);CK(stored && resume);
    at_post=CreateEventW(NULL,FALSE,FALSE,NULL);allow_store=CreateEventW(NULL,FALSE,FALSE,NULL);
    post_stored=CreateEventW(NULL,FALSE,FALSE,NULL);allow_tail=CreateEventW(NULL,FALSE,FALSE,NULL);
    CK(at_post && allow_store && post_stored && allow_tail);
    HR(D3D12CreateDevice(NULL,D3D_FEATURE_LEVEL_12_0,&IID_ID3D12Device,(void **)&d));
    HRESULT (WINAPI *make)(ID3D12Device *,SIZE_T,void **)=(void *)GetProcAddress(GetModuleHandleW(L"d3d12core.dll"),"ScarlettCpuControlViewV1");
    BYTE *page=NULL; CK(make); HR(make(d,65536,(void **)&page)); memset(page,0xa7,65536);
    struct scarlett_section_info alias,deny;
    HR(scarlett_query_section(0x53430120,page+8,4,&alias));CK(alias.base!=page && alias.offset==8 && !alias.section);
    CK(FAILED(scarlett_acquire_section(alias.base,4,&deny)));
    void *(WINAPI *alloc)(void *,SIZE_T,DWORD,UINT64,DWORD)=(void *)GetProcAddress(LoadLibraryW(L"xmem.dll"),"XMemVirtualAlloc");
    const UINT pool=0x49400000,off=0x4196698,base=off&~65535u;
    BYTE *sys=alloc?alloc(NULL,pool,0x20003000,0x183,0x22004):NULL;CK(sys);
    struct scarlett_section_info orig;HR(scarlett_acquire_section(sys+off,8,&orig));CK(orig.offset==off && orig.view_size==pool);
    BYTE *inspect=MapViewOfFile(orig.section,FILE_MAP_READ|FILE_MAP_WRITE,0,base,65536);CK(inspect && CloseHandle(orig.section));
    memset(inspect,0xce,65536);UINT64 dst=(UINT_PTR)sys+off,empty=0xcecececececececeull;
    volatile UINT64 *ticks=(volatile UINT64 *)(inspect+off-base);
    HR(xs_allocator(d,0,&a)); HR(xs_allocator(d,0,&b));
    HR(xs_device_list(d,0,a,TRUE,(IUnknown *)d,&list)); HR(xs_direct_queue(d,&q));
    HR(xs_shared_fence(d,&gate));HR(xs_shared_fence(d,&done));
    struct startup_list *s=sl(list); UINT64 addr=(UINT_PTR)page+8;
    CK(FAILED(prefix_write(s,addr-8,0,0,0)) && FAILED(prefix_write(s,addr+216,0,0,0)) &&
       FAILED(prefix_write(s,addr+1,0,0,0)) && FAILED(prefix_write(s,(UINT_PTR)&s,0,0,0)) &&
       FAILED(prefix_write(s,addr,0,1,0)) && FAILED(prefix_write(s,addr,0,0,1)));
    CK(FAILED(pool_timestamp(s,dst,1,0,1)) && FAILED(pool_timestamp(s,dst,0,1,1)) && FAILED(pool_timestamp(s,dst,0,0,0)) &&
        FAILED(pool_timestamp(s,dst+1,0,0,1)) && FAILED(pool_timestamp(s,addr,0,0,1)) && FAILED(pool_timestamp(s,(UINT_PTR)&addr,0,0,1)));
    /* Two genuine GPU copies bracket the split. Neither may be moved across the CPU store. */
    D3D12_HEAP_PROPERTIES hp={.Type=D3D12_HEAP_TYPE_UPLOAD,.CreationNodeMask=1,.VisibleNodeMask=1};
    D3D12_RESOURCE_DESC rd={.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER,.Width=8,.Height=1,.DepthOrArraySize=1,
        .MipLevels=1,.SampleDesc={1,0},.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR};
    ID3D12Resource *src;HR(ID3D12Device_CreateCommittedResource(d,&hp,0,&rd,D3D12_RESOURCE_STATE_GENERIC_READ,NULL,&IID_ID3D12Resource,(void **)&src));
    UINT64 *pattern;HR(ID3D12Resource_Map(src,0,NULL,(void **)&pattern));
    pool_vtbl.marker(list,addr,0,0,0);pool_vtbl.timestamp(list,dst+16,0,0,1);
    pool_vtbl.marker(list,addr+4,0x11111111,0,0);HR(sl_close(list)); HR(sl_reset(list,b,NULL,0));
    CK(*(DWORD *)(page+8)==0xa7a7a7a7 && *(DWORD *)(page+12)==0xa7a7a7a7 && ticks[2]==empty);
    pool_vtbl.marker(list,addr,0,0,0);CK(FAILED(split_after_timestamp(s,addr+4,7,0,0)));
    pool_vtbl.timestamp(list,dst,0,0,1); CK(FAILED(pool_timestamp(s,dst+8,0,0,1)));
    SIZE_T delta=off-base;
    ID3D12GraphicsCommandList7_CopyBufferRegion(s->host,s->stamp->dst,delta+32,src,0,8);
    CK(FAILED(split_after_timestamp(s,addr+8,7,0,0)) && FAILED(split_after_timestamp(s,addr+4,7,1,0)));
    pool_vtbl.marker(list,addr+4,post_value,0,0);CK(FAILED(split_after_timestamp(s,addr+4,7,0,0)));
    ID3D12GraphicsCommandList7_CopyBufferRegion(s->host,s->stamp->dst,delta+40,src,0,8);
    HR(sl_close(list));volatile DWORD *words=alias.base;
    UINT64 first=0,gpu0,cpu0,gpu1,cpu1; BYTE *reuse=NULL;
    for(UINT pass=1;pass<=2;pass++) {
        *pattern=0x789abcde12340000ull+pass;MemoryBarrier();
        HR(xs_queue_wait(q,gate,pass));startup_submit(q,1,&list,0,0);HR(queue_signal(q,done,pass));
        if(pass==2) {
            HR(sl_reset(list,a,NULL,0));CK(!s->marker && !s->post && !s->stamp && !s->head);
            CK(!IUnknown_Release(list)); CK(VirtualFree(page,65536,MEM_RELEASE));
            CK(VirtualAlloc(page,65536,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE)==page);memset(page,0x37,65536);
            CK(VirtualFree(sys,0,MEM_RELEASE));reuse=(BYTE *)(dst&~65535ull);
            CK(VirtualAlloc(reuse,65536,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE)==reuse);memset(reuse,0x49,65536);
        }
        HR(xs_fence_event(done,pass,event));CK(WaitForSingleObject(event,50)==WAIT_TIMEOUT && words[2]==0xa7a7a7a7 && words[3]==0xa7a7a7a7);
        HR(xs_cpu_signal(gate,pass)); CK(WaitForSingleObject(stored,10000)==WAIT_OBJECT_0 && !words[2]);
        CK(ticks[0]==empty && ticks[4]==empty && ticks[5]==empty);HR(ID3D12CommandQueue_GetClockCalibration(as_queue(q),&gpu0,&cpu0));
        CK(SetEvent(resume) && WaitForSingleObject(at_post,10000)==WAIT_OBJECT_0);
        HR(ID3D12CommandQueue_GetClockCalibration(as_queue(q),&gpu1,&cpu1));
        CK(ticks[0]>=gpu0 && ticks[0]<=gpu1 && ticks[0]>first && ticks[4]==*pattern && ticks[5]==empty && words[3]==0xa7a7a7a7);
        first=ticks[0];CK(SetEvent(allow_store) && WaitForSingleObject(post_stored,10000)==WAIT_OBJECT_0);
        CK(words[3]==post_value && WaitForSingleObject(event,50)==WAIT_TIMEOUT && ticks[5]==empty);
        CK(SetEvent(allow_tail) && WaitForSingleObject(event,10000)==WAIT_OBJECT_0 && ticks[5]==*pattern);
        printf("PASS submission %u: prefix -> timestamp %llu and GPU copy -> post 0x%x -> GPU copy -> fence\n",pass,first,post_value);
        if(pass==1){ticks[0]=ticks[4]=ticks[5]=empty;words[2]=words[3]=0xa7a7a7a7;MemoryBarrier();}
    }
    ID3D12Resource_Unmap(src,0,NULL);ID3D12Resource_Release(src);
    for(UINT i=0;i<65536;i++){if(i<delta || (i>=delta+8 && i<delta+32) || i>=delta+48)CK(inspect[i]==0xce);CK(reuse[i]==0x49);}
    CK(UnmapViewOfFile(inspect) && VirtualFree(reuse,0,MEM_RELEASE));
    for (UINT i=0;i<65536/4;i++) CK(words[i]==(i==2?0u:i==3?post_value:0xa7a7a7a7u) && ((DWORD *)page)[i]==0x37373737);
    CK(VirtualFree(alias.base,65536,MEM_RELEASE) && VirtualFree(page,0,MEM_RELEASE));
    /* An escaped native recorder can already contain GPU work, even after reset. */
    HR(make(d,65536,(void **)&page));
    HR(xs_device_list(d,0,a,TRUE,(IUnknown *)d,&list));
    CK(xs_list_host(list)!=NULL && prefix_write(sl(list),(UINT_PTR)page+8,0,0,0)==E_NOTIMPL);
    HR(sl_close(list));HR(sl_reset(list,b,NULL,0));
    CK(prefix_write(sl(list),(UINT_PTR)page+8,0,0,0)==E_NOTIMPL);
    CK(!IUnknown_Release(list) && VirtualFree(page,65536,MEM_RELEASE));
    IUnknown_Release(a);IUnknown_Release(b);IUnknown_Release(done);IUnknown_Release(gate);IUnknown_Release(q);
    HR(ID3D12Device_GetDeviceRemovedReason(d));ID3D12Device_Release(d);CloseHandle(event);CloseHandle(stored);CloseHandle(resume);
    CloseHandle(at_post);CloseHandle(post_stored);CloseHandle(allow_store);CloseHandle(allow_tail);
    puts("PASS discarded split recording, replay of same closed list, pending reset/release and exact VA reuse; original and replacement guards intact");
    return 0;
}
