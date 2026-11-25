#define COBJMACROS
#define CONST_VTABLE
#include "queue.h"
#include "used-proof.h"
#include <stdio.h>
static UINT draws,first_seen,bind_tables;
static UINT roots,psos;
static ID3D12RootSignature *last_root;
static void fixture_set_root(ID3D12GraphicsCommandList7 *l,ID3D12RootSignature *r){roots++;last_root=r;ID3D12GraphicsCommandList7_SetGraphicsRootSignature(l,r);}
static void set_pso(ID3D12GraphicsCommandList7 *l,ID3D12PipelineState *p){psos++;ID3D12GraphicsCommandList7_SetPipelineState(l,p);}
#undef ID3D12GraphicsCommandList7_SetGraphicsRootSignature
#undef ID3D12GraphicsCommandList7_SetPipelineState
#define ID3D12GraphicsCommandList7_SetGraphicsRootSignature fixture_set_root
#define ID3D12GraphicsCommandList7_SetPipelineState set_pso
static void draw(ID3D12GraphicsCommandList7*l,UINT n,UINT i,UINT f,UINT b){
    ++draws;first_seen=f;ID3D12GraphicsCommandList7_DrawInstanced(l,n,i,f,b);
}
#undef ID3D12GraphicsCommandList7_DrawInstanced
#define ID3D12GraphicsCommandList7_DrawInstanced draw
#include "queue.c"
static struct startup_list *s;
static IUnknown *l,*a;
#define OK(x) do{if(!(x)){fprintf(stderr,"FAIL list %d: %s\n",__LINE__,#x);exit(1);}}while(0)
#define REQUIRE(x) OK(x)
HRESULT xs_write_view(IUnknown*,UINT,IUnknown*,const void*,SIZE_T);
#include "test-target-oracle.h"
static struct xs_attachment_pin output;
ID3D12GraphicsCommandList7 *begin(IUnknown *owner,ID3D12Device *hd){
    OK(xs_allocator(hd,0,&a)==S_OK && xs_device_list(hd,0,a,TRUE,owner,&l)==S_OK);s=sl(l);return s->host;
}
ID3D12GraphicsCommandList7 *returned(struct used_proof *u,IUnknown *depth,ID3D12Resource *ds,SIZE_T rt,SIZE_T dv,SIZE_T dst,
        ID3D12CommandQueue *q,ID3D12Fence *gate,ID3D12Fence *end,IUnknown *clear,IUnknown *root,IUnknown *alias){
    struct xs_attachment_pin pins[]={{u->texture_key,u->image,rt,*(UINT64*)rt},{depth,ds,dv,*(UINT64*)dv}};
    /* A real head writes the sampled texture. Its end stays pending while the tail records. */
    D3D12_RESOURCE_BARRIER round={.Transition={u->image,0,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE}};
    ID3D12GraphicsCommandList7_ResourceBarrier(s->host,1,&round);
    round.Transition.StateBefore=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;round.Transition.StateAfter=D3D12_RESOURCE_STATE_RENDER_TARGET;
    ID3D12GraphicsCommandList7_ResourceBarrier(s->host,1,&round);
    s->marker=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof *s->marker);
    struct prefix_marker *post=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof *post);
    OK(s->marker && post);s->marker->refs=1;s->nops=6;
    OK(split_native_list(s,post)==S_OK && s->split_at==6 && s->head!=s->host);
    OK(ID3D12CommandQueue_Wait(q,gate,1)==S_OK);
    ID3D12CommandList *h=(ID3D12CommandList*)s->head;ID3D12CommandQueue_ExecuteCommandLists(q,1,&h);
    OK(ID3D12CommandQueue_Signal(q,end,1)==S_OK && xs_sr_submitted(u->owner,pins,pins+1,q,end));
    OK(prepare_returned_target(s,q,dst,&output) && test_target_frame(u->owner,q,end));
    OK(xs_sr_snapshot(u->owner,u->texture_key,&s->sr)==S_OK && s->sr.end==end && s->sr.rt.host==u->image);
    OK(xs_freeze_om(u->owner,pins,(SIZE_T[]){rt,dv},&s->om)==S_OK);
    pool_vtbl.pipeline(l,clear);pool_vtbl.root(l,root);
    s->prior_pso=s->bound;s->bound=NULL;s->pipeline=NULL;s->prior_root=s->graphics_identity;
    s->graphics_root=xs_retain_colour_root(u->owner,alias);OK(s->graphics_root);
    s->graphics_identity=s->required_root=alias;pool_vtbl.root(l,alias);
    s->rt_roundtripped=s->clear4_drew=s->pending_470=s->depth_zero_pending=TRUE;
    s->guest_topology=XS_RECTLIST;ID3D12GraphicsCommandList7_IASetPrimitiveTopology(s->host,D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    OK(xs_copy_clear_source(u->owner,s->device,s->host,u->base+0x3ff80,&s->source)==S_OK);
    OK(xs_copy_colour_vb(u->owner,s->device,s->host,u->base+0x3ef30,FALSE,s->source.placed,&s->previous_vb)==S_OK &&
       xs_copy_colour_vb(u->owner,s->device,s->host,u->base+0x3df10,TRUE,s->source.placed,&s->vb)==S_OK);
    bind_colour_vb(s->host,&s->vb);
    D3D12_TEXTURE_BARRIER edge={.SyncBefore=D3D12_BARRIER_SYNC_RENDER_TARGET,.SyncAfter=D3D12_BARRIER_SYNC_ALL_SHADING,
        .AccessBefore=D3D12_BARRIER_ACCESS_RENDER_TARGET,.AccessAfter=D3D12_BARRIER_ACCESS_SHADER_RESOURCE,
        .LayoutBefore=D3D12_BARRIER_LAYOUT_RENDER_TARGET,.LayoutAfter=D3D12_BARRIER_LAYOUT_SHADER_RESOURCE,
        .Subresources.IndexOrFirstMipLevel=UINT_MAX,.pResource=(ID3D12Resource*)u->texture_key};
    D3D12_BARRIER_GROUP group={.Type=D3D12_BARRIER_TYPE_TEXTURE,.NumBarriers=1,.pTextureBarriers=&edge};
    OK(record_primary_barrier_at(s,1,&group,0x32113)==S_OK && s->sr_reversed);
    OK(xs_freeze_returned_output(u->owner,output.identity,output.host,dst,output.serial,s->reuse_epoch,&s->sr,TRUE,
        &s->output_heap,&s->output_cpu)==S_OK);
    s->output_slot=dst;s->output_serial=output.serial;
    ID3D12GraphicsCommandList7_OMSetRenderTargets(s->host,1,&s->output_cpu,FALSE,NULL);
    D3D12_VIEWPORT vp={0,0,16,16,0,1};D3D12_RECT sc={0,0,16,16};
    ID3D12GraphicsCommandList7_RSSetScissorRects(s->host,1,&sc);ID3D12GraphicsCommandList7_RSSetViewports(s->host,1,&vp);
    s->full_rect=s->full_vp=TRUE;return s->host;
}
ID3D12Resource *oracle(void){return output.host;}
void record96(struct used_proof *u,struct used_binding *binding,IUnknown *w,IUnknown *wrong){
    /* Fixture-owned dirty producer, no claim about the natural returned table setter. */
    UINT slot;
    OK(xs_active_table_slot(u->owner,u->visible,u->table,3,&slot)==S_OK);
    IUnknown_AddRef(u->visible);s->heaps[0]=u->visible;s->table0=(__typeof__(s->table0)){u->visible,s->graphics_identity,slot,3};
    s->table_epoch=u->epoch=1;*binding=(struct used_binding){u->visible,u->table,1};u->bound=binding;OK(test_used_current(u));
    UINT rr=roots,pp=psos;OK(FAILED(record_clear4(s,wrong)) && roots==rr && psos==pp);
    memcpy((BYTE*)l+0x544,(BYTE*)w+0x10,32);pool_vtbl.pipeline(l,w);
    OK(s->bound==w && roots==rr+1 && psos==pp+1 && FAILED(record_graphics_root(s,s->prior_root)));
    pool_vtbl.root(l,s->required_root);OK(last_root==s->graphics_root && roots==rr+2 && psos==pp+1);
    s->ready_segment=s->host;s->ready_next=0x60;
    s->sealed=(struct consume_ticket){.list=s,.segment=s->host,.proof=u,.alias=s->graphics_identity,.output=output.identity,
        .slot=output.slot,.end=s->sr.end,.reuse=s->reuse_epoch,.allocator=s->allocator,.alloc_generation=s->alloc_generation,
        .wrapper=w,.private_root=s->graphics_root,.pso=s->pipeline,.vb=s->vb.buffer,.epoch=u->epoch,.output_serial=output.serial,
        .producer=u->cb_allocation,.source_heap=u->src_heap,.table=u->table,.arena_base=u->base,.map=u->map_at_alloc,
        .generation=u->cb_generation,.raw_serial=u->raw_serial,.texture_serial=u->tex_serial};
    OK(current96(s) && FAILED(draw96(s,3,1,167,0)) && FAILED(draw96(s,3,2,0,0)));
    ++s->output_serial;OK(FAILED(draw96(s,3,1,0,0)));--s->output_serial;
    ++s->table_epoch;OK(FAILED(draw96(s,3,1,0,0)));--s->table_epoch;
    ++u->map_at_publish;OK(FAILED(draw96(s,3,1,0,0)));--u->map_at_publish;
    *(UINT64*)(u->table+64)^=16;OK(FAILED(draw96(s,3,1,0,0)));*(UINT64*)(u->table+64)^=16;
    IUnknown *tex=u->texture_key;u->texture_key=output.identity;OK(FAILED(draw96(s,3,1,0,0)));u->texture_key=tex;
    OK(!bind_tables && !draws && current96(s));
    pool_vtbl.draw(l,3,1,0,0);OK(s->sealed.emitted && first_seen==0 && draws==1 && bind_tables==1 && FAILED(draw96(s,3,1,0,0)));
    puts("PASS returned96 recording: split-six tail, explicit +c8 +110 +60, first=0, one real table bind/draw");
}
void table_emitted(void){++bind_tables;}
void finish_record(void){OK(sl_close(l)==S_OK);}
BOOL submit96(ID3D12CommandQueue *q){
    if(!s->sealed.emitted || !s->closed || !current96(s))return FALSE;
    ID3D12CommandList *h=(ID3D12CommandList*)s->host;ID3D12CommandQueue_ExecuteCommandLists(q,1,&h);s->sealed.emitted=FALSE;return TRUE;
}
void retire(void){
    OK(test_target_frame(s->pool_owner,NULL,NULL));ID3D12Resource_Release(output.host);IUnknown_Release(output.identity);
    IUnknown_Release(l);IUnknown_Release(a);
}
