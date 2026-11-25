/* Pinned title's type-zero CPU records. All borrowed addresses are integers,
 * never a COM dispatch. Only use for +72a1 close and +24821e3 pair admission. */
struct pub_wire {
    BYTE kind,pad[7];UINT_PTR list,arg,pool;
    BYTE opaque[0x40];UINT_PTR acq,rel,next;
};
_Static_assert(sizeof(struct pub_wire)==0x78 && offsetof(struct pub_wire,acq)==0x60,"record ABI");
struct pub_seal {UINT_PTR record,list,arg,pool,node;UINT gen;};
static BOOL pub_read(UINT_PTR p,void *dst,SIZE_T n)
{
    SIZE_T got;
    return p && n<=UINTPTR_MAX-p && ReadProcessMemory(GetCurrentProcess(),(void*)p,dst,n,&got) && got==n;
}
static BOOL pub_close(UINT_PTR ctx,UINT_PTR id,BOOL producer,UINT_PTR kn,UINT gen,struct pub_seal *out)
{
    UINT_PTR active[3],poolList;struct pub_wire w;UINT g;
    if(!ctx || ctx>UINTPTR_MAX-0x127e0 || !id || !kn || kn>UINTPTR_MAX-0x58 || !gen ||
       !pub_read(ctx+0x127c8,active,sizeof active) || active[0]!=id || active[2]<0x70 ||
       !pub_read(active[2]-0x70,&w,sizeof w) || w.kind || w.list!=id || !w.pool || w.arg!=active[1] ||
       !pub_read(w.arg,&poolList,sizeof poolList) || poolList!=id || !pub_read(kn+0x24,&g,sizeof g) || g!=gen ||
       (producer ? w.acq!=kn || w.rel : w.acq || (w.rel && w.rel!=kn)))return FALSE;
    *out=(struct pub_seal){active[2]-0x70,id,w.arg,w.pool,kn,g};return TRUE;
}
static BOOL pub_pair(const struct pub_seal *p,const struct pub_seal *s,UINT_PTR lists,UINT64 stamp)
{
    struct pub_wire w[2];UINT_PTR ids[2],args[2],pools[2],links[2],owner;
    struct {UINT64 stamp;UINT holds,gen;} node;
    if(!p->record || !s->record || !p->gen || !p->node || p->node>UINTPTR_MAX-0x58 || p->node!=s->node ||
       p->gen!=s->gen || p->list==s->list || p->record==s->record || p->arg==s->arg || p->pool!=s->pool ||
       !lists || lists>UINTPTR_MAX-0x1010 || !pub_read(lists,ids,sizeof ids) ||
       !pub_read(lists+0x800,pools,sizeof pools) || !pub_read(lists+0x1000,args,sizeof args) ||
       !pub_read(p->record,w,sizeof w[0]) || !pub_read(s->record,w+1,sizeof w[1]))return FALSE;
    for(UINT j=0;j<2;j++){
        const struct pub_seal *g=j?s:p;
        if(w[j].kind || w[j].list!=g->list || w[j].arg!=g->arg || w[j].pool!=g->pool ||
           ids[j]!=g->list || pools[j]!=g->pool || args[j]!=g->arg)return FALSE;
    }
    return w[0].next==s->record && w[0].acq==p->node && !w[0].rel && !w[1].acq &&
        w[1].rel==p->node && !w[1].next && pub_read(p->node+0x18,&node,sizeof node) &&
        node.gen==p->gen && !node.holds && node.stamp==stamp &&
        pub_read(p->node+0x30,links,sizeof links) && !links[0] && !links[1] &&
        pub_read(p->node+0x40,&owner,sizeof owner) && !owner;
}
