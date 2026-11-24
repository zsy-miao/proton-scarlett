#include "queue.c"
#define CK(c) do { if (!(c)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#c); return 1; } } while (0)
#define HR(c) CK(SUCCEEDED(c))
int main(void)
{
    ID3D12Device *d; IUnknown *a,*b,*list,*q,*gate,*done;
    HANDLE event=CreateEventW(NULL,FALSE,FALSE,NULL); CK(event);
    HR(D3D12CreateDevice(NULL,D3D_FEATURE_LEVEL_12_0,&IID_ID3D12Device,(void **)&d));
    HRESULT (WINAPI *make)(ID3D12Device *,SIZE_T,void **)=(void *)GetProcAddress(GetModuleHandleW(L"d3d12core.dll"),"ScarlettCpuControlViewV1");
    BYTE *page=NULL; CK(make); HR(make(d,65536,(void **)&page)); memset(page,0xa7,65536);
    struct scarlett_section_info alias,deny;
    HR(scarlett_query_section(0x53430120,page+8,4,&alias));CK(alias.base!=page && alias.offset==8 && !alias.section);
    CK(FAILED(scarlett_acquire_section(alias.base,4,&deny)));
    HR(xs_allocator(d,0,&a)); HR(xs_allocator(d,0,&b));
    HR(xs_device_list(d,0,a,TRUE,(IUnknown *)d,&list)); HR(xs_direct_queue(d,&q));
    HR(xs_shared_fence(d,&gate));HR(xs_shared_fence(d,&done));
    struct startup_list *s=sl(list); UINT64 addr=(UINT_PTR)page+8;
    CK(FAILED(prefix_write(s,addr-8,0,0,0)) && FAILED(prefix_write(s,addr+216,0,0,0)) &&
       FAILED(prefix_write(s,addr+1,0,0,0)) && FAILED(prefix_write(s,(UINT_PTR)&s,0,0,0)) &&
       FAILED(prefix_write(s,addr,0,1,0)) && FAILED(prefix_write(s,addr,0,0,1)));
    pool_vtbl.marker(list,addr,0x11111111,0,0); HR(sl_close(list)); HR(sl_reset(list,b,NULL,0));
    CK(*(DWORD *)(page+8)==0xa7a7a7a7); /* Discarded recording. */
    pool_vtbl.marker(list,addr,0,0,0);CK(FAILED(prefix_write(s,addr+4,7,0,0)));
    HR(sl_close(list));CK(FAILED(prefix_write(s,addr,7,0,0)));
    HR(xs_queue_wait(q,gate,1));startup_submit(q,1,&list,0,0);HR(queue_signal(q,done,1));
    HR(sl_reset(list,a,NULL,0));pool_vtbl.marker(list,addr+212,0x53ca1234,0,0);HR(sl_close(list));
    HR(xs_queue_wait(q,gate,2));startup_submit(q,1,&list,0,0);HR(queue_signal(q,done,2));
    CK(!IUnknown_Release(list));CK(VirtualFree(page,65536,MEM_RELEASE));
    CK(VirtualAlloc(page,65536,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE)==page);memset(page,0x37,65536);
    volatile DWORD *words=alias.base;
    HR(xs_fence_event(done,1,event));CK(WaitForSingleObject(event,100)==WAIT_TIMEOUT && words[2]==0xa7a7a7a7);
    HR(xs_cpu_signal(gate,1));CK(WaitForSingleObject(event,10000)==WAIT_OBJECT_0 && words[2]==0);
    words[2]=0x12345678;MemoryBarrier();HR(xs_fence_event(done,2,event));
    CK(WaitForSingleObject(event,100)==WAIT_TIMEOUT && words[55]==0xa7a7a7a7);
    HR(xs_cpu_signal(gate,2));CK(WaitForSingleObject(event,10000)==WAIT_OBJECT_0);
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
    HR(ID3D12Device_GetDeviceRemovedReason(d));ID3D12Device_Release(d);CloseHandle(event);
    puts("PASS +238 two gated submissions, discarded recording, reset/release while pending, first/last marker and CPU clear, original 64KiB alias and exact replacement guards");
    return 0;
}
