/* Include the adapter for inspection without exporting callback pointers to the title. */
#include "queue.c"

#define CK(c) do { if (!(c)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#c); return 1; } } while (0)
static volatile LONG calls;
static void WINAPI sentinel(void) { InterlockedIncrement(&calls); }

int main(void)
{
    ID3D12Device *d;
    IUnknown *a,*b,*compute,*q,*gate,*done,*list;
    HANDLE event=CreateEventW(NULL,FALSE,FALSE,NULL);
    /* An opaque context cannot be AddRef'ed, sampled, or freed by the host. */
    void *ctx=VirtualAlloc(NULL,4096,MEM_RESERVE,PAGE_NOACCESS);
    CK(ctx && event && SUCCEEDED(D3D12CreateDevice(NULL,D3D_FEATURE_LEVEL_12_0,&IID_ID3D12Device,(void **)&d)));
    CK(SUCCEEDED(xs_allocator(d,D3D12_COMMAND_LIST_TYPE_DIRECT,&a)));
    CK(SUCCEEDED(xs_allocator(d,D3D12_COMMAND_LIST_TYPE_DIRECT,&b)));
    CK(SUCCEEDED(xs_allocator(d,D3D12_COMMAND_LIST_TYPE_COMPUTE,&compute)));
    CK(SUCCEEDED(xs_direct_queue(d,&q)) && SUCCEEDED(xs_shared_fence(d,&gate)) && SUCCEEDED(xs_shared_fence(d,&done)));
    const struct atable *vt=(const struct atable *)a->lpVtbl;
    vt->callback(a,sentinel,ctx);
    vt->callback(b,sentinel,(BYTE *)ctx+1);
    CK(!register_callback(q,sentinel,ctx) && !register_callback(compute,sentinel,ctx));
    CK(!register_callback(a,NULL,ctx) && !register_callback(a,sentinel,NULL) && !register_callback(a,sentinel,(BYTE *)ctx+1));
    CK(!IUnknown_Release(b) && !IUnknown_Release(compute));

    CK(SUCCEEDED(xs_startup_list(d,D3D12_COMMAND_LIST_TYPE_DIRECT,a,FALSE,&list)));
    sl_420(list,0x700000,0,0,0xffffffffff00ull);
    CK(SUCCEEDED(sl_close(list)) && SUCCEEDED(xs_queue_wait(q,gate,1)));
    startup_submit(q,1,&list,0,0);
    CK(SUCCEEDED(queue_prefix.signal(q,done,1)) && SUCCEEDED(xs_fence_event(done,1,event)));
    CK(IUnknown_Release(a)==1 && WaitForSingleObject(event,50)==WAIT_TIMEOUT && !calls);
    CK(SUCCEEDED(xs_cpu_signal(gate,1)) && WaitForSingleObject(event,10000)==WAIT_OBJECT_0);
    /* The list still owns the guest allocator. Only reset after the fence. */
    a=sl(list)->allocator;
    CK(SUCCEEDED(vt->reset(a)));
    struct command_allocator *registration=CONTAINING_RECORD(a,struct command_allocator,a.iface);
    CK(registration->entry==sentinel && registration->context==ctx);
    CK(!IUnknown_Release(list) && !calls && VirtualFree(ctx,0,MEM_RELEASE));
    CK(!IUnknown_Release(gate) && !IUnknown_Release(done) && !IUnknown_Release(q));
    CK(SUCCEEDED(ID3D12Device_GetDeviceRemovedReason(d)));
    ID3D12Device_Release(d);CloseHandle(event);
    puts("PASS allocator +48: isolated borrowed registration, invalid pairs/receiver rejected; list held allocator across gated GPU submission, reset after fence, zero callback invocations through release");
    return 0;
}
