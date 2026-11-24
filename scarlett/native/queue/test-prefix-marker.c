#define COBJMACROS
#define CONST_VTABLE
#include <windows.h>
/* In this fixture only, park the first +238 worker between its real store and fence signal. */
static HANDLE stored,resume;
static LONG test_exchange(volatile LONG *word,LONG value)
{
    LONG old=InterlockedExchange(word,value);
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
    pool_vtbl.marker(list,addr,0x11111111,0,0);pool_vtbl.timestamp(list,dst+16,0,0,1); HR(sl_close(list)); HR(sl_reset(list,b,NULL,0));
    CK(*(DWORD *)(page+8)==0xa7a7a7a7); /* Discarded recording. */
    pool_vtbl.marker(list,addr,0,0,0);CK(FAILED(prefix_write(s,addr+4,7,0,0)));
    pool_vtbl.timestamp(list,dst,0,0,1);CK(FAILED(pool_timestamp(s,dst+8,0,0,1)) && FAILED(prefix_write(s,addr+4,7,0,0)));
    HR(sl_close(list));CK(FAILED(pool_timestamp(s,dst,0,0,1)));CK(FAILED(prefix_write(s,addr,7,0,0)));
    HR(xs_queue_wait(q,gate,1));startup_submit(q,1,&list,0,0);HR(queue_signal(q,done,1));
    HR(sl_reset(list,a,NULL,0));pool_vtbl.marker(list,addr+212,0x53ca1234,0,0);pool_vtbl.timestamp(list,dst+8,0,0,1);HR(sl_close(list));
    HR(xs_queue_wait(q,gate,2));startup_submit(q,1,&list,0,0);HR(queue_signal(q,done,2));
    CK(!IUnknown_Release(list));CK(VirtualFree(page,65536,MEM_RELEASE));
    CK(VirtualAlloc(page,65536,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE)==page);memset(page,0x37,65536);
    CK(VirtualFree(sys,0,MEM_RELEASE)); BYTE *reuse=(BYTE *)(dst&~65535ull);
    CK(VirtualAlloc(reuse,65536,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE)==reuse);memset(reuse,0x49,65536);
    volatile DWORD *words=alias.base;
    HR(xs_fence_event(done,1,event));CK(WaitForSingleObject(event,100)==WAIT_TIMEOUT && words[2]==0xa7a7a7a7);
    CK(ticks[0]==empty && ticks[1]==empty && ticks[2]==empty);UINT64 gpu0,cpu0,gpu1,cpu1,hz;
    HR(ID3D12CommandQueue_GetTimestampFrequency(as_queue(q),&hz));
    HR(xs_cpu_signal(gate,1));CK(WaitForSingleObject(stored,10000)==WAIT_OBJECT_0 && words[2]==0);
    CK(WaitForSingleObject(event,100)==WAIT_TIMEOUT && ticks[0]==empty);
    HR(ID3D12CommandQueue_GetClockCalibration(as_queue(q),&gpu0,&cpu0));
    CK(SetEvent(resume) && WaitForSingleObject(event,10000)==WAIT_OBJECT_0);
    puts("PASS +238 store visible while +240 and downstream queue fence still blocked before worker signal");
    UINT64 first=ticks[0];CK(first!=empty && first>=gpu0 && ticks[1]==empty);
    words[2]=0x12345678;MemoryBarrier();HR(xs_fence_event(done,2,event));
    CK(WaitForSingleObject(event,100)==WAIT_TIMEOUT && words[55]==0xa7a7a7a7);
    HR(xs_cpu_signal(gate,2));CK(WaitForSingleObject(event,10000)==WAIT_OBJECT_0);
    HR(ID3D12CommandQueue_GetClockCalibration(as_queue(q),&gpu1,&cpu1));CK(ticks[0]==first && ticks[1]>first && ticks[1]<=gpu1);
    for(UINT i=0;i<65536;i++){if(i<off-base || i>=off-base+16)CK(inspect[i]==0xce);CK(reuse[i]==0x49);}
    printf("PASS +240 gated GPU timestamps %llu < %llu (calibration %llu..%llu at %llu Hz), discarded slot and full original/replacement guards intact\n",first,ticks[1],gpu0,gpu1,hz);
    CK(UnmapViewOfFile(inspect) && VirtualFree(reuse,0,MEM_RELEASE));
    for (UINT i=0;i<65536/4;i++) CK(words[i]==(i==2?0x12345678u:i==55?0x53ca1234u:0xa7a7a7a7u) && ((DWORD *)page)[i]==0x37373737);
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
    puts("PASS +238 two gated submissions, discarded recording, reset/release while pending, first/last marker and CPU clear, original 64KiB alias and exact replacement guards");
    return 0;
}
