/* Synthetic post-clear recording. The host device and list are real. */
#include "queue.c"
#define CHECK(x) do { if(!(x)){fprintf(stderr,"FAIL F96 line %d: %s\n",__LINE__,#x);return FALSE;} } while(0)
BOOL exercise(IUnknown *owner,ID3D12Device *hd,IUnknown *clear,IUnknown *r,IUnknown *alias,
        HRESULT (*publish)(IUnknown **))
{
    IUnknown *a=NULL,*l=NULL,*p=NULL;
    CHECK(xs_allocator(hd,0,&a)==S_OK && xs_device_list(hd,0,a,TRUE,owner,&l)==S_OK);
    struct startup_list *s=sl(l);
    pool_vtbl.pipeline(l,clear);pool_vtbl.root(l,r);
    CHECK(s->bound==clear && s->graphics_identity==r);
    s->prior_pso=s->bound;s->bound=NULL;s->pipeline=NULL;s->prior_root=s->graphics_identity;
    ID3D12RootSignature *b27=xs_retain_colour_root(owner,alias);
    CHECK(b27);s->graphics_root=b27;s->required_root=s->graphics_identity=alias;s->guest_topology=XS_RECTLIST;
    pool_vtbl.root(l,alias);
    CHECK(publish(&p)==S_OK && p && !s->bound && !s->pipeline && s->graphics_root==b27);
    pool_vtbl.root(l,alias);
    CHECK(!s->bound && !s->pipeline && s->graphics_root==b27);
    ID3D12RootSignature *pr=NULL;
    ID3D12PipelineState *host=xs_pin_im96_pso(owner,hd,p,alias,&pr);
    CHECK(host && pr && pr!=b27 && IUnknown_Release(p)==1);
    CHECK(FAILED(record_clear4(s,clear)) && FAILED(record_graphics_root(s,r)));
    s->guest_topology=D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    CHECK(FAILED(record_clear4(s,p)) && !s->bound && !s->pipeline && s->graphics_root==b27);
    s->guest_topology=XS_RECTLIST;pool_vtbl.pipeline(l,p);
    CHECK(s->bound==p && s->pipeline==host && s->graphics_root==pr && s->graphics_identity==alias);
    CHECK(IUnknown_Release(p)==1);
    pool_vtbl.root(l,alias);
    CHECK(s->bound==p && s->pipeline==host && s->graphics_root==pr && s->required_root==alias);
    CHECK(record_clear4_draw(s,3,1,0,0)==E_NOTIMPL);
    IUnknown *queue=NULL;
    CHECK(sl_close(l)==S_OK && xs_direct_queue(hd,&queue)==S_OK);
    CHECK(checked_startup_submit(queue,1,&l,0,0)==E_ACCESSDENIED);
    CHECK(checked_startup_submit(queue,1,&l,2,3)==E_ACCESSDENIED);
    CHECK(!s->seal && !IUnknown_Release(l));
    IUnknown_Release(queue);
    CHECK(!xs_pin_im96_pso(owner,hd,p,alias,&pr) && !pr);
    IUnknown_Release(a);
    puts("PASS F96 factory, explicit +c8 and wrapper retirement");
    return TRUE;
}
