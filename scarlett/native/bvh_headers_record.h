HRESULT xs_record_bvh_headers(IUnknown *pipeline,ID3D12GraphicsCommandList *list,const struct xs_bvh_range *dst,
        const struct xs_bvh_range *extra,const void *commands,SIZE_T bytes,UINT count,struct xs_bvh_hold **out)
{
    if (!out) return E_POINTER;
    *out=NULL;
    struct header_cmd { UINT64 va[2]; UINT fields[6]; } cmd[64];
    _Static_assert(sizeof cmd[0]==40 && offsetof(struct header_cmd,fields)==16,"HeaderInitializer");
    if (count>ARRAYSIZE(cmd)) return E_NOTIMPL;
    if (bytes!=count*sizeof cmd[0]) return E_INVALIDARG;
    if (bytes && !copy_in(cmd,commands,bytes)) return E_POINTER;
    ID3D12Device *device=NULL;
    HRESULT hr=bvh_list_device(pipeline,BVH_HEADERS,list,&device);
    if (FAILED(hr)) return hr;
    const struct xs_bvh_range *windows[]={dst,extra};
    UINT64 gpu[2];
    UINT n=extra ? 2 : 1;
    for (UINT k=0;k<n;k++) {
        const struct xs_bvh_range *r=windows[k];
        if (!r || !r->bytes || r->bytes>UINT32_MAX || (r->bytes&3)) return E_INVALIDARG;
        UINT64 first;
        hr=bvh_view_offset(device,r,r->guest_base,(UINT)r->bytes,TRUE,&first);
        if (FAILED(hr)) return hr;
        gpu[k]=ID3D12Resource_GetGPUVirtualAddress(r->resource);
        if (!gpu[k] || gpu[k]>UINT64_MAX-first || gpu[k]+first>UINT64_MAX-r->bytes) return E_INVALIDARG;
        gpu[k]+=first;
    }
    if (extra && ((dst->guest_base<extra->guest_base+extra->bytes && extra->guest_base<dst->guest_base+dst->bytes) ||
            (gpu[0]<gpu[1]+extra->bytes && gpu[1]<gpu[0]+dst->bytes))) return E_INVALIDARG;

    struct { UINT64 lo,hi; } spans[2*ARRAYSIZE(cmd)];
    UINT used=0;
    for (UINT i=0;i<count;i++) for (UINT k=0;k<2;k++) {
        const UINT64 va=cmd[i].va[k],width=k ? 24 : 64;
        if (k && !va) continue;
        const struct xs_bvh_range *r=k && extra ? extra : dst;
        if ((va&3) || va<r->guest_base || width>r->bytes || va-r->guest_base>r->bytes-width) return E_INVALIDARG;
        for (UINT j=0;j<used;j++)
            if (spans[j].lo<va+width && va<spans[j].hi) return E_INVALIDARG;
        spans[used].lo=va; spans[used++].hi=va+width;
    }
    struct bvh_pipeline *p=CONTAINING_RECORD(pipeline,struct bvh_pipeline,iface);
    struct xs_bvh_hold *h=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof *h);
    if (!h) return E_OUTOFMEMORY;
    h->pipeline=pipeline; IUnknown_AddRef(pipeline);
    h->dst=dst->resource; ID3D12Resource_AddRef(h->dst);
    if (extra) { h->extra=extra->resource; ID3D12Resource_AddRef(h->extra); }
    if (count) {
        D3D12_HEAP_PROPERTIES hp={.Type=D3D12_HEAP_TYPE_UPLOAD,.CreationNodeMask=1,.VisibleNodeMask=1};
        D3D12_RESOURCE_DESC rd={.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER,.Width=bytes,.Height=1,
            .DepthOrArraySize=1,.MipLevels=1,.SampleDesc={1,0},.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR};
        hr=ID3D12Device_CreateCommittedResource(device,&hp,D3D12_HEAP_FLAG_NONE,&rd,
                D3D12_RESOURCE_STATE_GENERIC_READ,NULL,&IID_ID3D12Resource,(void **)&h->upload);
        if (FAILED(hr)) { xs_drop_bvh_hold(h); return hr; }
        void *mapped=NULL; D3D12_RANGE read={0,0};
        hr=ID3D12Resource_Map(h->upload,0,&read,&mapped);
        if (FAILED(hr)) { xs_drop_bvh_hold(h); return hr; }
        memcpy(mapped,cmd,bytes); ID3D12Resource_Unmap(h->upload,0,NULL);
        UINT64 t0=ID3D12Resource_GetGPUVirtualAddress(h->upload);
        if (!t0 || t0>UINT64_MAX-bytes) { xs_drop_bvh_hold(h); return E_INVALIDARG; }
        ID3D12GraphicsCommandList_SetPipelineState(list,p->host);
        ID3D12GraphicsCommandList_SetComputeRootSignature(list,p->root);
        ID3D12GraphicsCommandList_SetComputeRootShaderResourceView(list,0,t0);
        ID3D12GraphicsCommandList_SetComputeRoot32BitConstant(list,1,count,0);
        /* In split mode the checked store skips the other, disjoint window. */
        for (UINT k=0;k<n;k++) {
            const struct xs_bvh_range *r=windows[k];
            UINT bounds[]={(UINT)r->guest_base,(UINT)(r->guest_base>>32),(UINT)r->bytes};
            ID3D12GraphicsCommandList_SetComputeRoot32BitConstants(list,2,3,bounds,0);
            ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(list,3,gpu[k]);
            ID3D12GraphicsCommandList_Dispatch(list,(count+31)/32,1,1);
        }
    }
    *out=h;
    return S_OK;
}
