/* One private window: guest address, byte count, byte offset in its original UAV. */
static BOOL begin_window(const struct xs_bvh_range *r,UINT64 addr,UINT size,UINT w[4])
{
    UINT64 delta=addr-r->guest_base;
    if (addr<r->guest_base || (addr&3) || delta>r->bytes || size>r->bytes-delta ||
        r->resource_offset>UINT32_MAX || delta>UINT32_MAX-r->resource_offset) return FALSE;
    UINT offset=(UINT)(r->resource_offset+delta);
    if (offset&3) return FALSE;
    w[0]=(UINT)addr; w[1]=(UINT)(addr>>32); w[2]=size; w[3]=offset;
    return TRUE;
}
HRESULT xs_record_bvh_beginpass(IUnknown *pipeline,ID3D12GraphicsCommandList *list,
        const struct xs_bvh_range *t,const struct xs_bvh_range *c,const void *b0,SIZE_T bytes,struct xs_bvh_hold **out)
{
    if (!out) return E_POINTER;
    *out=NULL;
    if (!pipeline || pipeline->lpVtbl!=&bp_methods.unknown || !t || !c || !t->resource || !c->resource ||
        t->resource==c->resource || !t->guest_base || !c->guest_base || t->bytes>UINT32_MAX ||
        (t->bytes&3) || t->bytes>UINT64_MAX-t->guest_base || c->bytes>UINT64_MAX-c->guest_base ||
        (t->guest_base<c->guest_base+c->bytes && c->guest_base<t->guest_base+t->bytes)) return E_INVALIDARG;
    struct bvh_pipeline *p=CONTAINING_RECORD(pipeline,struct bvh_pipeline,iface);
    UINT words[5],windows[12]={0};
    if (p->kind!=BVH_BEGIN_PASS || bytes!=sizeof words) return E_INVALIDARG;
    if (!copy_in(words,b0,sizeof words)) return E_POINTER;
    UINT64 tv=(UINT64)words[0]|((UINT64)words[1]<<32), cv=(UINT64)words[2]|((UINT64)words[3]<<32);
    BOOL global=words[4]==0xfffffffeu;
    UINT64 offset=global ? 16 : 84+28ull*words[4];
    /* Reject overflowing guest i32 multiply/add, rather than resetting a wrapped slot. */
    if (tv<t->guest_base || offset>UINT32_MAX || tv>UINT64_MAX-offset ||
        (!global && !begin_window(t,tv+64,8,windows)) || !begin_window(t,tv+offset,24,windows+4) ||
        !begin_window(c,cv,24,windows+8)) return E_INVALIDARG;
    D3D12_RESOURCE_DESC rd; c->resource->lpVtbl->GetDesc(c->resource,&rd);
    if (rd.Dimension!=D3D12_RESOURCE_DIMENSION_BUFFER || !(rd.Flags&D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) ||
        c->resource_offset>rd.Width || c->bytes>rd.Width-c->resource_offset) return E_INVALIDARG;
    ID3D12Device *device=NULL;
    HRESULT hr=ID3D12Resource_GetDevice(c->resource,&IID_ID3D12Device,(void **)&device);
    if (FAILED(hr)) return hr;
    BOOL same=device==p->owner->host; ID3D12Device_Release(device);
    UINT64 tbase=ID3D12Resource_GetGPUVirtualAddress(t->resource), cbase=ID3D12Resource_GetGPUVirtualAddress(c->resource);
    if (!same || !tbase || !cbase) return E_INVALIDARG;
    hr=bvh_record_target(pipeline,BVH_BEGIN_PASS,list,t,t->guest_base,(UINT)t->bytes,out);
    if (FAILED(hr)) return hr;
    (*out)->extra=c->resource; ID3D12Resource_AddRef(c->resource);
    ID3D12GraphicsCommandList_SetComputeRoot32BitConstants(list,1,5,words,0);
    ID3D12GraphicsCommandList_SetComputeRoot32BitConstants(list,2,12,windows,0);
    ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(list,3,tbase);
    ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(list,4,cbase);
    ID3D12GraphicsCommandList_Dispatch(list,1,1,1);
    return S_OK;
}
