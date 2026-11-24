#define COBJMACROS
#define CONST_VTABLE
#include "queue.h"
/* Check mixed-batch and split-head refusals at the host execution boundary. */
static UINT executions;
static void counted_execute(ID3D12CommandQueue *q,UINT n,ID3D12CommandList *const *lists)
{
    ++executions;ID3D12CommandQueue_ExecuteCommandLists(q,n,lists);
}
#undef ID3D12CommandQueue_ExecuteCommandLists
#define ID3D12CommandQueue_ExecuteCommandLists counted_execute
#include "queue.c"
#include "test-depth-slots.h"
#define REQUIRE(e) do{if(!(e)){fprintf(stderr,"FAIL bind line %d: %s\n",__LINE__,#e);return FALSE;}}while(0)
/* Takes the caller's last PSO reference. The submission waits behind a CPU gate. */
BOOL test_clear4_bind(ID3D12Device *hd,IUnknown *owner,IUnknown *other,IUnknown *root,IUnknown *other_root,IUnknown *p,IUnknown *linear)
{
    IUnknown *a,*b,*list,*q,*gate,*done,*tmp=NULL;
    REQUIRE(xs_allocator(hd,0,&a)==S_OK && xs_allocator(hd,0,&b)==S_OK && xs_device_list(hd,0,a,TRUE,owner,&list)==S_OK);
    struct startup_list *s=sl(list);BYTE *cache=(BYTE*)list+0x544;
    MEMORY_BASIC_INFORMATION m;
    REQUIRE(VirtualQuery(list,&m,sizeof m) && m.Protect==PAGE_READONLY && !((struct direct_prefix*)list)->put && !((struct direct_prefix*)list)->limit);
    REQUIRE(VirtualQuery(cache,&m,sizeof m) && m.BaseAddress==cache-4 && m.Protect==PAGE_READWRITE);
    REQUIRE(VirtualQuery(cache-4+4096,&m,sizeof m) && m.State==MEM_RESERVE);
    memcpy(cache,(BYTE*)p+16,32);REQUIRE(guarded_depth_slots(list));
    REQUIRE(!xs_pin_clear4(other,hd,p,&tmp) && FAILED(record_clear4(s,NULL)) && FAILED(record_clear4(s,(void*)1)) && FAILED(record_clear4(s,linear)));
    cache[-1]^=1;REQUIRE(FAILED(record_clear4(s,p)) && FAILED(sl_close(list)));cache[-1]^=1;
    REQUIRE(record_graphics_root(s,other_root)==S_OK && FAILED(record_clear4(s,p)) && !s->bound);
    REQUIRE(FAILED(record_470(list,UINT64_MAX)) && !s->pending_470);
    REQUIRE(sl_close(list)==S_OK && sl_reset(list,b,NULL,0)==S_OK);
    pool_vtbl.pipeline(list,p);REQUIRE(s->bound==p && !xs_list_host(list) && FAILED(record_graphics_root(s,other_root)));
    REQUIRE(FAILED(record_470(list,UINT64_MAX)) && !s->pending_470);
    pool_vtbl.root(list,root);REQUIRE(record_clear4(s,p)==S_OK);
    REQUIRE(FAILED(record_470(list,0)) && FAILED(record_470(list,UINT32_MAX)) && !s->pending_470);
    IUnknown *clean;
    REQUIRE(xs_device_list(hd,0,a,TRUE,other,&clean)==S_OK && FAILED(record_clear4(sl(clean),p)));
    REQUIRE(FAILED(record_470(clean,UINT64_MAX)) && xs_list_host(clean) && FAILED(record_clear4(sl(clean),p)));
    REQUIRE(sl_close(clean)==S_OK);
    s->marker=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof *s->marker);REQUIRE(s->marker);s->marker->refs=1;
    struct prefix_marker *post=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof *post);REQUIRE(post);
    pool_vtbl.stencil(list,0);
    pool_vtbl.state470(list,UINT64_MAX);
    pool_vtbl.blend(list,ones_factor);
    REQUIRE(s->pending_470 && !sl(clean)->pending_470 && record_470(list,UINT64_MAX)==S_OK && !xs_list_host(list));
    REQUIRE(record_primary_clear(s,0,NULL,0,NULL)==E_ACCESSDENIED && !s->primary_cleared);
    REQUIRE(split_native_list(s,post)==S_OK && s->head && s->pipeline && s->graphics_identity==root && s->pending_470 && s->blend_ones && !xs_list_host(list));
    REQUIRE(sl_close(list)==S_OK && FAILED(record_clear4(s,p)) && FAILED(sl_reset(list,a,NULL,1)) && s->bound==p);
    REQUIRE(FAILED(record_470(list,UINT64_MAX)) && s->pending_470 && xs_direct_queue(hd,&q)==S_OK);
    IUnknown *mixed[]={clean,list,clean}; UINT before=executions;
    REQUIRE(checked_startup_submit(q,1,&list,0,0)==E_ACCESSDENIED);
    REQUIRE(FAILED(checked_startup_submit(q,2,mixed,0,0)) && FAILED(checked_startup_submit(q,2,mixed+1,0,0)));
#ifdef SCARLETT_EXPERIMENTAL_TAGGED_BATCH
    REQUIRE(checked_startup_submit(q,1,&list,2,3)==E_ACCESSDENIED);
    REQUIRE(FAILED(checked_startup_submit(q,2,mixed,2,3)) && FAILED(checked_startup_submit(q,2,mixed+1,2,3)));
#endif
    REQUIRE(executions==before);
    IUnknown_Release(clean);
    REQUIRE(s->blend_ones && sl_reset(list,a,NULL,0)==S_OK && !s->blend_ones && !s->pending_470 && !s->stencil_zero && !s->bound && !s->head && !s->graphics_identity);
    REQUIRE(record_depth_order(s,0)==S_OK && record_clear4(s,p)==S_OK && record_stencil(s,0)==S_OK && s->depth_zero_pending && !xs_list_host(list));
    REQUIRE(sl_close(list)==S_OK && xs_shared_fence(hd,&gate)==S_OK && xs_shared_fence(hd,&done)==S_OK);
    REQUIRE(xs_queue_wait(q,gate,1)==S_OK);startup_submit(q,1,&list,0,0);
    REQUIRE(executions==before+1);
    REQUIRE(ID3D12CommandQueue_Signal(as_queue(q),as_fence(done),1)==S_OK && !ID3D12Fence_GetCompletedValue(as_fence(done)));
    REQUIRE(sl_reset(list,b,NULL,0)==S_OK);IUnknown_Release(list);IUnknown_Release(p);
    REQUIRE(xs_pin_clear4(owner,hd,p,&tmp) && tmp==root);IUnknown_Release(p);
    HANDLE ev=CreateEventW(NULL,FALSE,FALSE,NULL);REQUIRE(ev && xs_fence_event(done,1,ev)==S_OK && xs_cpu_signal(gate,1)==S_OK && WaitForSingleObject(ev,10000)==WAIT_OBJECT_0);
    for(UINT j=0;xs_pin_clear4(owner,hd,p,&tmp);++j){IUnknown_Release(p);REQUIRE(j<5000);Sleep(1);}
    CloseHandle(ev);IUnknown_Release(a);IUnknown_Release(b);IUnknown_Release(q);IUnknown_Release(gate);IUnknown_Release(done);
    puts("PASS +c8/+470: exact receiver, PSO and root; opaque state survives split and rejects clear/borrow/submit; reset and fenced retirement");return TRUE;
}
