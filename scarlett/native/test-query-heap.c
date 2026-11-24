/* White-box access to the owned heaps for host recording; no test-only DLL export. */
#define INITGUID
#include "queue/queue.c"
#include <d3dcompiler.h>

HRESULT WINAPI D3D12XboxCreateDevice(void *, const void *, REFIID, void **);
typedef HRESULT (WINAPI *CreateHeap)(IUnknown *, const void *, REFIID, void **);
#define CHECK(e) do { if (!(e)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#e); return 1; } } while (0)
#define OK(e) do { HRESULT h_=(e); if (h_!=S_OK) { fprintf(stderr,"FAIL line %d: %s = %#lx\n",__LINE__,#e,h_); return 1; } } while (0)

int main(void)
{
    const IID device_id={0x5405c344,0xd457,0x444e,{0xb4,0xdd,0x23,0x66,0xe4,0x5a,0xee,0x39}};
    DWORD params[20]={[0]=0x20004,[2]=0x100000,[10]=1,[15]=0x20};
    IUnknown *dev,*heap[4],*alias;
    ID3D12QueryHeap *h[4];
    ID3D12Device *host;
    OK(D3D12XboxCreateDevice(NULL,params,&device_id,(void **)&dev));
    CreateHeap create=(CreateHeap)((void *const *)dev->lpVtbl)[0x138/8];
    D3D12_QUERY_HEAP_DESC d={D3D12_QUERY_HEAP_TYPE_OCCLUSION,2460,1};
    void *out;
#define REJECT(desc,iid,hr) do { out=dev; CHECK(create(dev,desc,iid,&out)==(hr) && !out); } while (0)
    CHECK(create(dev,&d,&IID_ID3D12QueryHeap,NULL)==E_POINTER);
    REJECT(NULL,&IID_ID3D12QueryHeap,E_INVALIDARG);
    REJECT((void *)1,&IID_ID3D12QueryHeap,E_POINTER);
    REJECT(&d,NULL,E_INVALIDARG);
    REJECT(&d,(REFIID)1,E_POINTER);
    REJECT(&d,&IID_ID3D12Resource,E_NOINTERFACE);
    d.Count=0; REJECT(&d,&IID_ID3D12QueryHeap,E_INVALIDARG);
    d.Count=2459; REJECT(&d,&IID_ID3D12QueryHeap,E_NOTIMPL);
    d.Count=2460; d.NodeMask=2; REJECT(&d,&IID_ID3D12QueryHeap,E_NOTIMPL);
    d.NodeMask=1; d.Type=D3D12_QUERY_HEAP_TYPE_TIMESTAMP; REJECT(&d,&IID_ID3D12QueryHeap,E_NOTIMPL);
    d.Type=D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS; REJECT(&d,&IID_ID3D12QueryHeap,E_NOTIMPL);
    d.Type=D3D12_QUERY_HEAP_TYPE_OCCLUSION;
    for (unsigned i=0;i<4;++i) {
        OK(create(dev,&d,&IID_ID3D12QueryHeap,(void **)&heap[i]));
        OK(IUnknown_QueryInterface(heap[i],&IID_IUnknown,(void **)&alias)); CHECK(alias==heap[i]);
        CHECK(IUnknown_Release(alias)==1);
        OK(IUnknown_QueryInterface(heap[i],&IID_ID3D12QueryHeap,(void **)&alias)); CHECK(alias==heap[i]);
        CHECK(IUnknown_Release(alias)==1);
        CHECK(IUnknown_QueryInterface(heap[i],&IID_ID3D12CommandSignature,&out)==E_NOINTERFACE && !out);
        CHECK(heap[i]->lpVtbl==&qht.prefix);
        h[i]=CONTAINING_RECORD(heap[i],struct adapter,iface)->host.query_heap;
        CHECK((void *)h[i]!=(void *)heap[i]);
    }
    OK(ID3D12QueryHeap_GetDevice(h[0],&IID_ID3D12Device,(void **)&host));

    /* Synthetic 8x8 depth-only full-screen triangle. */
    static const char shader[]="float4 main(uint id:SV_VertexID):SV_Position {"
        "return float4(id==1 ? 3.0 : -1.0, id==2 ? 3.0 : -1.0,0.5,1.0);}";
    ID3DBlob *vs,*blob;
    OK(D3DCompile(shader,sizeof(shader)-1,NULL,NULL,NULL,"main","vs_5_0",0,0,&vs,NULL));
    D3D12_ROOT_SIGNATURE_DESC rd={0};
    ID3D12RootSignature *root;
    OK(D3D12SerializeRootSignature(&rd,D3D_ROOT_SIGNATURE_VERSION_1,&blob,NULL));
    OK(ID3D12Device_CreateRootSignature(host,0,ID3D10Blob_GetBufferPointer(blob),ID3D10Blob_GetBufferSize(blob),
        &IID_ID3D12RootSignature,(void **)&root));
    ID3D10Blob_Release(blob);
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pd={.pRootSignature=root,
        .VS={ID3D10Blob_GetBufferPointer(vs),ID3D10Blob_GetBufferSize(vs)},.SampleMask=UINT_MAX,
        .RasterizerState={.FillMode=D3D12_FILL_MODE_SOLID,.CullMode=D3D12_CULL_MODE_NONE,.DepthClipEnable=TRUE},
        .DepthStencilState={.DepthEnable=TRUE,.DepthWriteMask=D3D12_DEPTH_WRITE_MASK_ALL,.DepthFunc=D3D12_COMPARISON_FUNC_ALWAYS},
        .DSVFormat=DXGI_FORMAT_D32_FLOAT,.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,.SampleDesc={1,0}};
    ID3D12PipelineState *pso;
    const char pixel[]="uint main(float4 p:SV_Position):SV_Target{return (uint)round(ddx(p.x))+256*(uint)round(ddy(p.y));}";
    ID3DBlob *ps;
    OK(D3DCompile(pixel,sizeof pixel-1,NULL,NULL,NULL,"main","ps_5_0",0,0,&ps,NULL));
    pd.NumRenderTargets=1;pd.RTVFormats[0]=DXGI_FORMAT_R32_UINT;
    pd.BlendState.RenderTarget[0].RenderTargetWriteMask=D3D12_COLOR_WRITE_ENABLE_ALL;
    pd.PS=(D3D12_SHADER_BYTECODE){ID3D10Blob_GetBufferPointer(ps),ID3D10Blob_GetBufferSize(ps)};
    OK(ID3D12Device_CreateGraphicsPipelineState(host,&pd,&IID_ID3D12PipelineState,(void **)&pso));
    ID3D10Blob_Release(ps);
    ID3D10Blob_Release(vs);
    D3D12_HEAP_PROPERTIES hp={.Type=D3D12_HEAP_TYPE_READBACK,.CreationNodeMask=1,.VisibleNodeMask=1};
    D3D12_RESOURCE_DESC buf={.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER,.Width=512+8*256,.Height=1,
        .DepthOrArraySize=1,.MipLevels=1,.SampleDesc={1,0},.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR};
    ID3D12Resource *result;
    OK(ID3D12Device_CreateCommittedResource(host,&hp,0,&buf,D3D12_RESOURCE_STATE_COPY_DEST,NULL,&IID_ID3D12Resource,(void **)&result));
    hp.Type=D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC depth={.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D,.Width=8,.Height=8,
        .DepthOrArraySize=1,.MipLevels=1,.Format=DXGI_FORMAT_D32_FLOAT,.SampleDesc={1,0},.Flags=D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL};
    ID3D12Resource *depth_image;
    OK(ID3D12Device_CreateCommittedResource(host,&hp,0,&depth,D3D12_RESOURCE_STATE_DEPTH_WRITE,NULL,&IID_ID3D12Resource,(void **)&depth_image));
    D3D12_DESCRIPTOR_HEAP_DESC dh={.Type=D3D12_DESCRIPTOR_HEAP_TYPE_DSV,.NumDescriptors=1};
    ID3D12DescriptorHeap *views;
    OK(ID3D12Device_CreateDescriptorHeap(host,&dh,&IID_ID3D12DescriptorHeap,(void **)&views));
    D3D12_CPU_DESCRIPTOR_HANDLE dsv;
    views->lpVtbl->GetCPUDescriptorHandleForHeapStart(views,&dsv);
    ID3D12Device_CreateDepthStencilView(host,depth_image,NULL,dsv);
    ID3D12Resource *color;
    depth.Format=DXGI_FORMAT_R32_UINT;depth.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    OK(ID3D12Device_CreateCommittedResource(host,&hp,0,&depth,D3D12_RESOURCE_STATE_RENDER_TARGET,NULL,&IID_ID3D12Resource,(void**)&color));
    ID3D12DescriptorHeap *rtvs;dh.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    OK(ID3D12Device_CreateDescriptorHeap(host,&dh,&IID_ID3D12DescriptorHeap,(void**)&rtvs));
    D3D12_CPU_DESCRIPTOR_HANDLE rtv;rtvs->lpVtbl->GetCPUDescriptorHandleForHeapStart(rtvs,&rtv);
    ID3D12Device_CreateRenderTargetView(host,color,NULL,rtv);
    UINT64 *words;
    OK(ID3D12Resource_Map(result,0,NULL,(void **)&words));
    memset(words,0xff,buf.Width);
    ID3D12Resource_Unmap(result,0,NULL);
    D3D12_COMMAND_QUEUE_DESC qd={.Type=D3D12_COMMAND_LIST_TYPE_DIRECT};
    ID3D12CommandQueue *q;
    IUnknown *a,*list;
    ID3D12GraphicsCommandList *l;
    ID3D12Fence *f;
    OK(ID3D12Device_CreateCommandQueue(host,&qd,&IID_ID3D12CommandQueue,(void **)&q));
    OK(xs_allocator(host,qd.Type,&a));
    OK(xs_device_list(host,qd.Type,a,TRUE,dev,&list));
    OK(sl_close(list));
    D3D12_SHADING_RATE_COMBINER pair[2]={0,0};
    CHECK(record_pool_rate(sl(list),0,pair)==E_INVALIDARG);
    CHECK(!can_set_topology(sl(list),4));
    OK(sl_reset(list,a,NULL,0));
    CHECK(!can_set_topology(sl(list),5));
    const struct direct_prefix *prefix=(const void*)list;
    MEMORY_BASIC_INFORMATION mb;
    CHECK(!prefix->put && !prefix->limit && VirtualQuery(prefix,&mb,sizeof mb) && mb.Protect==PAGE_READONLY);
    CHECK(VirtualQuery(prefix+1,&mb,sizeof mb) && mb.Protect==PAGE_READONLY);
    l=(ID3D12GraphicsCommandList*)sl(list)->host;
    ID3D12GraphicsCommandList_SetPipelineState(l,pso);
    OK(ID3D12Device_CreateFence(host,0,0,&IID_ID3D12Fence,(void **)&f));
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(l,root);
    ID3D12GraphicsCommandList_OMSetRenderTargets(l,1,&rtv,FALSE,&dsv);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(l,D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    if(prefix->put>=prefix->limit) ((void(WINAPI*)(IUnknown*,UINT))(*(void***)list)[0xa0/8])(list,4);
    D3D12_VIEWPORT viewport={0,0,8,8,0,1};
    ID3D12GraphicsCommandList_RSSetViewports(l,1,&viewport);
    D3D12_FEATURE_DATA_D3D12_OPTIONS6 opts={0};
    OK(ID3D12Device_CheckFeatureSupport(host,D3D12_FEATURE_D3D12_OPTIONS6,&opts,sizeof opts));
    CHECK(opts.VariableShadingRateTier>=D3D12_VARIABLE_SHADING_RATE_TIER_1);
    ID3D12GraphicsCommandList7_RSSetShadingRate(sl(list)->host,D3D12_SHADING_RATE_2X2,pair);
    CHECK(record_pool_rate(sl(list),5,pair)==E_INVALIDARG && record_pool_rate(sl(list),0,NULL)==E_POINTER);
    for(UINT j=0;j<2;j++){pair[j]=D3D12_SHADING_RATE_COMBINER_OVERRIDE;CHECK(record_pool_rate(sl(list),0,pair)==E_NOTIMPL);pair[j]=0;}
    for (int i=0;i<4;++i) {
        if(i==1)pool_vtbl.rate(list,0,pair);
        D3D12_RECT scissor={0,0,8-i*2,8};
        ID3D12GraphicsCommandList_RSSetScissorRects(l,1,&scissor);
        ID3D12GraphicsCommandList_BeginQuery(l,h[i],D3D12_QUERY_TYPE_OCCLUSION,2459);
        /* Last three vertices coincide. A strip would rasterize the first triangle twice. */
        ID3D12GraphicsCommandList_DrawInstanced(l,6,1,0,0);
        ID3D12GraphicsCommandList_EndQuery(l,h[i],D3D12_QUERY_TYPE_OCCLUSION,2459);
    }
    for (int i=0;i<4;++i)
        ID3D12GraphicsCommandList_ResolveQueryData(l,h[i],D3D12_QUERY_TYPE_OCCLUSION,2459,1,result,(i+1)*8);
    D3D12_RESOURCE_BARRIER b={.Transition={color,0,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE}};
    ID3D12GraphicsCommandList_ResourceBarrier(l,1,&b);
    D3D12_TEXTURE_COPY_LOCATION from={.pResource=color},dst={.pResource=result,.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT,
        .PlacedFootprint={512,{DXGI_FORMAT_R32_UINT,8,8,1,256}}};
    ID3D12GraphicsCommandList_CopyTextureRegion(l,&dst,0,0,0,&from,NULL);
    OK(sl_close(list));
    ID3D12CommandList *submit=(ID3D12CommandList *)l;
    ID3D12CommandQueue_ExecuteCommandLists(q,1,&submit);
    OK(ID3D12CommandQueue_Signal(q,f,1));
    HANDLE e=CreateEventW(NULL,FALSE,FALSE,NULL); CHECK(e);
    OK(ID3D12Fence_SetEventOnCompletion(f,1,e)); CHECK(WaitForSingleObject(e,10000)==WAIT_OBJECT_0);
    OK(ID3D12Resource_Map(result,0,NULL,(void **)&words));
    printf("visible samples at index 2459: %llu %llu %llu %llu\n",words[1],words[2],words[3],words[4]);
    CHECK(words[1]==64 && words[2]==48 && words[3]==32 && words[4]==16);
    CHECK(words[0]==UINT64_MAX && words[5]==UINT64_MAX);
    for(UINT y=0;y<8;y++)for(UINT x=0;x<8;x++)
        CHECK(*(UINT*)((BYTE*)words+512+y*256+4*x)==(x<6?0x101u:0x202u));
    printf("PASS +3f0 tier=%u: 16 coarse and 48 fine pixels in order after fence\n",opts.VariableShadingRateTier);
    ID3D12Resource_Unmap(result,0,NULL);
    CloseHandle(e); ID3D12Fence_Release(f); IUnknown_Release(list);
    IUnknown_Release(a); ID3D12CommandQueue_Release(q);
    ID3D12PipelineState_Release(pso); ID3D12RootSignature_Release(root);
    ID3D12Resource_Release(result);
    ID3D12Resource_Release(depth_image); ID3D12DescriptorHeap_Release(views);
    ID3D12Resource_Release(color);ID3D12DescriptorHeap_Release(rtvs);
    for (unsigned i=0;i<4;++i) CHECK(!IUnknown_Release(heap[i]));
    CHECK(!IUnknown_Release(dev));
    CHECK(!ID3D12Device_Release(host));
    puts("PASS device+0x138: four independent 2460-entry occlusion heaps, guarded GPU results and native lifetime");
    return 0;
}
