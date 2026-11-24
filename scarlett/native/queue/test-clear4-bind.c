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
static D3D12_TEXTURE_BARRIER seen[2];static UINT calls;
static void checked_barrier(ID3D12GraphicsCommandList7 *l,UINT n,const D3D12_BARRIER_GROUP *g){
    if(n==1 && g->Type==D3D12_BARRIER_TYPE_TEXTURE && g->NumBarriers==2){memcpy(seen,g->pTextureBarriers,sizeof seen);++calls;}
    ID3D12GraphicsCommandList7_Barrier(l,n,g);
}
#undef ID3D12GraphicsCommandList7_Barrier
#define ID3D12GraphicsCommandList7_Barrier checked_barrier
static UINT om_calls;static SIZE_T last_rt,last_ds;
static void count_om(ID3D12GraphicsCommandList7 *l,UINT n,const D3D12_CPU_DESCRIPTOR_HANDLE *rt,BOOL range,const D3D12_CPU_DESCRIPTOR_HANDLE *ds){
    ++om_calls;last_rt=rt->ptr;last_ds=ds->ptr;ID3D12GraphicsCommandList7_OMSetRenderTargets(l,n,rt,range,ds);
}
#undef ID3D12GraphicsCommandList7_OMSetRenderTargets
#define ID3D12GraphicsCommandList7_OMSetRenderTargets count_om
HRESULT xs_write_view(IUnknown*,UINT,IUnknown*,const void*,SIZE_T);
static UINT bounds_calls;static FLOAT last_bounds[2];static ID3D12GraphicsCommandList7 *bounds_recorder;
static void count_bounds(ID3D12GraphicsCommandList7 *l,FLOAT lo,FLOAT hi){
    ++bounds_calls;last_bounds[0]=lo;last_bounds[1]=hi;bounds_recorder=l;ID3D12GraphicsCommandList7_OMSetDepthBounds(l,lo,hi);
}
#undef ID3D12GraphicsCommandList7_OMSetDepthBounds
#define ID3D12GraphicsCommandList7_OMSetDepthBounds count_bounds
static UINT rects,vps;static ID3D12GraphicsCommandList7 *rect_host,*vp_host;
static void count_rect(ID3D12GraphicsCommandList7 *l,UINT n,const D3D12_RECT *r){if(n==1 && !r->left && !r->top && r->right==3840 && r->bottom==2160){rect_host=l;++rects;}ID3D12GraphicsCommandList7_RSSetScissorRects(l,n,r);}
static void count_vp(ID3D12GraphicsCommandList7 *l,UINT n,const D3D12_VIEWPORT *v){if(n==1 && !v->TopLeftX && !v->TopLeftY && v->Width==3840 && v->Height==2160 && !v->MinDepth && v->MaxDepth==1){vp_host=l;++vps;}ID3D12GraphicsCommandList7_RSSetViewports(l,n,v);}
#undef ID3D12GraphicsCommandList7_RSSetViewports
#undef ID3D12GraphicsCommandList7_RSSetScissorRects
#define ID3D12GraphicsCommandList7_RSSetViewports count_vp
#define ID3D12GraphicsCommandList7_RSSetScissorRects count_rect
static UINT binds;static UINT64 last_va;static ID3D12GraphicsCommandList7 *src_host;
static void count_bind(ID3D12GraphicsCommandList7 *l,UINT i,UINT64 va){if(i==0){++binds;src_host=l;last_va=va;}ID3D12GraphicsCommandList7_SetGraphicsRootShaderResourceView(l,i,va);}
#undef ID3D12GraphicsCommandList7_SetGraphicsRootShaderResourceView
#define ID3D12GraphicsCommandList7_SetGraphicsRootShaderResourceView count_bind
#include "queue.c"
#include "test-depth-slots.h"
#define REQUIRE(e) do{if(!(e)){fprintf(stderr,"FAIL bind line %d: %s\n",__LINE__,#e);return FALSE;}}while(0)
#include "test-quad-read.h"
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

BOOL test_pair_record(ID3D12Device *hd,IUnknown *owner,IUnknown *pso,IUnknown *root,IUnknown **ids,SIZE_T *slots,ID3D12GraphicsCommandList7 *isolated,ID3D12Resource **images,IUnknown **siblings,UINT64 va,IUnknown *duplicate)
{
    IUnknown *a,*l,*q;REQUIRE(xs_allocator(hd,0,&a)==S_OK && xs_device_list(hd,0,a,TRUE,owner,&l)==S_OK);struct startup_list *s=sl(l);
    D3D12_TEXTURE_BARRIER b[2]={0};for(UINT k=0;k<2;k++){
        b[k].pResource=(ID3D12Resource*)ids[k];b[k].Subresources.IndexOrFirstMipLevel=UINT_MAX;
        b[k].SyncBefore=D3D12_BARRIER_SYNC_ALL_SHADING;b[k].SyncAfter=k?D3D12_BARRIER_SYNC_DEPTH_STENCIL:D3D12_BARRIER_SYNC_RENDER_TARGET;
        b[k].AccessBefore=D3D12_BARRIER_ACCESS_SHADER_RESOURCE;b[k].AccessAfter=k?D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE:D3D12_BARRIER_ACCESS_RENDER_TARGET;
        b[k].LayoutBefore=D3D12_BARRIER_LAYOUT_SHADER_RESOURCE;b[k].LayoutAfter=k?D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE:D3D12_BARRIER_LAYOUT_RENDER_TARGET;
    }
    D3D12_BARRIER_GROUP g={.Type=D3D12_BARRIER_TYPE_TEXTURE,.NumBarriers=2,.pTextureBarriers=b};UINT before=calls;
    D3D12_TEXTURE_BARRIER init[2];memcpy(init,b,sizeof init);
    for(UINT k=0;k<2;k++){
        init[k].pResource=images[k];init[k].LayoutBefore=b[k].LayoutAfter;init[k].LayoutAfter=b[k].LayoutBefore;
        init[k].SyncBefore=b[k].SyncAfter;init[k].SyncAfter=b[k].SyncBefore;
        init[k].AccessBefore=b[k].AccessAfter;init[k].AccessAfter=b[k].AccessBefore;
    }
    D3D12_BARRIER_GROUP pre=g;pre.pTextureBarriers=init;s->host->lpVtbl->Barrier(s->host,1,&pre);
    REQUIRE(FAILED(record_primary_barrier(s,1,&g)));pool_vtbl.pipeline(l,pso);pool_vtbl.root(l,root);pool_vtbl.depth_order(l,0);pool_vtbl.stencil(l,0);pool_vtbl.state470(l,UINT64_MAX);pool_vtbl.blend(l,ones_factor);
    b[1].pResource=(ID3D12Resource*)ids[0];REQUIRE(FAILED(record_primary_barrier(s,1,&g)));b[1].pResource=(ID3D12Resource*)ids[1];
    b[1].Flags=1;REQUIRE(FAILED(record_primary_barrier(s,1,&g)));b[1].Flags=0;
    UINT64 *tag=(UINT64*)slots[1];++*tag;REQUIRE(FAILED(record_primary_barrier(s,1,&g)));--*tag;
    REQUIRE(calls==before && !s->attachments[0].identity && !s->attachments[1].identity);pool_vtbl.barrier(l,1,&g);
    REQUIRE(calls==before+1 && FAILED(record_primary_barrier(s,1,&g)) && !s->primary);
    for(UINT k=0;k<2;k++){REQUIRE(s->attachments[k].identity==ids[k] && s->attachments[k].slot==slots[k] && s->attachments[k].serial==*(UINT64*)slots[k]);b[k].pResource=images[k];REQUIRE(!memcmp(b+k,seen+k,offsetof(D3D12_TEXTURE_BARRIER,Flags)+sizeof b[k].Flags));}
    g.pTextureBarriers=seen;isolated->lpVtbl->Barrier(isolated,1,&g);
    UINT ob=om_calls;REQUIRE(record_om(s,1,slots,TRUE,slots+1)==E_NOTIMPL && record_om(s,1,slots,FALSE,NULL)==E_POINTER);
    REQUIRE(FAILED(record_bounds(s,0,1)));
    pool_vtbl.om(l,1,slots,FALSE,slots+1);struct om_views frozen=s->om;
    REQUIRE(FAILED(record_clear_raster(s,1,&clear_rect,FALSE)));UINT nr=rects,nv=vps;
    UINT nb=bounds_calls;
    REQUIRE(FAILED(record_bounds(s,.125f,.375f)) && FAILED(record_bounds(s,-0.f,1.f)) && bounds_calls==nb);
    pool_vtbl.bounds(l,0,1);REQUIRE(s->full_bounds && s->depth_zero_pending && s->pending_470 && !xs_list_host(l) &&
        bounds_calls==nb+1 && bounds_recorder==s->host && last_bounds[0]==0 && last_bounds[1]==1);
    D3D12_RECT bad_rect=clear_rect;bad_rect.left=1;D3D12_VIEWPORT bad_vp=clear_viewport;bad_vp.MaxDepth=.5f;
    REQUIRE(FAILED(record_clear_raster(s,1,&clear_viewport,TRUE)) && FAILED(record_clear_raster(s,2,&clear_rect,FALSE)) && record_clear_raster(s,1,NULL,FALSE)==E_POINTER && FAILED(record_clear_raster(s,1,&bad_rect,FALSE)));
    REQUIRE(FAILED(record_source(s,0,va)));
    pool_vtbl.rect(l,1,&clear_rect);REQUIRE(FAILED(record_clear_raster(s,1,&bad_vp,TRUE)));pool_vtbl.vp(l,1,&clear_viewport);
    REQUIRE(rects==nr+1 && vps==nv+1 && rect_host==s->host && vp_host==s->host);
    REQUIRE(FAILED(record_source(s,0,va)) && IUnknown_Release(duplicate)==0);
    REQUIRE(FAILED(record_source(s,1,va)) && FAILED(record_source(s,0,va+16)) && !binds && !s->source.buffer);
    pool_vtbl.source(l,0,va);UINT64 private_va=ID3D12Resource_GetGPUVirtualAddress(s->source.buffer);
    REQUIRE(private_va!=va && binds==1 && src_host==s->host && last_va==private_va && FAILED(record_source(s,0,va)) && !xs_list_host(l));
    REQUIRE(FAILED(record_clear4_draw(s,3,1,0,0)));pool_vtbl.topology(l,D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    REQUIRE(FAILED(record_clear4_draw(s,4,1,0,0)) && FAILED(record_clear4_draw(s,3,2,0,0)) &&
            FAILED(record_clear4_draw(s,3,1,1,0)) && FAILED(record_clear4_draw(s,3,1,0,1)));
    D3D12_HEAP_PROPERTIES rbh={.Type=D3D12_HEAP_TYPE_READBACK,.CreationNodeMask=1,.VisibleNodeMask=1};
    D3D12_RESOURCE_DESC rbd={.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER,.Width=1536,.Height=1,.DepthOrArraySize=1,.MipLevels=1,.SampleDesc={1,0},.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR};
    ID3D12Resource *quad_rb;REQUIRE(SUCCEEDED(ID3D12Device_CreateCommittedResource(hd,&rbh,0,&rbd,D3D12_RESOURCE_STATE_COPY_DEST,NULL,&IID_ID3D12Resource,(void**)&quad_rb)));
    ID3D12PipelineState *full=s->pipeline;const FLOAT guard[]={1,0,1,1};
    ID3D12GraphicsCommandList7_ClearDepthStencilView(s->host,frozen.cpu[1],D3D12_CLEAR_FLAG_DEPTH|D3D12_CLEAR_FLAG_STENCIL,.375f,0x69,0,NULL);
    ID3D12GraphicsCommandList7_ClearRenderTargetView(s->host,frozen.cpu[0],guard,0,NULL);
    pool_vtbl.state470(l,0xffff00000000ffffULL);REQUIRE(s->pipeline!=full);
    ID3D12PipelineState *diag=s->pipeline;
    REQUIRE(FAILED(record_470(l,0xffff)) && s->pipeline==diag && s->clear4_lanes==0xffff00000000ffffULL);
    pool_vtbl.pipeline(l,pso);REQUIRE(s->pipeline==diag);
    pool_vtbl.draw(l,3,1,0,0);sample_quad(s->host,images[0],quad_rb,0);
    DWORD rt[]={28,4,0,0},ds[]={20,3,0,0,0,0};
    REQUIRE(xs_write_view(owner,0xa8,ids[1],ds,slots[1])==S_OK && FAILED(record_om(s,1,slots,FALSE,slots+1)));
    REQUIRE(xs_write_view(owner,0xa0,siblings[0],rt,slots[0])==S_OK && xs_write_view(owner,0xa8,siblings[1],ds,slots[1])==S_OK);
    REQUIRE(FAILED(record_om(s,1,slots,FALSE,slots+1)) && !memcmp(&frozen,&s->om,sizeof frozen) && om_calls==ob+1);
    s->marker=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof *s->marker);struct prefix_marker *post=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof *post);REQUIRE(s->marker && post);s->marker->refs=1;
    REQUIRE(split_native_list(s,post)==S_OK && om_calls==ob+2 && last_rt==frozen.cpu[0].ptr && last_ds==frozen.cpu[1].ptr);
    REQUIRE(bounds_calls==nb+2 && bounds_recorder==s->host && bounds_recorder!=s->head && last_bounds[0]==0 && last_bounds[1]==1);
    REQUIRE(rects==nr+2 && vps==nv+2 && rect_host==s->host && vp_host==s->host);
    REQUIRE(binds==2 && src_host==s->host && last_va==private_va && s->pending_470);
    ID3D12GraphicsCommandList7_ClearRenderTargetView(s->host,frozen.cpu[0],guard,0,NULL);
    pool_vtbl.draw(l,3,1,0,0);sample_quad(s->host,images[0],quad_rb,1);
    pool_vtbl.state470(l,UINT64_MAX);REQUIRE(s->pipeline==full && s->clear4_lanes==UINT64_MAX);
    pool_vtbl.root(l,root);REQUIRE(binds==3 && src_host==s->host && last_va==private_va);
    pool_vtbl.draw(l,3,1,0,0);sample_quad(s->host,images[0],quad_rb,2);
    const FLOAT green[4]={0,1,0,1};ID3D12GraphicsCommandList7_ClearRenderTargetView(isolated,frozen.cpu[0],green,0,NULL);
    REQUIRE(sl_close(l)==S_OK && xs_direct_queue(hd,&q)==S_OK && checked_startup_submit(q,1,&l,0,0)==E_ACCESSDENIED);
    REQUIRE(execute_quad_fixture(hd,s,quad_rb));ID3D12Resource_Release(quad_rb);
    REQUIRE(FAILED(record_bounds(s,0,1)) && FAILED(sl_reset(l,a,NULL,1)) && s->full_bounds && s->om.heap[1] && s->attachments[0].identity && sl_reset(l,a,NULL,0)==S_OK && !s->om.heap[0] && !s->om.heap[1] && !s->attachments[0].identity && !s->attachments[1].identity && !s->full_bounds && !s->full_rect && !s->full_vp);
    REQUIRE(!s->source.buffer && !s->source.placed);puts("PASS +160: real wrapper, order and ambiguity guards, identical head/tail root VA, reset and pending-submit refusal");
    puts("PASS +230: exact host call on head and split tail, full bounds guarded before bind/after close, reset and submit guards intact");
    IUnknown_Release(l);IUnknown_Release(a);IUnknown_Release(q);return TRUE;
}

/* Copy-only recorder: exercise the same lifetime path without admitting a +470 submission. */
BOOL test_source_lifetime(ID3D12Device *hd,IUnknown *owner,IUnknown *other,UINT64 va,IUnknown *placed,void (*replace)(void*),void *arg)
{
    IUnknown *a,*b,*l,*q,*gate,*done;REQUIRE(xs_allocator(hd,0,&a)==S_OK && xs_allocator(hd,0,&b)==S_OK && xs_device_list(hd,0,a,TRUE,owner,&l)==S_OK);struct startup_list *s=sl(l);
    REQUIRE(FAILED(xs_copy_clear_source(other,hd,s->host,va,&s->source)) && !s->source.buffer);
    REQUIRE(xs_copy_clear_source(owner,hd,s->host,va,&s->source)==S_OK && s->source.placed==placed);
    ID3D12Resource *dst=s->source.buffer,*rb;D3D12_RESOURCE_DESC rd;dst->lpVtbl->GetDesc(dst,&rd);REQUIRE(rd.Width==100);rd.Width=128;
    D3D12_HEAP_PROPERTIES hp={.Type=D3D12_HEAP_TYPE_READBACK,.CreationNodeMask=1,.VisibleNodeMask=1};
    REQUIRE(SUCCEEDED(ID3D12Device_CreateCommittedResource(hd,&hp,0,&rd,D3D12_RESOURCE_STATE_COPY_DEST,NULL,&IID_ID3D12Resource,(void**)&rb)));
    BYTE *bytes;REQUIRE(SUCCEEDED(ID3D12Resource_Map(rb,0,NULL,(void**)&bytes)));memset(bytes,0xe7,128);ID3D12Resource_Unmap(rb,0,NULL);
    D3D12_RESOURCE_BARRIER barrier={.Transition={dst,0,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE}};
    ID3D12GraphicsCommandList7_ResourceBarrier(s->host,1,&barrier);ID3D12GraphicsCommandList7_CopyBufferRegion(s->host,rb,12,dst,0,100);
    REQUIRE(sl_close(l)==S_OK && xs_direct_queue(hd,&q)==S_OK && xs_shared_fence(hd,&gate)==S_OK && xs_shared_fence(hd,&done)==S_OK);
    REQUIRE(xs_queue_wait(q,gate,1)==S_OK && checked_startup_submit(q,1,&l,0,0)==S_OK && ID3D12CommandQueue_Signal(as_queue(q),as_fence(done),1)==S_OK);
    REQUIRE(sl_reset(l,b,NULL,0)==S_OK && !s->source.buffer && !ID3D12Fence_GetCompletedValue(as_fence(done)));
    replace(arg);REQUIRE(FAILED(xs_copy_clear_source(owner,hd,s->host,va,&s->source)) && IUnknown_Release(placed)==1);
    IUnknown_Release(l);HANDLE e=CreateEventW(NULL,FALSE,FALSE,NULL);REQUIRE(e && xs_fence_event(done,1,e)==S_OK && xs_cpu_signal(gate,1)==S_OK && WaitForSingleObject(e,10000)==WAIT_OBJECT_0);
    REQUIRE(SUCCEEDED(ID3D12Resource_Map(rb,0,NULL,(void**)&bytes)));for(UINT k=0;k<128;k++)REQUIRE(bytes[k]==(k>=12 && k<112?(BYTE)(7+3*(k-12)):0xe7));ID3D12Resource_Unmap(rb,0,NULL);
    CloseHandle(e);ID3D12Resource_Release(rb);IUnknown_Release(q);IUnknown_Release(gate);IUnknown_Release(done);IUnknown_Release(a);IUnknown_Release(b);
    puts("PASS +160 fenced copy-only control: all 100 overwritten original bytes, neither snapshot nor replacement VA, both guards, reset/release while queued");return TRUE;
}
