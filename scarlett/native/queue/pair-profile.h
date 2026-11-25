/* Close supplies the final +048. Unknown typed slots, insertions and overflow
 * cannot compare equal; the read-only zero-capacity prefix prohibits packets. */
static BOOL pair_profile(struct startup_list *s)
{
#define SLOT(x) (0x##x/8)
    static const BYTE producer[]={SLOT(100),SLOT(238),SLOT(100),SLOT(240),SLOT(238),SLOT(250),SLOT(418),
        SLOT(490),SLOT(1a0),SLOT(0a0),SLOT(3f0),SLOT(0c8),SLOT(110),SLOT(258),SLOT(178),SLOT(0c0),
        SLOT(470),SLOT(0b8),SLOT(490),SLOT(190),SLOT(230),SLOT(0b0),SLOT(0a8),SLOT(160),SLOT(060),
        SLOT(0a0),SLOT(110),SLOT(180),SLOT(490),SLOT(190),SLOT(230),SLOT(120),SLOT(110),SLOT(180),
        SLOT(490),SLOT(190),SLOT(230),SLOT(120)};
    static const BYTE successor[]={SLOT(100),SLOT(490),SLOT(240)};
#undef SLOT
    struct system_timestamp *t=s->stamp;
    struct release_receipt *r=s->offered?s->offered:s->deferred;
    ID3D12DescriptorHeap *banks[2];
    if(!r || r->owner!=s->pool_owner || r->token!=2 || r->table_epoch!=2 ||
       !t || !t->dst || !t->query || t->tail_recorded || !s->heaps[0] || !s->heaps[1] ||
       FAILED(xs_initial_heap_banks(s->pool_owner,s->heaps,banks)))return FALSE;
    if(!s->offered){
        const D3D12_TEXTURE_BARRIER *b=&s->deferred_wire;
        return s->nops==sizeof successor && !memcmp(s->ops,successor,sizeof successor) &&
            !s->head && !s->marker && !s->post && !s->table_epoch && !s->primary &&
            r->claimed && b->pResource==(ID3D12Resource*)r->key && b->LayoutBefore==2 && b->LayoutAfter==31 &&
            s->pre_release && s->pre_release!=s->host && allocator_matches(s->after_alloc,s->device) &&
            r->producer_stamp && s->successor_query && (t->successor_bank?t==r->producer_stamp:
                t!=r->producer_stamp && t->cpu_section && t->mapping && !t->second_bank);
    }
    struct prefix_marker *m=s->marker,*p=s->post;
    if(s->nops!=sizeof producer || memcmp(s->ops,producer,sizeof producer) || s->deferred || s->pre_release ||
       !s->head || s->head==s->host || !allocator_matches(s->tail_allocator,s->device) ||
       !m || !p || p->parent!=m || m->value!=1 || !p->counter || p->threshold!=1 || s->post_boundary ||
       !t->second_bank || r->producer_stamp!=t || !s->primary_cleared || s->primary_released ||
       s->primary!=r->host || s->primary_pin!=r->key || s->output_slot!=r->slot || s->output_serial!=r->serial ||
       !s->rt_roundtripped || !s->clear4_drew || !s->prior_pso || !s->prior_root || !s->graphics_identity ||
       !s->attachments[0].identity || !s->attachments[1].identity || !s->om.heap[0] || !s->om.heap[1] ||
       !s->source.placed || s->previous_vb.placed!=s->source.placed || s->vb.placed!=s->source.placed ||
       !s->source.buffer || !s->previous_vb.buffer || !s->vb.buffer || s->previous_vb.buffer==s->vb.buffer ||
       s->clear4_lanes!=UINT64_MAX || s->table_epoch!=2 || s->guest_topology!=XS_RECTLIST)return FALSE;
    if(m->buffer)return xs_is_adapter(s->device,0x1002,0x164e) && s->split_at==6 && m->control_section &&
        m->heap && !m->alias && m->native_post==0x29a5ee15 && !p->word && t->retained && t->successor_bank;
    return xs_is_rtx3070(s->device) && s->split_at==5 && m->alias && !m->native_post && !m->heap &&
        m->word==(LONG*)((BYTE*)m->alias+0x50) && p->word==m->word+1 && p->value==0x29a5ee15 &&
        p->counter==m->alias && t->mapping && t->cpu_section && !t->retained;
}
