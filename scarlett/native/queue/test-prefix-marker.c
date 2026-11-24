#define COBJMACROS
#define CONST_VTABLE
#include <windows.h>
#include <d3d12.h>
#ifdef TEST_FAILED_WAIT_REASON
static LONG fail_reason,failed_checks;
static HRESULT fixture_reason(ID3D12Device *d)
{
    if (!__atomic_load_n(&fail_reason,__ATOMIC_ACQUIRE)) return ID3D12Device_GetDeviceRemovedReason(d);
    InterlockedIncrement(&failed_checks);
    return DXGI_ERROR_DEVICE_REMOVED;
}
#undef ID3D12Device_GetDeviceRemovedReason
#define ID3D12Device_GetDeviceRemovedReason(d) fixture_reason(d)
#endif
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
#include "test-depth-slots.h"
#define CK(c) do { if (!(c)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#c); return 1; } } while (0)
#define HR(c) CK(SUCCEEDED(c))
#ifdef SCARLETT_EXPERIMENTAL_DIRECT_WAIT
static BOOL unmapped(void *p,DWORD ms)
{
    ULONGLONG until=GetTickCount64()+ms;
    do {
        MEMORY_BASIC_INFORMATION mbi;
        if (VirtualQuery(p,&mbi,sizeof mbi)==sizeof mbi && mbi.State==MEM_FREE) return TRUE;
        Sleep(1);
    } while (GetTickCount64()<until);
    return FALSE;
}
struct producer {
    ID3D12Fence *earlier;
    HANDLE published,allow;
    UINT64 *cell,value;
};
static DWORD WINAPI publish_after_earlier(void *ctx)
{
    struct producer *p=ctx;
    HANDLE event=CreateEventW(NULL,FALSE,FALSE,NULL);
    if (!event || FAILED(ID3D12Fence_SetEventOnCompletion(p->earlier,1,event)) ||
        WaitForSingleObject(event,10000)!=WAIT_OBJECT_0 || ID3D12Fence_GetCompletedValue(p->earlier)!=1 ||
        WaitForSingleObject(p->allow,10000)!=WAIT_OBJECT_0) ExitProcess(12);
    __atomic_store_n(p->cell,p->value,__ATOMIC_RELEASE);
    if (!SetEvent(p->published)) ExitProcess(13);
    CloseHandle(event); return 0;
}
#endif
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
    CK(guarded_depth_slots(list));
#ifdef SCARLETT_EXPERIMENTAL_DIRECT_WAIT
    const UINT64 target=0x7fffffff00000011ull,low=target-(1ull<<32);
    UINT64 *cell=alias.base;
    IUnknown *other,*other_done,*other_list;
    HR(xs_direct_queue(d,&other));HR(xs_shared_fence(d,&other_done));
    HR(xs_startup_list(d,0,b,FALSE,&other_list));sl_420(other_list,0x700000,0,0,0xffffffffff00ull);
    HR(sl_close(other_list));
    HANDLE advanced=CreateEventW(NULL,FALSE,FALSE,NULL);CK(advanced);
    CK(FAILED(record_counter_wait(s,addr-8,7,target,1)));
    __atomic_store_n(cell,low,__ATOMIC_RELEASE);
#endif
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
    CK(record_depth_order(s,1)==E_NOTIMPL);
    pool_vtbl.depth_order(list,0);
        CK(unbind_index(s,(const D3D12_INDEX_BUFFER_VIEW *)1)==E_NOTIMPL);pool_vtbl.index(list,NULL);CK(s->depth_zero_pending && !xs_list_host(list));
    pool_vtbl.marker(list,addr+4,0x11111111,0,0);
    CK(s->head && s->depth_zero_pending && !xs_list_host(list));
#ifdef SCARLETT_EXPERIMENTAL_DIRECT_WAIT
    CK(FAILED(record_counter_wait(s,addr-8,7,target,1)));
    HR(sl_close(list));HR(sl_reset(list,b,NULL,0));
    pool_vtbl.marker(list,addr,0,0,0);pool_vtbl.timestamp(list,dst+16,0,0,1);
    pool_vtbl.marker(list,addr+4,post_value,0,0);
    sl_barrier(s,TRUE);
    CK(FAILED(record_counter_wait(s,addr-8,7,target,1))); /* preceding tail work cannot move */
    HR(sl_close(list));HR(sl_reset(list,b,NULL,0));
    pool_vtbl.marker(list,addr,0,0,0);pool_vtbl.timestamp(list,dst+16,0,0,1);
    pool_vtbl.marker(list,addr+4,post_value,0,0);
    pool_vtbl.wait64(list,addr-8,7,target,1); /* discarded while false */
    void *discard_alias=s->marker->alias,*discard_stamp=s->stamp->mapping;
#endif
    HR(sl_close(list)); HR(sl_reset(list,b,NULL,0));
    CK(!s->depth_zero_pending);
#ifdef SCARLETT_EXPERIMENTAL_DIRECT_WAIT
    CK(unmapped(discard_alias,10) && unmapped(discard_stamp,10));
    puts("PASS discard before submission released both original retained mappings without publishing");
#endif
    CK(*(DWORD *)(page+8)==0xa7a7a7a7 && *(DWORD *)(page+12)==0xa7a7a7a7 && ticks[2]==empty);
    pool_vtbl.marker(list,addr,0,0,0);CK(FAILED(split_after_timestamp(s,addr+4,7,0,0)));
    pool_vtbl.timestamp(list,dst,0,0,1); CK(FAILED(pool_timestamp(s,dst+8,0,0,1)));
    SIZE_T delta=off-base;
    ID3D12GraphicsCommandList7_CopyBufferRegion(s->host,s->stamp->dst,delta+32,src,0,8);
    CK(FAILED(split_after_timestamp(s,addr+8,7,0,0)) && FAILED(split_after_timestamp(s,addr+4,7,1,0)));
    pool_vtbl.marker(list,addr+4,post_value,0,0);CK(FAILED(split_after_timestamp(s,addr+4,7,0,0)));
#ifdef SCARLETT_EXPERIMENTAL_DIRECT_WAIT
    BYTE *foreign; HR(make(d,65536,(void **)&foreign));
    CK(FAILED(record_counter_wait(s,addr,7,target,1)) && FAILED(record_counter_wait(s,(UINT_PTR)foreign,7,target,1)) &&
       FAILED(record_counter_wait(s,addr-8,7,target,0)) && FAILED(record_counter_wait(s,addr-8,6,target,1)));
    CK(VirtualFree(foreign,65536,MEM_RELEASE));
    __atomic_store_n(cell,target,__ATOMIC_RELEASE); /* true now; false when actually executed */
    pool_vtbl.wait64(list,addr-8,7,target,1);
    CK(FAILED(record_counter_wait(s,addr-8,7,target,1)));
    void *job_alias=s->marker->alias,*retire_mapping=s->stamp->mapping;
    struct producer prod={.cell=cell,.value=target};
    HR(ID3D12Device_CreateFence(d,0,0,&IID_ID3D12Fence,(void **)&prod.earlier));
    prod.allow=CreateEventW(NULL,FALSE,FALSE,NULL);prod.published=CreateEventW(NULL,FALSE,FALSE,NULL);
    CK(prod.allow && prod.published);
    HANDLE publisher=CreateThread(NULL,0,publish_after_earlier,&prod,0,NULL);CK(publisher);
#endif
    ID3D12GraphicsCommandList7_CopyBufferRegion(s->host,s->stamp->dst,delta+40,src,0,8);
    pool_vtbl.depth_order(list,0);
    HR(sl_close(list));volatile DWORD *words=alias.base;
    CK(!xs_list_host(list) && record_depth_order(s,0)==E_NOTIMPL && FAILED(unbind_index(s,NULL)));
    CK(FAILED(sl_reset(list,b,NULL,1)) && s->depth_zero_pending);
    UINT64 first=0,gpu0,cpu0,gpu1,cpu1; BYTE *reuse=NULL;
    for(UINT pass=1;pass<=2;pass++) {
#ifdef SCARLETT_EXPERIMENTAL_DIRECT_WAIT
        __atomic_store_n(cell,low,__ATOMIC_RELEASE);
#endif
        *pattern=0x789abcde12340000ull+pass;MemoryBarrier();
        HR(xs_queue_wait(q,gate,pass));
#ifdef SCARLETT_EXPERIMENTAL_DIRECT_WAIT
        if (pass==1) {
            startup_submit(q,1,&other_list,0,0); /* genuine earlier GPU barrier and fence on q */
            HR(ID3D12CommandQueue_Signal(as_queue(q),prod.earlier,1));
            CK(ID3D12Fence_GetCompletedValue(prod.earlier)==0);
        }
#endif
        startup_submit(q,1,&list,0,0);HR(queue_signal(q,done,pass));
        if(pass==2) {
            HR(sl_reset(list,a,NULL,0));CK(!s->marker && !s->post && !s->stamp && !s->head && !s->depth_zero_pending);
            CK(!IUnknown_Release(list)); CK(VirtualFree(page,65536,MEM_RELEASE));
             CK(VirtualAlloc(page,65536,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE)==page);memset(page,0x37,65536);
#ifdef SCARLETT_EXPERIMENTAL_DIRECT_WAIT
             *(UINT64 *)page=UINT64_MAX;
#endif
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
        CK(SetEvent(allow_tail));
#ifdef SCARLETT_EXPERIMENTAL_DIRECT_WAIT
        startup_submit(other,1,&other_list,0,0);HR(queue_signal(other,other_done,pass));
        HR(xs_fence_event(other_done,pass,advanced));CK(WaitForSingleObject(advanced,10000)==WAIT_OBJECT_0);
        CK(WaitForSingleObject(event,150)==WAIT_TIMEOUT && ticks[5]==empty && words[3]==post_value);
        if (pass==1) {
            CK(ID3D12Fence_GetCompletedValue(prod.earlier)==1 && *cell==low);
            CK(SetEvent(prod.allow) && WaitForSingleObject(prod.published,10000)==WAIT_OBJECT_0 &&
               WaitForSingleObject(publisher,10000)==WAIT_OBJECT_0 && *cell==target);
            puts("PASS CPU publisher dependent on an earlier same-direct-queue GPU completion released wait");
        }
        else {
#ifdef TEST_FAILED_WAIT_REASON
            /* Retire from a timer wake without completing anything on the healthy host queue. */
            __atomic_store_n(&fail_reason,1,__ATOMIC_RELEASE);
            CK(unmapped(job_alias,10000));
            DWORD wake=WaitForSingleObject(event,150);
            fprintf(stderr,"fault observer: wake=%lu tail=%llx completed=%llu cell=%llx retired=%d failures=%ld\n",
                wake,ticks[5],ID3D12Fence_GetCompletedValue(as_fence(done)),*cell,
                unmapped(retire_mapping,10),__atomic_load_n(&failed_checks,__ATOMIC_ACQUIRE));
            CK(wake==WAIT_TIMEOUT && ticks[5]==empty && ID3D12Fence_GetCompletedValue(as_fence(done))==1 &&
                *cell==low && !unmapped(retire_mapping,10) && __atomic_load_n(&failed_checks,__ATOMIC_ACQUIRE)==1);
            puts("PASS injected FAILED device reason on timer wake: observer alias unmapped, no more checks, suffix/fence still pending; GPU retirement remains pinned");
            fflush(stdout);
            /* Real device is healthy and the suffix was submitted. There is no safe
             * COM-release shortcut or success signal; end this isolated fixture. */
            ExitProcess(0);
#else
            __atomic_store_n(cell,UINT64_MAX,__ATOMIC_RELEASE);
#endif
        }
#endif
        CK(WaitForSingleObject(event,10000)==WAIT_OBJECT_0 && ticks[5]==*pattern);
        printf("PASS submission %u: prefix -> timestamp %llu and GPU copy -> post 0x%x -> GPU copy -> fence\n",pass,first,post_value);
        if(pass==1){ticks[0]=ticks[4]=ticks[5]=empty;words[2]=words[3]=0xa7a7a7a7;MemoryBarrier();}
    }
    ID3D12Resource_Unmap(src,0,NULL);ID3D12Resource_Release(src);
    for(UINT i=0;i<65536;i++){if(i<delta || (i>=delta+8 && i<delta+32) || i>=delta+48)CK(inspect[i]==0xce);CK(reuse[i]==0x49);}
    CK(UnmapViewOfFile(inspect) && VirtualFree(reuse,0,MEM_RELEASE));
    UINT start=0;
#ifdef SCARLETT_EXPERIMENTAL_DIRECT_WAIT
    CK(*cell==UINT64_MAX && *(UINT64 *)page==UINT64_MAX); start=2;
    CK(unmapped(job_alias,10000) && unmapped(retire_mapping,10000));
    CloseHandle(publisher);CloseHandle(prod.published);CloseHandle(prod.allow);ID3D12Fence_Release(prod.earlier);
    puts("PASS final replay drained; worker alias and retirement timestamp mapping both unmapped");
    IUnknown_Release(other_list);IUnknown_Release(other_done);IUnknown_Release(other);CloseHandle(advanced);
    puts("PASS experimental +250: low with identical low DWORD held tail and fence while independent queue retired; equal and unsigned-above released");
#endif
    for (UINT i=start;i<65536/4;i++) CK(words[i]==(i==2?0u:i==3?post_value:0xa7a7a7a7u) && ((DWORD *)page)[i]==0x37373737);
    CK(VirtualFree(alias.base,65536,MEM_RELEASE) && VirtualFree(page,0,MEM_RELEASE));
    /* An escaped native recorder can already contain GPU work, even after reset. */
    HR(make(d,65536,(void **)&page));
    HR(xs_device_list(d,0,a,TRUE,(IUnknown *)d,&list));
    CK(xs_list_host(list)!=NULL && prefix_write(sl(list),(UINT_PTR)page+8,0,0,0)==E_NOTIMPL);
    CK(record_depth_order(sl(list),0)==E_NOTIMPL);
    HR(sl_close(list));HR(sl_reset(list,b,NULL,0));
    CK(record_depth_order(sl(list),0)==E_NOTIMPL);
    CK(prefix_write(sl(list),(UINT_PTR)page+8,0,0,0)==E_NOTIMPL);
    CK(!IUnknown_Release(list) && VirtualFree(page,65536,MEM_RELEASE));
    IUnknown_Release(a);IUnknown_Release(b);IUnknown_Release(done);IUnknown_Release(gate);IUnknown_Release(q);
    HR(ID3D12Device_GetDeviceRemovedReason(d));ID3D12Device_Release(d);CloseHandle(event);CloseHandle(stored);CloseHandle(resume);
    CloseHandle(at_post);CloseHandle(post_stored);CloseHandle(allow_store);CloseHandle(allow_tail);
    puts("PASS discarded split recording, replay of same closed list, pending reset/release and exact VA reuse; original and replacement guards intact");
    return 0;
}
