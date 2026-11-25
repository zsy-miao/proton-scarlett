/* The carried #4 draw already proved the source, frozen views and effective pipeline.
 * Only the immediately following RECTLIST -> explicit #42 -> root order is covered. */
static HRESULT carried_rect42(struct startup_list *s,IUnknown *identity,UINT_PTR site)
{
    static const BYTE ops[]={0x20,0x47,0x20,0x48,0x47,0x4a,0x92,0x34,0x14,0x7e,0x19,0x22,0x4b,
        0x2f,0x18,0x8e,0x17,0x92,0x32,0x46,0x16,0x15,0x2c,0x0c,0x14,0x19,0x22};
    BOOL set_root=site==0x327cb;
    HRESULT hr=E_NOTIMPL;
    AcquireSRWLockExclusive(&s->borrow_lock);
    if((!set_root && site!=0x327bc) || !identity || !open_direct_state(s) ||
       s->nops!=sizeof ops-!set_root || memcmp(s->ops,ops,s->nops) ||
       !s->cache_page || memcmp(s->cache_page,cache_template,sizeof cache_template) ||
       !s->clear4_drew || s->clear4_lanes!=UINT64_MAX || !s->pending_470 || !s->depth_zero_pending ||
       s->guest_topology!=XS_RECTLIST || !s->source.needs_publication || !s->source.capture ||
       !s->source.placed || !s->source.buffer || s->vb.buffer || s->previous_vb.buffer || s->table_epoch ||
       s->table0.heap || !s->sr.end || s->reuse_epoch!=1 || s->sr_reversed || s->rt_roundtripped ||
       !s->sr.rt.identity || !s->sr.ds.identity || !s->om.heap[0] || !s->om.heap[1] ||
       !s->bound || !s->pipeline || !s->graphics_identity || !s->graphics_root || s->prior_root)goto done;
    if(!set_root){
        if(s->prior_pso || s->bound==identity || s->graphics_identity!=s->required_root)goto done;
        IUnknown *root=NULL;
        ID3D12PipelineState *rect=xs_pin_rect42(s->pool_owner,s->device,identity,&root);
        if(!rect)goto done;
        s->prior_pso=s->bound;s->bound=identity;s->pipeline=rect;s->required_root=root;
        ID3D12GraphicsCommandList7_SetPipelineState(s->host,rect);
    }else{
        if(!s->prior_pso || identity!=s->required_root || identity==s->graphics_identity)goto done;
        ID3D12RootSignature *root=xs_retain_colour_root(s->pool_owner,identity);
        if(!root)goto done;
        s->prior_root=s->graphics_identity;s->graphics_identity=identity;s->graphics_root=root;
        ID3D12GraphicsCommandList7_SetGraphicsRootSignature(s->host,root);
    }
    recorded_direct_state(s);hr=S_OK;
    fprintf(stderr,"scarlett: carried #42 +%s recorder=%p segment=%p bound=%p prior=%p activeRoot=%p required=%p\n",
            set_root?"110":"c8",s,s->host,s->bound,s->prior_pso,s->graphics_identity,s->required_root);
done:
    ReleaseSRWLockExclusive(&s->borrow_lock);return hr;
}
