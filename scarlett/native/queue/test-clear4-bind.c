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
static D3D12_TEXTURE_BARRIER seen[2];static UINT calls,barrier_total;
static ID3D12GraphicsCommandList7 *barrier_host;
static void checked_barrier(ID3D12GraphicsCommandList7 *l,UINT n,const D3D12_BARRIER_GROUP *g){
    ++barrier_total;barrier_host=l;
    if(n==1 && g->Type==D3D12_BARRIER_TYPE_TEXTURE && g->NumBarriers==2){memcpy(seen,g->pTextureBarriers,sizeof seen);++calls;}
    if(n==1 && g->Type==D3D12_BARRIER_TYPE_TEXTURE && g->NumBarriers==1)seen[0]=g->pTextureBarriers[0];
    ID3D12GraphicsCommandList7_Barrier(l,n,g);
}
#undef ID3D12GraphicsCommandList7_Barrier
#define ID3D12GraphicsCommandList7_Barrier checked_barrier
static UINT om_calls;static SIZE_T last_rt,last_ds;
static void count_om(ID3D12GraphicsCommandList7 *l,UINT n,const D3D12_CPU_DESCRIPTOR_HANDLE *rt,BOOL range,const D3D12_CPU_DESCRIPTOR_HANDLE *ds){
    ++om_calls;last_rt=rt->ptr;last_ds=ds?ds->ptr:0;ID3D12GraphicsCommandList7_OMSetRenderTargets(l,n,rt,range,ds);
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
static UINT topo_calls;static ID3D12GraphicsCommandList7 *ia_list;static D3D_PRIMITIVE_TOPOLOGY ia_value;
static void probe_topology(ID3D12GraphicsCommandList7 *l,D3D_PRIMITIVE_TOPOLOGY t){ia_list=l;ia_value=t;++topo_calls;ID3D12GraphicsCommandList7_IASetPrimitiveTopology(l,t);}
#undef ID3D12GraphicsCommandList7_IASetPrimitiveTopology
#define ID3D12GraphicsCommandList7_IASetPrimitiveTopology probe_topology
#ifdef TEST_LATE_COLOUR
extern IUnknown *late_root_id(BOOL),*publish_late(void);
static UINT roots,psos;static ID3D12RootSignature *seen_root;static ID3D12GraphicsCommandList7 *seen_rec;
static void probe_root(ID3D12GraphicsCommandList7 *l,ID3D12RootSignature *r){++roots;seen_rec=l;seen_root=r;ID3D12GraphicsCommandList7_SetGraphicsRootSignature(l,r);}
static void probe_pso(ID3D12GraphicsCommandList7 *l,ID3D12PipelineState *p){++psos;seen_rec=l;ID3D12GraphicsCommandList7_SetPipelineState(l,p);}
static UINT creates;static ID3D12PipelineState *last_initial;
static HRESULT probe_create(ID3D12Device *d,UINT node,D3D12_COMMAND_LIST_TYPE t,ID3D12CommandAllocator *a,ID3D12PipelineState *p,REFIID iid,void **result){
    last_initial=p;++creates;return ID3D12Device_CreateCommandList(d,node,t,a,p,iid,result);
}
#undef ID3D12Device_CreateCommandList
#define ID3D12Device_CreateCommandList probe_create
#undef ID3D12GraphicsCommandList7_SetGraphicsRootSignature
#define ID3D12GraphicsCommandList7_SetGraphicsRootSignature probe_root
#undef ID3D12GraphicsCommandList7_SetPipelineState
#define ID3D12GraphicsCommandList7_SetPipelineState probe_pso
#endif
static UINT vb_calls;static ID3D12GraphicsCommandList7 *vb_rec;static D3D12_VERTEX_BUFFER_VIEW last_vb;
static void probe_vb(ID3D12GraphicsCommandList7 *l,UINT slot,UINT count,const D3D12_VERTEX_BUFFER_VIEW *v){if(!slot && count==1){++vb_calls;vb_rec=l;last_vb=*v;}ID3D12GraphicsCommandList7_IASetVertexBuffers(l,slot,count,v);}
#undef ID3D12GraphicsCommandList7_IASetVertexBuffers
#define ID3D12GraphicsCommandList7_IASetVertexBuffers probe_vb
#ifdef TEST_INLINE_B27
extern IUnknown *inline_pair[2];
static ID3D12DescriptorHeap *bank_pair[2];static UINT tables;
static ID3D12GraphicsCommandList7 *table_rec;
static D3D12_GPU_DESCRIPTOR_HANDLE table_gpu;
static void probe_pair(ID3D12GraphicsCommandList7 *l,UINT n,ID3D12DescriptorHeap *const *banks){
    if(n==2)memcpy(bank_pair,banks,sizeof bank_pair);
    ID3D12GraphicsCommandList7_SetDescriptorHeaps(l,n,banks);
}
static inline void probe_table(ID3D12GraphicsCommandList7 *l,UINT index,D3D12_GPU_DESCRIPTOR_HANDLE g){
    if(!index){tables++;table_rec=l;table_gpu=g;}ID3D12GraphicsCommandList7_SetGraphicsRootDescriptorTable(l,index,g);
}
#undef ID3D12GraphicsCommandList7_SetDescriptorHeaps
#define ID3D12GraphicsCommandList7_SetDescriptorHeaps probe_pair
#undef ID3D12GraphicsCommandList7_SetGraphicsRootDescriptorTable
#define ID3D12GraphicsCommandList7_SetGraphicsRootDescriptorTable probe_table
#endif
#include "queue.c"
#include "test-depth-slots.h"
#define REQUIRE(e) do{if(!(e)){fprintf(stderr,"FAIL bind line %d: %s\n",__LINE__,#e);return FALSE;}}while(0)
#include "test-quad-read.h"
#include "test-rt-roundtrip.h"
#ifdef TEST_DEFER_RELEASE
#include "test-deferred-release.h"
#endif
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
#ifdef TEST_DISTINCT_PRIMARY
    extern IUnknown *test_primary_image(IUnknown*,ID3D12Device*);
    IUnknown *pk=test_primary_image(owner,hd);REQUIRE(pk);
    D3D12_TEXTURE_BARRIER acquire={.pResource=(ID3D12Resource*)pk,.Subresources.IndexOrFirstMipLevel=UINT_MAX,
        .SyncBefore=D3D12_BARRIER_SYNC_ALL_SHADING,.SyncAfter=D3D12_BARRIER_SYNC_RENDER_TARGET,
        .AccessBefore=D3D12_BARRIER_ACCESS_UNORDERED_ACCESS,.AccessAfter=D3D12_BARRIER_ACCESS_RENDER_TARGET,
        .LayoutBefore=D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS,.LayoutAfter=D3D12_BARRIER_LAYOUT_RENDER_TARGET};
    D3D12_BARRIER_GROUP ag={.Type=D3D12_BARRIER_TYPE_TEXTURE,.NumBarriers=1,.pTextureBarriers=&acquire};
    pool_vtbl.barrier(l,1,&ag);ID3D12Resource *primary=s->primary;SIZE_T ps=slots[0]+32;
    REQUIRE(primary && primary!=images[0] && s->primary_key==(UINT_PTR)pk && xs_write_view(owner,0xa0,pk,(DWORD[]){28,4,0,0},ps)==S_OK);
    REQUIRE(record_primary_clear(s,ps,(FLOAT[4]){0},0,NULL)==S_OK);
    UINT64 ps_serial=s->clear_serial;REQUIRE(ps_serial && s->clear_slot==ps);
#endif
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
    REQUIRE(calls==before+1 && FAILED(record_primary_barrier(s,1,&g)));
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
    REQUIRE(FAILED(record_clear4_draw(s,3,1,0,0)) && !can_set_topology(s,17));pool_vtbl.topology(l,D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    REQUIRE(FAILED(record_clear4_draw(s,4,1,0,0)) && FAILED(record_clear4_draw(s,3,2,0,0)) &&
            FAILED(record_clear4_draw(s,3,1,1,0)) && FAILED(record_clear4_draw(s,3,1,0,1)));
    D3D12_HEAP_PROPERTIES rbh={.Type=D3D12_HEAP_TYPE_READBACK,.CreationNodeMask=1,.VisibleNodeMask=1};
    D3D12_RESOURCE_DESC rbd={.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER,.Width=2048,.Height=1,.DepthOrArraySize=1,.MipLevels=1,.SampleDesc={1,0},.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR};
    ID3D12Resource *quad_rb;REQUIRE(SUCCEEDED(ID3D12Device_CreateCommittedResource(hd,&rbh,0,&rbd,D3D12_RESOURCE_STATE_COPY_DEST,NULL,&IID_ID3D12Resource,(void**)&quad_rb)));
#ifdef TEST_DISTINCT_PRIMARY
    BYTE *qp;REQUIRE(SUCCEEDED(ID3D12Resource_Map(quad_rb,0,NULL,(void**)&qp)));
    memset(qp+512,0xcc,512);ID3D12Resource_Unmap(quad_rb,0,NULL);
#endif
    ID3D12PipelineState *full=s->pipeline;const FLOAT guard[]={1,0,1,1};
    ID3D12GraphicsCommandList7_ClearDepthStencilView(s->host,frozen.cpu[1],D3D12_CLEAR_FLAG_DEPTH|D3D12_CLEAR_FLAG_STENCIL,.375f,0x69,0,NULL);
    ID3D12GraphicsCommandList7_ClearRenderTargetView(s->host,frozen.cpu[0],guard,0,NULL);
    pool_vtbl.state470(l,0xffff00000000ffffULL);REQUIRE(s->pipeline!=full);
    ID3D12PipelineState *diag=s->pipeline;
    REQUIRE(FAILED(record_470(l,0xffff)) && s->pipeline==diag && s->clear4_lanes==0xffff00000000ffffULL);
    pool_vtbl.pipeline(l,pso);REQUIRE(s->pipeline==diag);
#ifdef TEST_LATE_COLOUR
    REQUIRE(FAILED(record_graphics_root(s,late_root_id(FALSE))) && !s->prior_pso);
#endif
    pool_vtbl.draw(l,3,1,0,0);sample_quad(s->host,images[0],quad_rb,0);
    REQUIRE(!can_set_topology(s,17));
    REQUIRE(test_rt_roundtrip(s,ids[0],siblings[0],images[0],FALSE));
    pool_vtbl.state470(l,UINT64_MAX);pool_vtbl.draw(l,3,1,0,0);
    UINT nia=topo_calls;pool_vtbl.topology(l,17);
    REQUIRE(s->guest_topology==17 && topo_calls==nia+1 && ia_list==s->host && ia_value==D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST &&
            !can_set_topology(s,16) && !can_set_topology(s,18) && FAILED(record_clear4_draw(s,3,1,0,0)) && !xs_list_host(l));
    pool_vtbl.state470(l,0xffff00000000ffffULL);
    DWORD rt[]={28,4,0,0},ds[]={20,3,0,0,0,0};
    REQUIRE(xs_write_view(owner,0xa8,ids[1],ds,slots[1])==S_OK && FAILED(record_om(s,1,slots,FALSE,slots+1)));
    REQUIRE(xs_write_view(owner,0xa0,siblings[0],rt,slots[0])==S_OK && xs_write_view(owner,0xa8,siblings[1],ds,slots[1])==S_OK);
    REQUIRE(FAILED(record_om(s,1,slots,FALSE,slots+1)) && !memcmp(&frozen,&s->om,sizeof frozen) && om_calls==ob+1);
    REQUIRE(test_rt_roundtrip(s,ids[0],siblings[0],images[0],FALSE));
    s->marker=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof *s->marker);struct prefix_marker *post=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof *post);REQUIRE(s->marker && post);s->marker->refs=1;
#ifdef TEST_LATE_COLOUR
    D3D12_VERTEX_BUFFER_VIEW v={va-0x1050,4080,24};REQUIRE(FAILED(record_colour_vb(s,0,1,&v)));
    UINT old_psos=psos,old_roots=roots;IUnknown *colour=late_root_id(FALSE);
    REQUIRE(FAILED(record_graphics_root(s,late_root_id(TRUE))) && !s->prior_pso);
    UINT old_creates=creates;struct xs_clear_source held=s->source;
    REQUIRE(s->guest_topology==17 && s->pipeline==diag);
    pool_vtbl.root(l,colour);REQUIRE(s->prior_pso==pso && s->prior_root==root && s->graphics_identity==colour && !s->pipeline && !s->bound && roots==old_roots+1 && seen_rec==s->host && seen_root==s->graphics_root && FAILED(record_clear4(s,pso)));
    REQUIRE(s->guest_topology==17 && !memcmp(&held,&s->source,sizeof held) && binds==1 &&
            FAILED(record_clear4_draw(s,3,1,0,0)) && FAILED(record_470(l,UINT64_MAX)));
    REQUIRE(FAILED(record_colour_vb(s,1,1,&v)) && FAILED(record_colour_vb(s,0,2,&v)));v.BufferLocation++;REQUIRE(FAILED(record_colour_vb(s,0,1,&v)));v.BufferLocation--;
    v.SizeInBytes=72;REQUIRE(FAILED(record_colour_vb(s,0,1,&v)));v.SizeInBytes=4080;pool_vtbl.vb(l,0,1,&v);REQUIRE(s->vb.placed==s->source.placed && s->vb.buffer && FAILED(record_colour_vb(s,0,1,&v)));
#ifdef TEST_INLINE_B27
    UINT64 *slot=(UINT64*)(va-0x1080);UINT64 handle=(SIZE_T)slot;
    memset(slot,0,32);MemoryBarrier();
    REQUIRE(FAILED(record_table0(s,0,handle)));pool_vtbl.bind(l,2,inline_pair);
    ID3D12DescriptorHeap *orig[2];memcpy(orig,bank_pair,sizeof orig);
    REQUIRE(FAILED(record_table0(s,0,handle)));
#endif
    REQUIRE(test_rt_roundtrip(s,ids[0],siblings[0],images[0],TRUE));
#ifdef TEST_INLINE_B27
    REQUIRE(FAILED(record_table0(s,1,handle)) && FAILED(record_table0(s,0,handle+1)) && FAILED(record_table0(s,0,UINT64_MAX)));
    pool_vtbl.b27(l,0,handle+32);REQUIRE(s->table0.slot==368505);
    pool_vtbl.b27(l,0,handle);REQUIRE(s->table0.slot==368504 && s->table0.heap==inline_pair[0] && s->table0.root==colour && !tables);
    REQUIRE(FAILED(record_table0(s,0,0)) && s->table0.slot==368504 && !s->bound);
    memset(slot,0xa5,32);MemoryBarrier(); // a volatile rewrite must not leave a materialized CBV
#ifdef TEST_SECOND_VB
    struct xs_clear_source first_vb=s->vb;
    D3D12_VERTEX_BUFFER_VIEW second={v.BufferLocation-0x1020,4080,24},bad=second;
    bad.BufferLocation+=16;REQUIRE(FAILED(record_colour_vb(s,0,1,&bad)));
    bad=second;bad.SizeInBytes=4079;REQUIRE(FAILED(record_colour_vb(s,0,1,&bad)));
    bad=second;bad.StrideInBytes=32;REQUIRE(FAILED(record_colour_vb(s,0,1,&bad)));
    struct xs_clear_source rejected={0};
    REQUIRE(FAILED(xs_copy_colour_vb(owner,hd,s->host,second.BufferLocation,TRUE,pso,&rejected)) && !rejected.buffer && !rejected.placed);
    REQUIRE(vb_calls==1 && !memcmp(&s->vb,&first_vb,sizeof first_vb) && !s->previous_vb.buffer);
    D3D12_TEXTURE_BARRIER single={.SyncBefore=0x40,.SyncAfter=0x1000,.AccessBefore=8,.AccessAfter=0x80,
        .LayoutBefore=2,.LayoutAfter=6,.pResource=(ID3D12Resource*)ids[0],.Subresources={.IndexOrFirstMipLevel=UINT_MAX}};
    D3D12_BARRIER_GROUP sg={.Type=D3D12_BARRIER_TYPE_TEXTURE,.NumBarriers=1,.pTextureBarriers=&single};
    UINT before_single=barrier_total;
    REQUIRE(FAILED(record_primary_barrier(s,1,&sg))); // too early, even after the round trip
    pool_vtbl.vb(l,0,1,&second);
    REQUIRE(vb_calls==2 && !memcmp(&s->previous_vb,&first_vb,sizeof first_vb) && s->vb.placed==first_vb.placed && s->vb.buffer!=first_vb.buffer);
    REQUIRE(FAILED(record_table0(s,0,handle)) && s->table0.cells==1);
    REQUIRE(FAILED(record_colour_vb(s,0,1,&second)) && vb_calls==2);
    extern IUnknown *test_texture(IUnknown*,ID3D12Resource*,BOOL);
    IUnknown *alias=test_texture(owner,images[0],FALSE);REQUIRE(alias && alias!=ids[0]);
    single.pResource=(ID3D12Resource*)alias;REQUIRE(FAILED(record_primary_barrier(s,1,&sg)));IUnknown_Release(alias);
    single.pResource=(ID3D12Resource*)siblings[0];REQUIRE(FAILED(record_primary_barrier(s,1,&sg)));
    single.pResource=(ID3D12Resource*)ids[0];
    single.Subresources.NumPlanes=1;REQUIRE(FAILED(record_primary_barrier(s,1,&sg)));single.Subresources.NumPlanes=0;
    single.Flags=1;REQUIRE(FAILED(record_primary_barrier(s,1,&sg)));single.Flags=0;
    single.LayoutAfter=2;REQUIRE(FAILED(record_primary_barrier(s,1,&sg)));single.LayoutAfter=6;
    single.AccessAfter=0;REQUIRE(FAILED(record_primary_barrier(s,1,&sg)));single.AccessAfter=0x80;
    single.SyncAfter=0;REQUIRE(FAILED(record_primary_barrier(s,1,&sg)));single.SyncAfter=0x1000;
    REQUIRE(FAILED(record_primary_barrier(s,2,&sg)) && barrier_total==before_single && !s->offscreen_sr);
#ifdef TEST_DISTINCT_PRIMARY
    REQUIRE(FAILED(record_om(s,1,&ps,FALSE,NULL)) && !s->output_heap);
    /* Isolate each exclusion even though an ordinary immutable wrapper cannot change its host. */
    s->primary_key=(UINT_PTR)ids[0];REQUIRE(FAILED(record_primary_barrier(s,1,&sg)));s->primary_key=(UINT_PTR)pk;
    s->primary=images[0];REQUIRE(FAILED(record_primary_barrier(s,1,&sg)));s->primary=primary;
    single.pResource=(ID3D12Resource*)pk;REQUIRE(FAILED(record_primary_barrier(s,1,&sg)));single.pResource=(ID3D12Resource*)ids[0];
    REQUIRE(!s->offscreen_sr && barrier_total==before_single);
    enum batch_phase phase=s->phase;
#endif
    pool_vtbl.barrier(l,1,&sg);
#ifdef TEST_DISTINCT_PRIMARY
    REQUIRE(s->phase==phase && s->primary==primary && s->primary_key==(UINT_PTR)pk && s->primary_cleared && !s->primary_released);
    UINT pre_om=om_calls;
    REQUIRE(FAILED(record_table0(s,0,handle)) && s->table0.cells==1);
    SIZE_T other_slot=ps+32;
    IUnknown *same_host=test_texture(owner,primary,FALSE);REQUIRE(same_host && same_host!=pk);
    REQUIRE(xs_write_view(owner,0xa0,same_host,rt,other_slot)==S_OK && FAILED(record_om(s,1,&other_slot,FALSE,NULL)));
    IUnknown_Release(same_host);
    REQUIRE(FAILED(record_om(s,0,&ps,FALSE,NULL)) && FAILED(record_om(s,1,&ps,TRUE,NULL)) &&
            FAILED(record_om(s,1,&ps,FALSE,slots+1)) && FAILED(record_om(s,1,slots,FALSE,NULL)) &&
            record_om(s,1,NULL,FALSE,NULL)==E_POINTER);
    UINT64 *rt_tag=(UINT64*)ps;UINT64 saved=*rt_tag;*rt_tag^=1;
    REQUIRE(FAILED(record_om(s,1,&ps,FALSE,NULL)));*rt_tag=saved;
    REQUIRE(xs_write_view(owner,0xa0,pk,rt,ps)==S_OK && FAILED(record_om(s,1,&ps,FALSE,NULL)));
    REQUIRE(record_primary_clear(s,ps,(FLOAT[4]){0},0,NULL)==E_ACCESSDENIED);
    /* Prepare a different valid RTV generation; the overwritten clear slot stays stale. */
    REQUIRE(xs_write_view(owner,0xa0,pk,rt,other_slot)==S_OK && !s->output_heap && om_calls==pre_om);
    pool_vtbl.om(l,1,&other_slot,FALSE,NULL);
    UINT64 output_gen=*(UINT64*)other_slot;SIZE_T private_rtv=s->output_cpu.ptr;
    REQUIRE(s->output_heap && output_gen==s->output_serial && om_calls==pre_om+1 && last_rt==private_rtv && !last_ds);
    UINT64 base=handle-32ull*s->table0.slot;
    UINT64 end=base+32ull*D3D12_MAX_SHADER_VISIBLE_DESCRIPTOR_HEAP_SIZE_TIER_1;
    UINT64 epoch=s->table_epoch,composite=handle-131*32;
    REQUIRE(FAILED(record_table0(s,0,end-32)) && s->table0.slot==368504 &&
            s->table0.cells==1 && s->table_epoch==epoch);
    REQUIRE(record_table0(s,0,end-96)==S_OK);
    REQUIRE(FAILED(record_table0(s,0,end-64)) && FAILED(record_table0(s,0,end-32)) &&
            FAILED(record_table0(s,0,end)) && FAILED(record_table0(s,1,composite)) &&
            FAILED(record_table0(s,0,composite+1)) && FAILED(record_table0(s,0,UINT64_MAX)) &&
            s->table_epoch==epoch+1 && s->table0.slot==D3D12_MAX_SHADER_VISIBLE_DESCRIPTOR_HEAP_SIZE_TIER_1-3);
    pool_vtbl.b27(l,0,composite);
    memset((void*)(SIZE_T)composite,0x5a,96);MemoryBarrier();
    REQUIRE(s->table_epoch==epoch+2 && s->table0.slot==368373 && !tables);
    pool_vtbl.b27(l,0,composite);
    REQUIRE(s->table_epoch==epoch+3 && s->table0.cells==3 && s->table0.slot==368373 &&
            s->table0.root==colour && s->table0.heap==inline_pair[0] && !s->bound && !s->pipeline && !tables);
#ifndef TEST_DEFER_RELEASE
    REQUIRE(xs_write_view(owner,0xa0,siblings[0],rt,other_slot)==S_OK && *(UINT64*)other_slot!=output_gen);
#endif
    REQUIRE(IUnknown_Release(pk)==1 && FAILED(record_om(s,1,&other_slot,FALSE,NULL)) && om_calls==pre_om+1);
#endif
    REQUIRE(barrier_total==before_single+1 && barrier_host==s->host && s->offscreen_sr && seen[0].pResource==images[0]);
    D3D12_TEXTURE_BARRIER native=single;native.pResource=images[0];
    REQUIRE(!memcmp(&seen[0],&native,offsetof(D3D12_TEXTURE_BARRIER,Flags)+sizeof native.Flags));
    BYTE *sources[]={(BYTE*)v.BufferLocation,(BYTE*)second.BufferLocation};
    BYTE expected[2][4080];
    for(UINT j=0;j<2;j++){
        for(UINT k=0;k<4080;k++)expected[j][k]=(BYTE)(k*7+(k>>5)+j*113);
        if(j==1){
            /* +1263ad..+1263e6 producer shape; synthetic dimensions and firstVertex. */
            const struct {float x,y,z;UINT color;float u,v;} verts[]={
                {0,0,0,~0u,0,0},{0,2160,0,~0u,0,1},{3840,2160,0,~0u,1,1}};
            _Static_assert(sizeof verts==72,"three packed composite vertices");
            memcpy(expected[j],verts,sizeof verts);
        }
        memcpy(sources[j],expected[j],4080);
    }
    MemoryBarrier();
    ID3D12Resource *vb_read; rbd.Width=2*4080+72;
    REQUIRE(SUCCEEDED(ID3D12Device_CreateCommittedResource(hd,&rbh,0,&rbd,D3D12_RESOURCE_STATE_COPY_DEST,NULL,&IID_ID3D12Resource,(void**)&vb_read)));
#endif

#endif
#endif
    UINT before_split_om=om_calls;
#ifdef TEST_DISTINCT_PRIMARY
    REQUIRE(split_native_list(s,post)==S_OK && om_calls==before_split_om+1 && last_rt==private_rtv && !last_ds);
#else
    REQUIRE(split_native_list(s,post)==S_OK && om_calls==before_split_om+!s->offscreen_sr && last_rt==frozen.cpu[0].ptr && last_ds==frozen.cpu[1].ptr);
#endif
    REQUIRE(s->guest_topology==17 && topo_calls==nia+2 && ia_list==s->host && ia_list!=s->head && ia_value==D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
#ifdef TEST_SECOND_VB
    REQUIRE(s->offscreen_sr && barrier_total==before_single+1 && FAILED(record_primary_barrier(s,1,&sg)) &&
            barrier_total==before_single+1 && FAILED(record_om(s,1,slots,FALSE,slots+1)));
#ifdef TEST_DISTINCT_PRIMARY
    REQUIRE(s->primary==primary && s->primary_key==(UINT_PTR)pk && s->clear_serial==ps_serial &&
            s->clear_slot==ps && s->primary_cleared && !s->primary_released);
    const FLOAT cyan[]={0,1,1,1};ID3D12GraphicsCommandList7_ClearRenderTargetView(s->host,s->output_cpu,cyan,0,NULL);
    D3D12_TEXTURE_BARRIER rel=acquire;rel.SyncBefore=0x40;rel.SyncAfter=0;rel.AccessBefore=8;
    rel.AccessAfter=D3D12_BARRIER_ACCESS_NO_ACCESS;rel.LayoutBefore=2;rel.LayoutAfter=(D3D12_BARRIER_LAYOUT)31;
    ag.pTextureBarriers=&rel;REQUIRE(FAILED(record_primary_barrier(s,1,&ag)) && !s->primary_released);
    sample_quad(s->host,primary,quad_rb,1);
#endif
    /* Test readback only: return the image to RT for the independent OM/DSV fixture that follows. */
    D3D12_TEXTURE_BARRIER back=seen[0];
    back.SyncBefore=D3D12_BARRIER_SYNC_ALL_SHADING;back.SyncAfter=D3D12_BARRIER_SYNC_COPY;
    back.AccessBefore=D3D12_BARRIER_ACCESS_SHADER_RESOURCE;back.AccessAfter=D3D12_BARRIER_ACCESS_COPY_SOURCE;
    back.LayoutBefore=D3D12_BARRIER_LAYOUT_SHADER_RESOURCE;back.LayoutAfter=D3D12_BARRIER_LAYOUT_COPY_SOURCE;
    D3D12_BARRIER_GROUP bg={.Type=D3D12_BARRIER_TYPE_TEXTURE,.NumBarriers=1,.pTextureBarriers=&back};
    s->host->lpVtbl->Barrier(s->host,1,&bg);
    copy_quad(s->host,images[0],quad_rb,3);
    back.SyncBefore=back.SyncAfter;back.SyncAfter=D3D12_BARRIER_SYNC_RENDER_TARGET;
    back.AccessBefore=back.AccessAfter;back.AccessAfter=D3D12_BARRIER_ACCESS_RENDER_TARGET;
    back.LayoutBefore=back.LayoutAfter;back.LayoutAfter=D3D12_BARRIER_LAYOUT_RENDER_TARGET;
    s->host->lpVtbl->Barrier(s->host,1,&bg);
#else
    ID3D12GraphicsCommandList7_ClearRenderTargetView(s->host,frozen.cpu[0],guard,0,NULL);
    sample_quad(s->host,images[0],quad_rb,3);
#endif
    REQUIRE(!xs_list_host(l));
    REQUIRE(FAILED(record_clear4_draw(s,3,1,0,0)));
    REQUIRE(bounds_calls==nb+2 && bounds_recorder==s->host && bounds_recorder!=s->head && last_bounds[0]==0 && last_bounds[1]==1);
    REQUIRE(rects==nr+2 && vps==nv+2 && rect_host==s->host && vp_host==s->host);
#ifdef TEST_LATE_COLOUR
    REQUIRE(binds==1 && src_host==s->head && roots==old_roots+2 && seen_rec==s->host && seen_root==s->graphics_root && !s->pipeline && psos==old_psos);
#ifdef TEST_SECOND_VB
    const UINT expected_vb_calls=3;
#else
    const UINT expected_vb_calls=2;
#endif
    REQUIRE(vb_calls==expected_vb_calls && vb_rec==s->host && last_vb.BufferLocation==ID3D12Resource_GetGPUVirtualAddress(s->vb.buffer) && last_vb.BufferLocation!=v.BufferLocation && last_vb.SizeInBytes==4080 && last_vb.StrideInBytes==24);
#ifdef TEST_INLINE_B27
#ifdef TEST_DISTINCT_PRIMARY
    REQUIRE(s->table0.cells==3 && s->table0.slot==368373 && s->prior_b27_slot==368504 && s->table_epoch==epoch+3 &&
            *(BYTE*)(SIZE_T)composite==0x5a && s->output_serial==output_gen);
#else
    REQUIRE(s->table0.cells==1 && s->table0.slot==368504);
#endif
    REQUIRE(!tables && s->table0.heap==inline_pair[0] && s->table0.root==colour &&
        bank_pair[0]==orig[0] && bank_pair[1]==orig[1] && slot[0]==0xa5a5a5a5a5a5a5a5ull);
#endif
    puts("PASS +180 exact VB: placed owner, guarded view/extent, private host address replayed on tail");
    REQUIRE(creates==old_creates+1 && !last_initial &&
            s->clear4_lanes==0xffff00000000ffffULL && s->prior_pso==pso && s->prior_root==root &&
            !memcmp(&held,&s->source,sizeof held));
    IUnknown *ready=publish_late();REQUIRE(ready && !s->pipeline && !s->bound && !xs_list_host(l));
#ifdef TEST_SECOND_VB
    REQUIRE(FAILED(record_clear4(s,ready)) && !s->bound && !s->pipeline && psos==old_psos);
#ifdef TEST_DISTINCT_PRIMARY
    REQUIRE(s->table_epoch==epoch+3 && s->table0.cells==3 && !tables && FAILED(record_clear4_draw(s,3,1,0,0)));
#else
    REQUIRE(FAILED(record_table0(s,0,handle)) && !tables && FAILED(record_clear4_draw(s,3,1,0,0)));
#endif
    ID3D12Resource *buffers[]={s->previous_vb.buffer,s->vb.buffer};
    for(UINT j=0;j<2;j++){
        D3D12_RESOURCE_BARRIER b={.Transition={buffers[j],0,D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER,D3D12_RESOURCE_STATE_COPY_SOURCE}};
        ID3D12GraphicsCommandList7_ResourceBarrier(s->host,1,&b);
        ID3D12GraphicsCommandList7_CopyBufferRegion(s->host,vb_read,4080*j,buffers[j],0,4080);
    }
    /* Last legal start for three 24-byte records; do not bake firstVertex into the bound VA. */
    ID3D12GraphicsCommandList7_CopyBufferRegion(s->host,vb_read,8160,buffers[1],167*24,72);
#else
    pool_vtbl.pipeline(l,ready);REQUIRE(s->bound==ready && psos==old_psos+1 && seen_rec==s->host && s->required_root==colour && FAILED(record_graphics_root(s,root)));
#ifdef TEST_INLINE_B27
    REQUIRE(!tables && s->table0.heap==inline_pair[0] && s->table0.root==colour && s->table0.slot==368504 && FAILED(record_table0(s,0,handle)));
    puts("PASS logical b27: empty descriptor accepted with NULL PSO, rebind/rewrite, original heaps on split, explicit PSO leaves table unresolved");
#endif
    REQUIRE(s->pipeline!=diag && s->pipeline!=full && FAILED(record_470(l,UINT64_MAX)) && FAILED(record_clear4_draw(s,3,1,0,0)) &&
            s->prior_pso==pso && s->prior_root==root && topo_calls==nia+2 && binds==1);
#endif
#else
    REQUIRE(binds==2 && src_host==s->host && last_va==private_va && s->pending_470);
    pool_vtbl.topology(l,D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    pool_vtbl.draw(l,3,1,0,0);sample_quad(s->host,images[0],quad_rb,1);
    pool_vtbl.state470(l,UINT64_MAX);REQUIRE(s->pipeline==full && s->clear4_lanes==UINT64_MAX);
    pool_vtbl.root(l,root);REQUIRE(binds==3 && src_host==s->host && last_va==private_va);
    pool_vtbl.draw(l,3,1,0,0);sample_quad(s->host,images[0],quad_rb,2);
    pool_vtbl.topology(l,17);
#endif
    const FLOAT green[4]={0,1,0,1};ID3D12GraphicsCommandList7_ClearRenderTargetView(isolated,frozen.cpu[0],green,0,NULL);
#ifdef TEST_DEFER_RELEASE
    IUnknown *syncq;REQUIRE(release_fixture_clock(hd,owner,&syncq));
#endif
    REQUIRE(sl_close(l)==S_OK && xs_direct_queue(hd,&q)==S_OK && checked_startup_submit(q,1,&l,0,0)==E_ACCESSDENIED);
#ifdef TEST_DEFER_RELEASE
    REQUIRE(test_successor(hd,owner,s,syncq,ids[0],inline_pair));
    IUnknown_Release(syncq);
    REQUIRE(xs_write_view(owner,0xa0,siblings[0],rt,other_slot)==S_OK && *(UINT64*)other_slot!=output_gen);
#endif
    REQUIRE(execute_quad_fixture(hd,s,quad_rb));
#ifdef TEST_DISTINCT_PRIMARY
    REQUIRE(SUCCEEDED(ID3D12Resource_Map(quad_rb,0,NULL,(void**)&qp)));
    for(UINT y=0;y<2;y++)for(UINT x=0;x<2;x++)REQUIRE(*(DWORD*)(qp+512+y*256+x*4)==0xffffff00);
    ID3D12Resource_Unmap(quad_rb,0,NULL);
    REQUIRE(s->primary_pin==pk && s->output_heap);
#endif
    ID3D12Resource_Release(quad_rb);
#ifdef TEST_SECOND_VB
    BYTE *read;REQUIRE(SUCCEEDED(ID3D12Resource_Map(vb_read,0,NULL,(void**)&read)) &&
        !memcmp(read,expected,sizeof expected) && !memcmp(read+8160,expected[1]+167*24,72));
    ID3D12Resource_Unmap(vb_read,0,NULL);ID3D12Resource_Release(vb_read);
    puts("PASS changing VB: both 4080-byte buffers through fence, replacement on tail, exact firstVertex=167 window, late colour PSO refused");
    puts("PASS composite +490: original RT to shader-resource once, exact native operand, split preserved layout and offscreen pixels");
#endif
    REQUIRE(FAILED(sl_reset(l,a,NULL,1)) && s->guest_topology==17 && !can_set_topology(s,17));
    REQUIRE(FAILED(record_bounds(s,0,1)) && FAILED(sl_reset(l,a,NULL,1)) && s->full_bounds && s->om.heap[1] && s->attachments[0].identity && sl_reset(l,a,NULL,0)==S_OK && !s->om.heap[0] && !s->om.heap[1] && !s->attachments[0].identity && !s->attachments[1].identity && !s->full_bounds && !s->full_rect && !s->full_vp);
#ifdef TEST_LATE_COLOUR
    REQUIRE(!s->previous_vb.placed && !s->previous_vb.buffer && !s->table0.heap && !s->vb.placed && !s->vb.buffer);
    REQUIRE(!s->prior_pso && !s->prior_root && !s->graphics_identity && !s->pipeline && !s->bound);puts("PASS controlled late worker: root on current and split recorder, source not rebound under CBV table, publication alone ineffective, explicit PSO on tail, denied submit and reset");
#endif
    REQUIRE(!s->source.buffer && !s->source.placed);puts("PASS +160: real wrapper, order and ambiguity guards, identical head/tail root VA, reset and pending-submit refusal");
    REQUIRE(!s->offscreen_sr && !s->rt_roundtripped && !s->guest_topology && !s->clear4_drew && !can_set_topology(s,17));
#ifdef TEST_DISTINCT_PRIMARY
    REQUIRE(!s->table0.cells && !s->table0.root && !s->prior_b27_slot && s->table_epoch==epoch+3 && FAILED(record_table0(s,0,composite)));
    puts("PASS logical composite +120: whole bank span, later slot rewrite and rebind, null PSO on split, stale +c8/draw/submit refused, reset");
    REQUIRE(!s->primary_pin && !s->output_heap && !xs_retain_primary(owner,pk));
    puts("PASS ordinary composite +190: unique live primary RTV, no DSV, generation/alias rejects, original cyan output after slot rewrite and split, reset retired key");
#endif
    puts("PASS +a0(17): host triangle input on head/tail, rectangle rejects #4, fenced unchanged guard and reset");
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
    replace(arg);REQUIRE(FAILED(xs_copy_clear_source(owner,hd,s->host,va,&s->source)));
    REQUIRE(IUnknown_Release(placed)==1);
    IUnknown_Release(l);HANDLE e=CreateEventW(NULL,FALSE,FALSE,NULL);REQUIRE(e && xs_fence_event(done,1,e)==S_OK && xs_cpu_signal(gate,1)==S_OK && WaitForSingleObject(e,10000)==WAIT_OBJECT_0);
    REQUIRE(SUCCEEDED(ID3D12Resource_Map(rb,0,NULL,(void**)&bytes)));for(UINT k=0;k<128;k++)REQUIRE(bytes[k]==(k>=12 && k<112?(BYTE)(7+3*(k-12)):0xe7));ID3D12Resource_Unmap(rb,0,NULL);
    CloseHandle(e);ID3D12Resource_Release(rb);IUnknown_Release(q);IUnknown_Release(gate);IUnknown_Release(done);IUnknown_Release(a);IUnknown_Release(b);
    puts("PASS +160 fenced copy-only control: all 100 overwritten original bytes, neither snapshot nor replacement VA, both guards, reset/release while queued");return TRUE;
}
