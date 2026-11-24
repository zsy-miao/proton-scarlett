/* Preflight the entire batch before touching the list. Root constants take a
 * private copy at record time, so no source memory remains live on the GPU. */
struct offline_cmd { UINT id,n; UINT64 va; };
struct offline_node { UINT dst,box,mask,self,child[4],parent[4],leaf[4],bounds[24]; };
_Static_assert(sizeof(struct offline_cmd)==16 && sizeof(struct offline_node)==160,"offline wire and host layout");
struct offline_span { UINT begin,end; };
static int offline_span_cmp(const void *a,const void *b)
{
    UINT x=((const struct offline_span *)a)->begin, y=((const struct offline_span *)b)->begin;
    return (x>y)-(x<y);
}
static BOOL offline_span(struct offline_span **next,UINT64 at,UINT bytes,UINT limit)
{
    if ((at&3) || at<80 || at>limit || bytes>limit-at) return FALSE;
    *(*next)++=(struct offline_span){(UINT)at,(UINT)at+bytes};
    return TRUE;
}
HRESULT xs_record_bvh_offline(IUnknown *pipeline,ID3D12GraphicsCommandList *list,const struct xs_bvh_range *dst,
        const void *header80,const struct xs_bvh_range *source,const void *commands,SIZE_T command_bytes,
        UINT count,struct xs_bvh_hold **out)
{
    if (!out) return E_POINTER;
    *out=NULL;
    if (!dst || !dst->resource || !dst->guest_base || dst->bytes>UINT64_MAX-dst->guest_base ||
        dst->bytes<80 || dst->bytes>UINT32_MAX || (dst->bytes&3) ||
        !pipeline || pipeline->lpVtbl!=&bp_methods.unknown) return E_INVALIDARG;
    struct bvh_pipeline *p=CONTAINING_RECORD(pipeline,struct bvh_pipeline,iface);
    if (!p->offline || !list) return E_INVALIDARG;
    /* Guard host work as well as shader extents. No truncation or partial submit. */
    struct offline_cmd cmd[256];
    UINT hd[20],total=0;
    if (count>ARRAYSIZE(cmd)) return E_NOTIMPL;
    if (command_bytes!=count*sizeof *cmd) return E_INVALIDARG;
    if (!copy_in(hd,header80,sizeof hd) || (count && !copy_in(cmd,commands,count*sizeof *cmd))) return E_POINTER;
    for (UINT i=0;i<count;i++) {
        if (cmd[i].n>1024-total) return E_NOTIMPL;
        total+=cmd[i].n;
    }
    struct offline_node *nodes=NULL;
    struct offline_span *spans=NULL,*end=NULL;
    BYTE *mapped=NULL;
    HRESULT hr=E_INVALIDARG;
    if (total) {
        if (!source || !source->resource || source->resource==dst->resource || !source->guest_base ||
            source->bytes>UINT64_MAX-source->guest_base) return E_INVALIDARG;
        if (source->guest_base<dst->guest_base+dst->bytes && dst->guest_base<source->guest_base+source->bytes)
            return E_INVALIDARG;
        ID3D12Resource *r=source->resource;
        ID3D12Device *device=NULL;
        hr=ID3D12Resource_GetDevice(r,&IID_ID3D12Device,(void **)&device);
        if (FAILED(hr)) return hr;
        BOOL same=device==p->owner->host; ID3D12Device_Release(device);
        D3D12_RESOURCE_DESC rd; r->lpVtbl->GetDesc(r,&rd);
        if (!same || rd.Dimension!=D3D12_RESOURCE_DIMENSION_BUFFER || source->resource_offset>rd.Width ||
            source->bytes>rd.Width-source->resource_offset) return E_INVALIDARG;
        D3D12_HEAP_PROPERTIES hp;
        hr=ID3D12Resource_GetHeapProperties(r,&hp,NULL);
        if (FAILED(hr)) return hr;
        if (hp.Type!=D3D12_HEAP_TYPE_UPLOAD) return E_INVALIDARG;
        D3D12_RANGE read={source->resource_offset,source->resource_offset+source->bytes};
        hr=ID3D12Resource_Map(r,0,&read,(void **)&mapped);
        if (FAILED(hr)) return hr;
        nodes=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,total*sizeof *nodes);
        end=spans=HeapAlloc(GetProcessHeap(),0,total*10*sizeof *spans);
        if (!nodes || !spans) { hr=E_OUTOFMEMORY; goto done; }
    }
    hr=E_INVALIDARG;
    UINT index=0;
    for (UINT i=0;i<count;i++) {
        const struct offline_cmd c=cmd[i];
        if (!c.n) continue;
        UINT64 delta=c.va-source->guest_base;
        if ((c.va&3) || c.va<source->guest_base || delta>source->bytes || 128ull*c.n>source->bytes-delta) goto done;
        BOOL global=c.id==0xfffffffe;
        UINT part=global ? hd[16] : c.id;
        if ((!global && part>=hd[16]) || c.n>(global ? hd[18] : hd[17])) goto done;
        UINT64 slot=(UINT64)part*hd[17];
        if (slot>(UINT32_MAX-hd[10])/128 || ((hd[10]|hd[13])&63) || (hd[14]&3)) goto done;
        UINT64 base=hd[10]+slot*128;
        for (UINT j=0;j<c.n;j++) {
            struct offline_node *n=&nodes[index++];
            UINT w[32]; memcpy(w,mapped+source->resource_offset+delta+128*j,sizeof w);
            UINT64 d=base+128*j;
            if (!offline_span(&end,d,112,(UINT)dst->bytes)) goto done;
            n->dst=(UINT)d; n->self=((n->dst>>6)<<3)|5;
            n->box=UINT32_MAX;
            if (!j) {
                UINT64 box=84+28ull*part;
                if (!offline_span(&end,box,24,(UINT)dst->bytes)) goto done;
                n->box=(UINT)box;
                if (!global) n->mask=1u<<(part&31);
            }
            memcpy(n->bounds,w+4,sizeof n->bounds);
            for (UINT k=0;k<4;k++) {
                n->child[k]=n->leaf[k]=UINT32_MAX;
                if (w[k]==UINT32_MAX) continue;
                BOOL leaf=w[28+k]!=0;
                if (!leaf && w[k]>=c.n) goto done;
                UINT64 offset=(leaf ? hd[13] : base)+128ull*w[k];
                if (offset>=dst->bytes) goto done;
                n->child[k]=(((UINT)offset>>6)<<3)|(leaf ? 6 : 5);
                if (leaf) {
                    if (!offline_span(&end,offset+116,4,(UINT)dst->bytes)) goto done;
                    n->leaf[k]=(UINT)offset+116;
                }
                /* Internal children can legitimately land BEFORE the parent anchor. */
                INT64 at=(INT64)hd[14]+4*((INT64)(offset>>6)-(INT64)(hd[13]>>6));
                if (at<0 || !offline_span(&end,(UINT64)at,4,(UINT)dst->bytes)) goto done;
                n->parent[k]=(UINT)at;
            }
        }
    }
    if (total) {
        qsort(spans,end-spans,sizeof *spans,offline_span_cmp);
        for (struct offline_span *s=spans+1;s<end;s++) if (s->begin<s[-1].end) goto done;
    }
    hr=bvh_record_target(pipeline,TRUE,list,dst,dst->guest_base,(UINT)dst->bytes,out);
    if (SUCCEEDED(hr)) for (UINT i=0;i<total;i++) {
        ID3D12GraphicsCommandList_SetComputeRoot32BitConstants(list,1,40,&nodes[i],0);
        ID3D12GraphicsCommandList_Dispatch(list,1,1,1);
    }
done:
    if (mapped) {
        D3D12_RANGE written={0,0};
        ID3D12Resource_Unmap(source->resource,0,&written);
    }
    HeapFree(GetProcessHeap(),0,spans); HeapFree(GetProcessHeap(),0,nodes);
    return hr;
}
