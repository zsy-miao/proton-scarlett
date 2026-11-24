struct instance_job { struct { UINT words[21],index; UINT64 blas; } cmd; BYTE header[64]; };
_Static_assert(sizeof(struct instance_job)==160 && offsetof(struct instance_job,cmd.blas)==88,"instance upload");
HRESULT xs_record_bvh_instances(IUnknown *pipeline,ID3D12GraphicsCommandList *list,const struct xs_bvh_range *dst,
        const void *header80,const struct xs_bvh_range *src,const void *commands,SIZE_T bytes,UINT count,struct xs_bvh_hold **out)
{
    if (!out) return E_POINTER;
    *out=NULL;
    if (!list || !pipeline || pipeline->lpVtbl!=&bp_methods.unknown || !dst || !dst->resource ||
        !dst->guest_base || dst->bytes>UINT64_MAX-dst->guest_base || dst->bytes<80 ||
        dst->bytes>UINT32_MAX || (dst->bytes&3)) return E_INVALIDARG;
    struct bvh_pipeline *p=CONTAINING_RECORD(pipeline,struct bvh_pipeline,iface);
    if (p->kind!=BVH_INSTANCES) return E_INVALIDARG;
    struct instance_job jobs[32]={0};
    if (count>ARRAYSIZE(jobs)) return E_NOTIMPL;
    if (bytes!=count*sizeof jobs[0].cmd) return E_INVALIDARG;
    UINT hd[20]; BYTE wire[32*sizeof jobs[0].cmd];
    if (!copy_in(hd,header80,sizeof hd) || (bytes && !copy_in(wire,commands,bytes))) return E_POINTER;
    BOOL need=FALSE;
    for (UINT i=0;i<count;i++) {
        memcpy(&jobs[i].cmd,wire+i*sizeof jobs[i].cmd,sizeof jobs[i].cmd);
        UINT index=jobs[i].cmd.index;
        UINT64 at=hd[13]+(UINT64)index*128;
        if (hd[13]<80 || (hd[13]&3) || index>=hd[3] || at+128>dst->bytes) return E_INVALIDARG;
        for (UINT j=0;j<i;j++) if (jobs[j].cmd.index==index) return E_INVALIDARG;
        if (jobs[i].cmd.blas) need=TRUE;
    }
    if (need) {
        if (!src || !src->resource || src->resource==dst->resource || !src->guest_base ||
            src->bytes>UINT64_MAX-src->guest_base ||
            (src->guest_base<dst->guest_base+dst->bytes && dst->guest_base<src->guest_base+src->bytes)) return E_INVALIDARG;
        ID3D12Resource *r=src->resource;
        D3D12_RESOURCE_DESC rd; r->lpVtbl->GetDesc(r,&rd);
        if (rd.Dimension!=D3D12_RESOURCE_DIMENSION_BUFFER || src->resource_offset>rd.Width ||
            src->bytes>rd.Width-src->resource_offset) return E_INVALIDARG;
        for (UINT i=0;i<count;i++) if (jobs[i].cmd.blas) {
            UINT64 va=jobs[i].cmd.blas, delta=va-src->guest_base;
            if ((va&255) || va<src->guest_base || delta>src->bytes || 64>src->bytes-delta) return E_INVALIDARG;
        }
        ID3D12Device *d=NULL;
        HRESULT hr=ID3D12Resource_GetDevice(r,&IID_ID3D12Device,(void **)&d);
        if (FAILED(hr)) return hr;
        BOOL same=d==p->owner->host; ID3D12Device_Release(d);
        if (!same) return E_INVALIDARG;
        D3D12_HEAP_PROPERTIES hp;
        hr=ID3D12Resource_GetHeapProperties(r,&hp,NULL);
        if (FAILED(hr)) return hr;
        if (hp.Type!=D3D12_HEAP_TYPE_UPLOAD) return E_INVALIDARG;
        BYTE *mapped=NULL; D3D12_RANGE read={src->resource_offset,src->resource_offset+src->bytes},written={0,0};
        hr=ID3D12Resource_Map(r,0,&read,(void **)&mapped);
        if (FAILED(hr)) return hr;
        for (UINT i=0;i<count;i++) if (jobs[i].cmd.blas)
            memcpy(jobs[i].header,mapped+src->resource_offset+(jobs[i].cmd.blas-src->guest_base),64);
        ID3D12Resource_Unmap(r,0,&written);
    }
    ID3D12Resource *upload=NULL;
    D3D12_GPU_VIRTUAL_ADDRESS base=0;
    if (count) {
        D3D12_HEAP_PROPERTIES hp={.Type=D3D12_HEAP_TYPE_UPLOAD,.CreationNodeMask=1,.VisibleNodeMask=1};
        D3D12_RESOURCE_DESC rd={.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER,.Width=count*sizeof *jobs,.Height=1,
            .DepthOrArraySize=1,.MipLevels=1,.SampleDesc={1,0},.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR};
        HRESULT hr=ID3D12Device_CreateCommittedResource(p->owner->host,&hp,D3D12_HEAP_FLAG_NONE,&rd,
            D3D12_RESOURCE_STATE_GENERIC_READ,NULL,&IID_ID3D12Resource,(void **)&upload);
        if (FAILED(hr)) return hr;
        base=ID3D12Resource_GetGPUVirtualAddress(upload);
        if (!base || base>UINT64_MAX-rd.Width) { ID3D12Resource_Release(upload); return E_INVALIDARG; }
        void *mapped=NULL; D3D12_RANGE read={0,0};
        hr=ID3D12Resource_Map(upload,0,&read,&mapped);
        if (FAILED(hr)) { ID3D12Resource_Release(upload); return hr; }
        memcpy(mapped,jobs,(SIZE_T)rd.Width); ID3D12Resource_Unmap(upload,0,NULL);
    }
    HRESULT hr=bvh_record_target(pipeline,BVH_INSTANCES,list,dst,dst->guest_base,(UINT)dst->bytes,out);
    if (FAILED(hr)) { if (upload) ID3D12Resource_Release(upload); return hr; }
    (*out)->upload=upload;
    for (UINT i=0;i<count;i++) {
        UINT64 va=jobs[i].cmd.blas;
        UINT range[3]={(UINT)va,(UINT)(va>>32),64};
        ID3D12GraphicsCommandList_SetComputeRootShaderResourceView(list,1,base);
        ID3D12GraphicsCommandList_SetComputeRoot32BitConstant(list,2,1,0);
        ID3D12GraphicsCommandList_SetComputeRoot32BitConstants(list,3,3,range,0);
        ID3D12GraphicsCommandList_SetComputeRootShaderResourceView(list,4,base+sizeof jobs[i].cmd);
        ID3D12GraphicsCommandList_Dispatch(list,1,1,1);
        base+=sizeof *jobs;
    }
    return S_OK;
}
