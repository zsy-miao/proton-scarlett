/* Included by test-descriptor.cpp. test-view.dxil is built from the adjacent HLSL. */
extern "C" {
struct xs_buffer_binding;
HRESULT xs_bind_view_buffer(IUnknown *,IUnknown *,ID3D12Resource *,xs_buffer_binding **);
void xs_unbind_view_buffer(xs_buffer_binding *);
HRESULT xs_resolve_view(IUnknown *,SIZE_T,UINT,ID3D12DescriptorHeap **,D3D12_GPU_DESCRIPTOR_HANDLE *);
HRESULT xs_write_view(IUnknown *,UINT,IUnknown *,const void *,SIZE_T);
}
static void test_writers(IUnknown *d,ID3D12Device *host,IUnknown *heap,SIZE_T start,UINT stride)
{
    using CB = void (WINAPI *)(IUnknown *,const void *,SIZE_T);
    using SR = void (WINAPI *)(IUnknown *,IUnknown *,const void *,SIZE_T);
    using UA = void (WINAPI *)(IUnknown *,IUnknown *,IUnknown *,const void *,SIZE_T);
    const SIZE_T raw=start+stride, null_srv=raw+stride, cb=null_srv+stride, null_uav=cb+stride;
    auto srv=SLOT(d,0x90,SR); auto uav=SLOT(d,0x98,UA);
    UINT words[10]={42,1,0x1688};
    for (UINT dim : {2,4,9,8,1}) { words[1]=dim; srv(d,nullptr,words,null_srv); }
    words[2]=0;
    for (UINT dim : {2,4,5,8,1}) { words[1]=dim; uav(d,nullptr,nullptr,words,null_uav); }
    UINT64 zero[2]={};
    SLOT(d,0x88,CB)(d,zero,cb);
    CK(((UINT64*)cb)[0]==0x10000000000000ull && ((UINT64*)cb)[1]==0x2403c00000000ull);
    words[2]=0x1688;
    srv(d,nullptr,words,cb);
    ((UINT64*)cb)[0]=0x10000000000000ull; ((UINT64*)cb)[1]=0x2403c00000000ull;
    D3D12_HEAP_PROPERTIES hp={}; hp.CreationNodeMask=hp.VisibleNodeMask=1;
    D3D12_RESOURCE_DESC buf={}; buf.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER; buf.Width=256;
    buf.Height=buf.DepthOrArraySize=buf.MipLevels=buf.SampleDesc.Count=1; buf.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ID3D12Resource *in,*out,*readback; hp.Type=D3D12_HEAP_TYPE_UPLOAD;
    HR(host->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&buf,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&in)));
    UINT *mapped;
    HR(in->Map(0,nullptr,(void **)&mapped)); mapped[0]=123; mapped[7]=0xac125bee; in->Unmap(0,nullptr);
    xs_buffer_binding *binding;
    HR(xs_bind_view_buffer(d,heap,in,&binding)); /* test identity; foreign pointers are rejected */
    UINT record[10]={39,1,0x1688,0,7,0,1,0,1,0};
    CK(FAILED(xs_write_view(d,0x90,(IUnknown*)1,record,raw)));
    record[4]=64; CK(FAILED(xs_write_view(d,0x90,heap,record,raw))); record[4]=7;
    CK(FAILED(xs_write_view(d,0x90,heap,record,raw+1)));
    words[1]=0; CK(xs_write_view(d,0x90,nullptr,words,raw)==E_NOTIMPL); words[1]=1;
    srv(d,heap,record,raw);
    xs_unbind_view_buffer(binding);
    in->Release(); /* only the written descriptor retains the host resource */
    CK(FAILED(xs_write_view(d,0x90,heap,record,raw)));
    ID3D12DescriptorHeap *desktop;
    D3D12_GPU_DESCRIPTOR_HANDLE handles[4];
    HR(xs_resolve_view(heap,raw,0x90,&desktop,&handles[0]));
    ID3D12DescriptorHeap *bank;
    HR(xs_resolve_view(heap,null_srv,0x90,&bank,&handles[1])); CK(bank==desktop);
    HR(xs_resolve_view(heap,cb,0x88,&bank,&handles[2])); CK(bank==desktop);
    HR(xs_resolve_view(heap,null_uav,0x98,&bank,&handles[3])); CK(bank==desktop);
    auto shader=CreateFileA("test-view.dxil",GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
    CK(shader!=INVALID_HANDLE_VALUE);
    BYTE code[8192]; DWORD n; CK(ReadFile(shader,code,sizeof code,&n,nullptr) && n && n<sizeof code); CloseHandle(shader);
    D3D12_DESCRIPTOR_RANGE range[4]={}; D3D12_ROOT_PARAMETER rp[5]={};
    for (UINT i=0;i<4;i++) {
        range[i].RangeType=i<2?D3D12_DESCRIPTOR_RANGE_TYPE_SRV:i==2?D3D12_DESCRIPTOR_RANGE_TYPE_CBV:D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        range[i].BaseShaderRegister=i==1||i==3?1:0; range[i].NumDescriptors=1;
        rp[i].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;rp[i].DescriptorTable={1,&range[i]};
    }
    rp[4].ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV;
    D3D12_ROOT_SIGNATURE_DESC rs={5,rp,0,nullptr,D3D12_ROOT_SIGNATURE_FLAG_NONE};
    ID3DBlob *blob; HR(D3D12SerializeRootSignature(&rs,D3D_ROOT_SIGNATURE_VERSION_1,&blob,nullptr));
    ID3D12RootSignature *sig; HR(host->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&sig))); blob->Release();
    D3D12_COMPUTE_PIPELINE_STATE_DESC pd={}; pd.pRootSignature=sig; pd.CS={code,n};
    ID3D12PipelineState *pso; HR(host->CreateComputePipelineState(&pd,IID_PPV_ARGS(&pso)));
    hp.Type=D3D12_HEAP_TYPE_DEFAULT; buf.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    HR(host->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&buf,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&out)));
    hp.Type=D3D12_HEAP_TYPE_READBACK; buf.Flags=D3D12_RESOURCE_FLAG_NONE;
    HR(host->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&buf,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback)));
    ID3D12CommandQueue *q; ID3D12CommandAllocator *a; ID3D12GraphicsCommandList *l; ID3D12Fence *f;
    D3D12_COMMAND_QUEUE_DESC qd={}; qd.Type=D3D12_COMMAND_LIST_TYPE_COMPUTE;
    HR(host->CreateCommandQueue(&qd,IID_PPV_ARGS(&q))); HR(host->CreateCommandAllocator(qd.Type,IID_PPV_ARGS(&a)));
    HR(host->CreateCommandList(0,qd.Type,a,pso,IID_PPV_ARGS(&l)));
    l->SetDescriptorHeaps(1,&desktop); l->SetComputeRootSignature(sig);
    for (UINT i=0;i<4;i++) l->SetComputeRootDescriptorTable(i,handles[i]);
    l->SetComputeRootUnorderedAccessView(4,out->GetGPUVirtualAddress()); l->Dispatch(1,1,1);
    D3D12_RESOURCE_BARRIER b={}; b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; b.Transition.pResource=out;
    b.Transition.StateBefore=D3D12_RESOURCE_STATE_UNORDERED_ACCESS; b.Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_SOURCE;
    l->ResourceBarrier(1,&b); l->CopyBufferRegion(readback,0,out,0,16); HR(l->Close());
    ID3D12CommandList *batch[]={l}; q->ExecuteCommandLists(1,batch);
    HR(host->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&f))); HR(q->Signal(f,1));
    HANDLE e=CreateEventW(nullptr,FALSE,FALSE,nullptr); CK(e); HR(f->SetEventOnCompletion(1,e)); CK(WaitForSingleObject(e,5000)==WAIT_OBJECT_0);
    HR(readback->Map(0,nullptr,(void**)&mapped));
    CK(mapped[0]==0xac125bee && !mapped[1] && !mapped[2] && !mapped[3]); readback->Unmap(0,nullptr);
    *(DWORD*)raw=0xffffffff; CK(FAILED(xs_resolve_view(heap,raw,0x90,&desktop,&handles[0])) && !desktop && !handles[0].ptr);
    CK(FAILED(xs_resolve_view(heap,cb,0x90,&desktop,&handles[0])));
    CK(FAILED(xs_resolve_view(heap,raw,0x88,&desktop,&handles[0])));
    CloseHandle(e); f->Release(); l->Release(); a->Release(); q->Release(); readback->Release(); out->Release(); pso->Release(); sig->Release();
    puts("PASS GPU descriptor writers: bounded RAW SRV after owner unregister; null SRV/inline CBV/UAV returned zero, UAV store discarded");
}
