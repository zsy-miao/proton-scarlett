/* Emit an exact typed-slot inventory, in order, including the split prefix
 * length and the deferred barrier position. Overflow is not a full inventory. */
static void pair_inventory(const struct startup_list *s,IUnknown *id)
{
    char ops[3*sizeof s->ops+1],*out=ops;
    for(UINT i=0;i<s->nops && i<sizeof s->ops;i++)out+=sprintf(out,"%02x",s->ops[i]);
    *out=0;
    const struct prefix_marker *m=s->marker,*p=s->post;
    fprintf(stderr,"PAIR close list=%p producer=%u receipt=%p alloc=%p agen=%llu head=%p tail=%p pre=%p ops=%u:%s splitAt=%u prefix=%u alias=%p word=%p buffer=%p nativePost=%x postWord=%p postVal=%x counter=%p target=%llu stamp=%p mapping=%p dst=%p secondBank=%u tailRecorded=%u output=%zx/%llu epoch=%llu\n",
        id,!!s->offered,s->offered?s->offered:s->deferred,s->allocator,s->alloc_generation,s->head,s->host,s->pre_release,s->nops,ops,s->split_at,
        m?m->value:0,m?m->alias:NULL,m?m->word:NULL,m?m->buffer:NULL,m?m->native_post:0,p?p->word:NULL,p?p->value:0,
        p?p->counter:NULL,p?p->threshold:0,s->stamp,s->stamp?s->stamp->mapping:NULL,s->stamp?s->stamp->dst:NULL,
        s->stamp?s->stamp->second_bank:0,s->stamp?s->stamp->tail_recorded:0,s->output_slot,s->output_serial,s->table_epoch);
}
/* Recording alone does not enable retail submission. Inventory always wins. */
#if defined(PAIR_INVENTORY_ONLY) || !(defined(XS_TEST_OWNED_PAIR) || defined(SCARLETT_GUARDED_PAIR_DISPATCH))
static void pair_seal(struct startup_list *s,IUnknown *id){pair_inventory(s,id);}
static void pair_unseal(struct startup_list *s){(void)s;}
#else
#define XS_PAIR_DISPATCH
#include "pair-execute.h"
#endif
