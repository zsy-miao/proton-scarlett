// Included by the staged test-descriptor build, on its live WB placement.
#include <wrl/client.h>
extern "C" HRESULT xs_write_address_srv(IUnknown *,const void *,const void *,SIZE_T);
static void test_pool_srv(IUnknown *d,ID3D12Device *host,IUnknown *heap,SIZE_T slot)
{
    using Microsoft::WRL::ComPtr;
    auto reserve=(void *(WINAPI *)(void *,SIZE_T,DWORD,UINT64,DWORD))(void *)
        GetProcAddress(GetModuleHandleW(L"xmem.dll"),"XMemVirtualAlloc");
    constexpr UINT pool=0x89700000,n=0xc00;
    static_assert(DXGI_FORMAT_R16_UINT==0x39 && DXGI_FORMAT_R16_TYPELESS==0x35);
    CK(reserve);
    auto *p=(WORD *)reserve(nullptr,pool,0x20003000,0x283,0x2404); CK(p);
    DWORD rd[]={1,0x7d9d1efa,0,0,0x1800,0,1,0x10001,0,1,0,1,0};
    DWORD w[16]={DXGI_FORMAT_R16_UINT,1,0x1688,0,0,0,n};
    SIZE_T va=(SIZE_T)p; memcpy(w+10,&va,8);
    *(UINT64 *)slot=23; *(UINT64 *)(slot+16)=0x942613ad;
    auto attempt=[&]{return xs_write_address_srv(d,rd,w,slot);};
#ifdef TEST_POOL_BASELINE
    CK(attempt()==E_NOTIMPL && *(UINT64 *)slot==23);
    puts("PASS baseline: original +230 R16_UINT tuple rejected E_NOTIMPL before touching WB slot");
    CK(VirtualFree(p,0,MEM_RELEASE)); return;
#endif
    w[0]=DXGI_FORMAT_R16_TYPELESS; CK(FAILED(attempt())); w[0]=DXGI_FORMAT_R16_UINT;
    w[6]*=2; CK(FAILED(attempt())); w[6]=n;
    w[8]=1; CK(FAILED(attempt())); w[8]=0;
    w[10]+=16; CK(FAILED(attempt())); w[10]-=16;
    rd[6]=2; CK(FAILED(attempt())); rd[6]=1;
    CK(FAILED(xs_write_address_srv(d,rd,w,slot+1)) && *(UINT64 *)slot==23);
    UINT cpu_desc[]={0,0xc38,0,1}; IUnknown *cpu;
    HR(SLOT(d,0x70,Make)(d,cpu_desc,IID_ID3D12DescriptorHeap,(void **)&cpu));
    CK(FAILED(xs_write_address_srv(d,rd,w,SLOT(cpu,0x48,Start)(cpu)))); cpu->Release();
    using Writer=void(WINAPI *)(IUnknown *,const void *,const void *,SIZE_T);
    SLOT(d,0x230,Writer)(d,rd,w,slot);
    CK(*(UINT64 *)(slot+16)==0x942613ad);
    ID3D12DescriptorHeap *bank; D3D12_GPU_DESCRIPTOR_HANDLE handle;
    HR(xs_resolve_view(heap,slot,0x90,&bank,&handle));
    constexpr UINT smaller=0x17a, padded=0x180;
    auto window=[&](SIZE_T offset,UINT width,UINT first,UINT count) {
        SIZE_T address=va+offset; memcpy(w+10,&address,8);
        rd[4]=width; w[4]=first; w[6]=count;
    };
    window(2*n,2*padded,0,smaller);
    SLOT(d,0x230,Writer)(d,rd,w,slot+32);
    D3D12_GPU_DESCRIPTOR_HANDLE interior,last;
    HR(xs_resolve_view(heap,slot+32,0x90,&bank,&interior));
    UINT64 stamp=*(UINT64 *)slot;
    window(pool-256,512,0,1); CK(FAILED(attempt())); // count fits, padded resource doesn't
    window(pool-256,256,127,2); CK(FAILED(attempt()));
    window(2*n,2*padded,0,~0u); CK(FAILED(attempt()));
    w[6]=0; CK(FAILED(attempt())); w[6]=1; w[5]=~0u; CK(FAILED(attempt())); w[5]=0;
    CK(*(UINT64 *)slot==stamp);
    window(pool-256,256,127,1);
    SLOT(d,0x230,Writer)(d,rd,w,slot+64);
    HR(xs_resolve_view(heap,slot+64,0x90,&bank,&last));
    D3D12_DESCRIPTOR_RANGE range={D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,0,0,0};
    D3D12_ROOT_PARAMETER rp[2]={}; rp[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rp[0].DescriptorTable={1,&range}; rp[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV;
    D3D12_ROOT_SIGNATURE_DESC rs={2,rp,0,nullptr,D3D12_ROOT_SIGNATURE_FLAG_NONE};
    ComPtr<ID3DBlob> blob; HR(D3D12SerializeRootSignature(&rs,D3D_ROOT_SIGNATURE_VERSION_1,&blob,nullptr));
    ComPtr<ID3D12RootSignature> sig; HR(host->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&sig)));
    FILE *f=std::fopen("pool-srv.dxil","rb"); BYTE code[8192]; CK(f);
    size_t bytes=std::fread(code,1,sizeof code,f); CK(bytes && std::feof(f)); std::fclose(f);
    D3D12_COMPUTE_PIPELINE_STATE_DESC pd={}; pd.pRootSignature=sig.Get(); pd.CS={code,bytes};
    ComPtr<ID3D12PipelineState> pso; HR(host->CreateComputePipelineState(&pd,IID_PPV_ARGS(&pso)));
    D3D12_RESOURCE_DESC b={}; b.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER; b.Width=4*(n+padded+64);
    b.Height=b.DepthOrArraySize=b.MipLevels=b.SampleDesc.Count=1; b.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    D3D12_HEAP_PROPERTIES hp={}; hp.CreationNodeMask=hp.VisibleNodeMask=1; hp.Type=D3D12_HEAP_TYPE_DEFAULT;
    b.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    ComPtr<ID3D12Resource> out,rb;
    HR(host->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&b,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&out)));
    hp.Type=D3D12_HEAP_TYPE_READBACK; b.Flags=D3D12_RESOURCE_FLAG_NONE;
    HR(host->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&b,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&rb)));
    D3D12_COMMAND_QUEUE_DESC qd={}; qd.Type=D3D12_COMMAND_LIST_TYPE_COMPUTE;
    ComPtr<ID3D12CommandQueue> q; ComPtr<ID3D12CommandAllocator> a; ComPtr<ID3D12GraphicsCommandList> l;
    HR(host->CreateCommandQueue(&qd,IID_PPV_ARGS(&q))); HR(host->CreateCommandAllocator(qd.Type,IID_PPV_ARGS(&a)));
    HR(host->CreateCommandList(0,qd.Type,a.Get(),pso.Get(),IID_PPV_ARGS(&l)));
    l->SetDescriptorHeaps(1,&bank); l->SetComputeRootSignature(sig.Get()); l->SetComputeRootDescriptorTable(0,handle);
    l->SetComputeRootUnorderedAccessView(1,out->GetGPUVirtualAddress()); l->Dispatch(n/64,1,1);
    l->SetComputeRootDescriptorTable(0,interior);
    l->SetComputeRootUnorderedAccessView(1,out->GetGPUVirtualAddress()+4*n); l->Dispatch(padded/64,1,1);
    l->SetComputeRootDescriptorTable(0,last);
    l->SetComputeRootUnorderedAccessView(1,out->GetGPUVirtualAddress()+4*(n+padded)); l->Dispatch(1,1,1);
    D3D12_RESOURCE_BARRIER bar={}; bar.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    bar.Transition={out.Get(),0,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE};
    l->ResourceBarrier(1,&bar); l->CopyBufferRegion(rb.Get(),0,out.Get(),0,b.Width); HR(l->Close());
    ComPtr<ID3D12Fence> fence; HR(host->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));
    HR(q->Wait(fence.Get(),1)); ID3D12CommandList *batch[]={l.Get()}; q->ExecuteCommandLists(1,batch);
    HR(q->Signal(fence.Get(),2));
    for (UINT i=0;i<n+padded;i++) p[i]=(WORD)(0x8031u+199u*i);
    p[pool/2-1]=0xf935;
    MemoryBarrier(); CK(VirtualFree(p,0,MEM_RELEASE));
    CK(VirtualAlloc(p,65536,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE)==p);
    memset(p,0xda,2*(n+padded)); window(2*n,2*padded,0,smaller); CK(FAILED(attempt())); HR(fence->Signal(1));
    HANDLE ev=CreateEventW(nullptr,FALSE,FALSE,nullptr); CK(ev); HR(fence->SetEventOnCompletion(2,ev));
    CK(WaitForSingleObject(ev,15000)==WAIT_OBJECT_0); CloseHandle(ev); HR(host->GetDeviceRemovedReason());
    UINT *m; HR(rb->Map(0,nullptr,(void **)&m));
    for (UINT i=0;i<n+padded;i++) CK(m[i]==(i<n+smaller?(WORD)(0x8031u+199u*i):0) && p[i]==0xdada);
    for (UINT i=0;i<64;i++) CK(m[n+padded+i]==(i?0u:0xf935u));
    rb->Unmap(0,nullptr); CK(VirtualFree(p,0,MEM_RELEASE));
    puts("PASS +230: neighboring WC R16 views returned 3072 + 378 original elements and 6 padding zeros; final pool element selected by FirstElement, 63 OOB zeros; stale/clipped/malformed windows rejected");
}
