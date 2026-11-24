struct partitions_cmd { UINT id,translation[3],n,reserved; UINT64 va; };
struct partitions_job { UINT count_offset,list_offset,n,mask,indices[32]; };
_Static_assert(sizeof(struct partitions_cmd)==32 && offsetof(struct partitions_cmd,va)==24,"partition wire");
_Static_assert(sizeof(struct partitions_job)==36*4,"partition root constants");

HRESULT xs_record_bvh_partitions(IUnknown *pipeline,ID3D12GraphicsCommandList *list,const struct xs_bvh_range *dst,
        const void *header80,const struct xs_bvh_range *src,const void *commands,SIZE_T command_bytes,
        UINT count,struct xs_bvh_hold **out)
{
    if (!out) return E_POINTER;
    *out=NULL;
    if (!list || !pipeline || pipeline->lpVtbl!=&bp_methods.unknown ||
        !dst || !dst->resource || !dst->guest_base || dst->bytes<80 || dst->bytes>UINT32_MAX ||
        (dst->bytes&3) || dst->bytes>UINT64_MAX-dst->guest_base) return E_INVALIDARG;
    struct bvh_pipeline *p=CONTAINING_RECORD(pipeline,struct bvh_pipeline,iface);
    if (p->kind!=BVH_PARTITIONS) return E_INVALIDARG;
    // shortcut: fixed batch and root constant budget; reject larger submissions until qualified.
    struct partitions_cmd cmd[32];
    struct partitions_job jobs[32]={0};
    UINT hd[20],used=0;
    BOOL global_used=FALSE,needs_src=FALSE;
    if (count>ARRAYSIZE(cmd)) return E_NOTIMPL;
    if (command_bytes!=count*sizeof *cmd) return E_INVALIDARG;
    if (!copy_in(hd,header80,sizeof hd) || (count && !copy_in(cmd,commands,count*sizeof *cmd))) return E_POINTER;
    for (UINT i=0;i<count;i++) {
        struct partitions_job *job=&jobs[i];
        BOOL global=cmd[i].id==0xfffffffeu;
        UINT part=global ? hd[16] : cmd[i].id;
        if (global ? global_used : (part>=hd[16])) return E_INVALIDARG;
        if (global) global_used=TRUE;
        else {
            job->mask=1u<<(part&31);
            if (used&job->mask) return E_INVALIDARG; /* Includes distinct IDs with colliding mask bits. */
            used|=job->mask;
        }
        UINT64 slot=80+28ull*part;
        if (slot+4>dst->bytes) return E_INVALIDARG;
        job->count_offset=(UINT)slot;
        job->n=cmd[i].n;
        if (job->n>ARRAYSIZE(job->indices)) return E_NOTIMPL;
        if (job->n>(global ? hd[18] : hd[17])) return E_INVALIDARG;
        if (!job->n) continue; /* Count and ordinary mask are still written. */
        UINT64 first=108+28ull*hd[16], element=(UINT64)part*hd[17];
        if (first>dst->bytes || element+job->n>(dst->bytes-first)/4) return E_INVALIDARG;
        job->list_offset=(UINT)(first+4*element);
        needs_src=TRUE;
    }
    if (needs_src) {
        if (!src || !src->resource || src->resource==dst->resource || !src->guest_base ||
            src->bytes>UINT64_MAX-src->guest_base ||
            (src->guest_base<dst->guest_base+dst->bytes && dst->guest_base<src->guest_base+src->bytes)) return E_INVALIDARG;
        ID3D12Resource *r=src->resource;
        D3D12_RESOURCE_DESC rd; r->lpVtbl->GetDesc(r,&rd);
        if (rd.Dimension!=D3D12_RESOURCE_DIMENSION_BUFFER || src->resource_offset>rd.Width ||
            src->bytes>rd.Width-src->resource_offset) return E_INVALIDARG;
        for (UINT i=0;i<count;i++) if (cmd[i].n) {
            UINT64 delta=cmd[i].va-src->guest_base;
            if ((cmd[i].va&3) || cmd[i].va<src->guest_base || delta>src->bytes || 4u*cmd[i].n>src->bytes-delta)
                return E_INVALIDARG;
        }
        ID3D12Device *device=NULL;
        HRESULT hr=ID3D12Resource_GetDevice(r,&IID_ID3D12Device,(void **)&device);
        if (FAILED(hr)) return hr;
        BOOL same=device==p->owner->host; ID3D12Device_Release(device);
        if (!same) return E_INVALIDARG;
        D3D12_HEAP_PROPERTIES hp;
        hr=ID3D12Resource_GetHeapProperties(r,&hp,NULL);
        if (FAILED(hr)) return hr;
        if (hp.Type!=D3D12_HEAP_TYPE_UPLOAD) return E_INVALIDARG;
        BYTE *mapped=NULL;
        D3D12_RANGE read={src->resource_offset,src->resource_offset+src->bytes},written={0,0};
        hr=ID3D12Resource_Map(r,0,&read,(void **)&mapped);
        if (FAILED(hr)) return hr;
        for (UINT i=0;i<count;i++) if (cmd[i].n) {
            UINT64 at=src->resource_offset+(cmd[i].va-src->guest_base);
            memcpy(jobs[i].indices,mapped+at,4*cmd[i].n);
        }
        ID3D12Resource_Unmap(r,0,&written);
    }
    HRESULT hr=bvh_record_target(pipeline,BVH_PARTITIONS,list,dst,dst->guest_base,(UINT)dst->bytes,out);
    if (SUCCEEDED(hr)) for (UINT i=0;i<count;i++) {
        ID3D12GraphicsCommandList_SetComputeRoot32BitConstants(list,1,36,&jobs[i],0);
        ID3D12GraphicsCommandList_Dispatch(list,1,1,1);
    }
    return hr;
}
