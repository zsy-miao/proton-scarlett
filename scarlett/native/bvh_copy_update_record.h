HRESULT xs_record_bvh_copy_update(IUnknown *pipeline,ID3D12GraphicsCommandList *list,
        const struct xs_bvh_range *dst,const void *source,SIZE_T bytes,UINT reset_bounds,struct xs_bvh_hold **out)
{
    if (!out) return E_POINTER;
    *out=NULL;
    if (!pipeline || pipeline->lpVtbl!=&bp_methods.unknown || !list || !dst || !dst->resource ||
        reset_bounds>1 || bytes<64 || (bytes&63) || bytes>dst->bytes) return E_INVALIDARG;
    struct bvh_pipeline *p=CONTAINING_RECORD(pipeline,struct bvh_pipeline,iface);
    if (p->kind!=BVH_COPY_UPDATE) return E_INVALIDARG;
    if (bytes>1024*1024) return E_NOTIMPL; /* Bound the snapshot and the shader's strided loop. */
    UINT header[16];
    if (!copy_in(header,source,sizeof header)) return E_POINTER;
    if (header[1]!=bytes/64-1) return E_INVALIDARG;

    D3D12_HEAP_PROPERTIES hp={.Type=D3D12_HEAP_TYPE_UPLOAD,.CreationNodeMask=1,.VisibleNodeMask=1};
    D3D12_RESOURCE_DESC rd={.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER,.Width=bytes,.Height=1,
        .DepthOrArraySize=1,.MipLevels=1,.SampleDesc={1,0},.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR};
    ID3D12Resource *upload=NULL;
    HRESULT hr=ID3D12Device_CreateCommittedResource(p->owner->host,&hp,D3D12_HEAP_FLAG_NONE,&rd,
            D3D12_RESOURCE_STATE_GENERIC_READ,NULL,&IID_ID3D12Resource,(void **)&upload);
    if (FAILED(hr)) return hr;
    BYTE *mapped=NULL; D3D12_RANGE read={0,0};
    D3D12_GPU_VIRTUAL_ADDRESS va=ID3D12Resource_GetGPUVirtualAddress(upload);
    if (!va || va>UINT64_MAX-bytes) hr=E_INVALIDARG;
    else hr=ID3D12Resource_Map(upload,0,&read,(void **)&mapped);
    if (SUCCEEDED(hr)) {
        /* Bind the same header we checked, never a second read of the size field. */
        memcpy(mapped,header,sizeof header);
        if (bytes>sizeof header && !copy_in(mapped+sizeof header,(const BYTE *)source+sizeof header,
                bytes-sizeof header)) hr=E_POINTER;
        ID3D12Resource_Unmap(upload,0,NULL);
    }
    if (SUCCEEDED(hr)) hr=bvh_record_target(pipeline,BVH_COPY_UPDATE,list,dst,dst->guest_base,(UINT)bytes,out);
    if (FAILED(hr)) { ID3D12Resource_Release(upload); return hr; }
    (*out)->upload=upload;
    UINT constants[]={32,reset_bounds};
    ID3D12GraphicsCommandList_SetComputeRoot32BitConstants(list,1,2,constants,0);
    ID3D12GraphicsCommandList_SetComputeRootShaderResourceView(list,2,va);
    ID3D12GraphicsCommandList_Dispatch(list,1,1,1);
    return S_OK;
}
