HRESULT xs_record_bvh_morton(IUnknown *pipeline,ID3D12GraphicsCommandList *list,
        const struct xs_bvh_range *arena,const UINT offsets[4],const UINT b0[8],
        UINT64 guest_base,const void *snapshot,UINT bytes,struct xs_bvh_hold **out)
{
    if (!out) return E_POINTER;
    *out=NULL;
    if (!pipeline || pipeline->lpVtbl!=&bp_methods.unknown || !list || !arena || !arena->resource ||
        !guest_base || (guest_base&3) || !bytes || (bytes&3) || bytes>UINT64_MAX-guest_base) return E_INVALIDARG;
    struct bvh_pipeline *p=CONTAINING_RECORD(pipeline,struct bvh_pipeline,iface);
    if (p->kind!=BVH_MORTON32 && p->kind!=BVH_MORTON64) return E_INVALIDARG;
    ID3D12Device *d=NULL;
    HRESULT hr=bvh_list_device(pipeline,p->kind,list,&d);
    if (FAILED(hr)) return hr;
    UINT k[8],off[4];
    if (!copy_in(k,b0,sizeof k) || !copy_in(off,offsets,sizeof off)) return E_POINTER;
    if (!k[4] || k[4]>65536 || bytes>1024*1024) return E_NOTIMPL;
    /* Partitioned mode uses a count loaded indirectly, not k[4]. Reserve all
     * dispatched lanes even if the header advertises more than k[4]. */
    UINT lanes=(k[4]+31)&~31u;
    UINT widths[4]={4*lanes,(p->kind==BVH_MORTON64?8:4)*lanes,24*lanes,24};
    ID3D12Resource *r=arena->resource;
    UINT64 va=ID3D12Resource_GetGPUVirtualAddress(r), first[4];
    if (!va || !arena->guest_base || arena->bytes>UINT64_MAX-arena->guest_base) return E_INVALIDARG;
    for (UINT i=0;i<4;i++) {
        if (off[i]>arena->bytes) return E_INVALIDARG;
        hr=bvh_view_offset(d,arena,arena->guest_base+off[i],widths[i],TRUE,&first[i]);
        if (FAILED(hr)) return hr;
        if (first[i]>UINT64_MAX-va || widths[i]>UINT64_MAX-va-first[i]) return E_INVALIDARG;
        for (UINT j=0;j<i;j++) if (first[i]<first[j]+widths[j] && first[j]<first[i]+widths[i]) return E_INVALIDARG;
    }
    struct xs_bvh_hold *h=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof *h);
    if (!h) return E_OUTOFMEMORY;
    D3D12_HEAP_PROPERTIES hp={.Type=D3D12_HEAP_TYPE_UPLOAD,.CreationNodeMask=1,.VisibleNodeMask=1};
    const UINT cb=D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT;
    D3D12_RESOURCE_DESC upload={.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER,.Width=cb+bytes,.Height=1,
        .DepthOrArraySize=1,.MipLevels=1,.SampleDesc={1,0},.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR};
    hr=ID3D12Device_CreateCommittedResource(d,&hp,D3D12_HEAP_FLAG_NONE,&upload,D3D12_RESOURCE_STATE_GENERIC_READ,
            NULL,&IID_ID3D12Resource,(void **)&h->upload);
    if (FAILED(hr)) { HeapFree(GetProcessHeap(),0,h); return hr; }
    UINT64 host=ID3D12Resource_GetGPUVirtualAddress(h->upload);
    void *mapped=NULL; D3D12_RANGE read={0,0};
    if (!host || upload.Width>UINT64_MAX-host) hr=E_INVALIDARG;
    else hr=ID3D12Resource_Map(h->upload,0,&read,&mapped);
    if (SUCCEEDED(hr)) {
        memcpy(mapped,k,sizeof k);
        if (!copy_in((BYTE *)mapped+cb,snapshot,bytes)) hr=E_POINTER;
        ID3D12Resource_Unmap(h->upload,0,NULL);
    }
    if (FAILED(hr)) { ID3D12Resource_Release(h->upload); HeapFree(GetProcessHeap(),0,h); return hr; }
    h->dst=r; ID3D12Resource_AddRef(r); h->pipeline=pipeline; IUnknown_AddRef(pipeline);
    UINT range[3]={(UINT)guest_base,(UINT)(guest_base>>32),bytes};
    ID3D12GraphicsCommandList_SetPipelineState(list,p->host);
    ID3D12GraphicsCommandList_SetComputeRootSignature(list,p->root);
    ID3D12GraphicsCommandList_SetComputeRootConstantBufferView(list,0,host);
    for (UINT i=0;i<4;i++) ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(list,i+1,va+first[i]);
    ID3D12GraphicsCommandList_SetComputeRoot32BitConstants(list,5,3,range,0);
    ID3D12GraphicsCommandList_SetComputeRootShaderResourceView(list,6,host+cb);
    ID3D12GraphicsCommandList_Dispatch(list,lanes/32,1,1); *out=h;
    return S_OK;
}
