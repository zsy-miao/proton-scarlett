#include <d3d12.h>
#include <wrl/client.h>
#include <array>
#include <cstdio>
#include <cstring>
using Microsoft::WRL::ComPtr;
extern "C" HRESULT WINAPI D3D12XboxCreateDevice(void *,const void *,REFIID,void **);
extern "C" ID3D12PipelineState *test_postbuild_host(IUnknown *,ID3D12RootSignature **);
extern "C" ID3D12RootSignature *xs_bvh_root_host(IUnknown *,IUnknown *);
#define SLOT(o,n,T) ((T)((*reinterpret_cast<void***>((o).Get()))[n/8]))
#define CK(x) do{if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);return 1;}}while(0)
#define HR(x) CK(SUCCEEDED(x))

struct Query {
    UINT mode, flags, last, refs, extent, delta, slots;
    UINT64 size, serialized_refs;
    UINT u1,u2;
};
static int dispatch(IUnknown *wrapped,const Query &c)
{
    ID3D12RootSignature *root=nullptr;auto ps=test_postbuild_host(wrapped,&root);CK(ps && root);
    ComPtr<ID3D12Device>d;HR(ps->GetDevice(IID_PPV_ARGS(&d)));
    using R=ComPtr<ID3D12Resource>;
    auto buffer=[&](R&r,D3D12_HEAP_TYPE type){
        D3D12_HEAP_PROPERTIES hp={};hp.Type=type;hp.CreationNodeMask=hp.VisibleNodeMask=1;
        D3D12_RESOURCE_DESC rd={};rd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;rd.Width=512;
        rd.Height=rd.DepthOrArraySize=rd.MipLevels=rd.SampleDesc.Count=1;rd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if(type==D3D12_HEAP_TYPE_DEFAULT)rd.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        return d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,type==D3D12_HEAP_TYPE_UPLOAD?
            D3D12_RESOURCE_STATE_GENERIC_READ:D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&r));};
    R seed,arena,rb,input;HR(buffer(seed,D3D12_HEAP_TYPE_UPLOAD));HR(buffer(input,D3D12_HEAP_TYPE_UPLOAD));
    HR(buffer(arena,D3D12_HEAP_TYPE_DEFAULT));HR(buffer(rb,D3D12_HEAP_TYPE_READBACK));
    std::array<UINT,128> expected;for(UINT i=0;i<expected.size();i++)expected[i]=0x5eedfea7+i;
    void*m;HR(seed->Map(0,nullptr,&m));memcpy(m,expected.data(),512);seed->Unmap(0,nullptr);
    // The SRV starts at input+64. Header at +76; header+4 crosses a 32-bit carry.
    constexpr UINT64 base=0x57fffffff0ull;UINT64 addr=base+c.delta;
    UINT k[]={UINT(addr),UINT(addr>>32),c.mode,0xa29df104},w[]={UINT(base),UINT(base>>32),c.extent};
    UINT hdr[]={c.flags,c.last,0xbadc0de,c.refs};HR(input->Map(0,nullptr,&m));memset(m,0x5a,512);
    memcpy((BYTE*)m+76,hdr,sizeof hdr);memcpy((BYTE*)m+256,k,sizeof k);input->Unmap(0,nullptr);
    D3D12_DESCRIPTOR_HEAP_DESC hd={D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,3,D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,1};
    ComPtr<ID3D12DescriptorHeap>heap;HR(d->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&heap)));
    auto cpu=heap->GetCPUDescriptorHandleForHeapStart();
    for(UINT i=0;i<3;i++){UINT stride=i?4:8;D3D12_UNORDERED_ACCESS_VIEW_DESC v={};
        v.ViewDimension=D3D12_UAV_DIMENSION_BUFFER;v.Buffer.StructureByteStride=stride;
        v.Buffer.FirstElement=64*(1+i)/stride;v.Buffer.NumElements=i?1:c.slots;
        d->CreateUnorderedAccessView(arena.Get(),nullptr,&v,cpu);cpu.ptr+=d->GetDescriptorHandleIncrementSize(hd.Type);}
    if(c.mode==1||c.mode==2) memcpy(&expected[16],&c.size,8);
    if(c.mode==2&&c.slots==2) memcpy(&expected[18],&c.serialized_refs,8);
    if(c.mode==3){expected[32]=c.u1;expected[48]=c.u2;}
    D3D12_COMMAND_QUEUE_DESC qd={};qd.Type=D3D12_COMMAND_LIST_TYPE_COMPUTE;ComPtr<ID3D12CommandQueue>q;
    HR(d->CreateCommandQueue(&qd,IID_PPV_ARGS(&q)));ComPtr<ID3D12CommandAllocator>a;ComPtr<ID3D12GraphicsCommandList>l;
    HR(d->CreateCommandAllocator(qd.Type,IID_PPV_ARGS(&a)));HR(d->CreateCommandList(1,qd.Type,a.Get(),ps,IID_PPV_ARGS(&l)));
    auto trans=[&](D3D12_RESOURCE_STATES from,D3D12_RESOURCE_STATES to){D3D12_RESOURCE_BARRIER b={};b.Transition={arena.Get(),0,from,to};l->ResourceBarrier(1,&b);};
    l->CopyResource(arena.Get(),seed.Get());trans(D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    l->SetDescriptorHeaps(1,heap.GetAddressOf());l->SetComputeRootSignature(root);
    l->SetComputeRootDescriptorTable(0,heap->GetGPUDescriptorHandleForHeapStart());l->SetComputeRootConstantBufferView(1,input->GetGPUVirtualAddress()+256);
    l->SetComputeRoot32BitConstants(2,3,w,0);l->SetComputeRootShaderResourceView(3,input->GetGPUVirtualAddress()+64);l->Dispatch(1,1,1);
    trans(D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);l->CopyResource(rb.Get(),arena.Get());HR(l->Close());
    ID3D12CommandList*lists[]={l.Get()};q->ExecuteCommandLists(1,lists);ComPtr<ID3D12Fence>f;HR(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&f)));
    HR(q->Signal(f.Get(),1));HANDLE e=CreateEventW(nullptr,FALSE,FALSE,nullptr);CK(e);HR(f->SetEventOnCompletion(1,e));
    CK(WaitForSingleObject(e,15000)==WAIT_OBJECT_0&&f->GetCompletedValue()==1);CloseHandle(e);
    HR(rb->Map(0,nullptr,&m));for(UINT i=0;i<expected.size();i++)if(((UINT*)m)[i]!=expected[i]){
        fprintf(stderr,"FAIL mode=%u extent=%u offset=%u slot=%u gpu=%08x expected=%08x\n",c.mode,c.extent,c.delta,i,((UINT*)m)[i],expected[i]);return 1;}
    rb->Unmap(0,nullptr);printf("PASS query mode=%u extent=%u offset=%u size=%llu refs=%llu u1=%u u2=%u all guards\n",
        c.mode,c.extent,c.delta,c.size,c.serialized_refs,c.u1,c.u2);return 0;
}

using Create=decltype(&D3D12XboxCreateDevice);
static int verify(Create create,bool staged)
{
    const IID boot={0x5405c344,0xd457,0x444e,{0xb4,0xdd,0x23,0x66,0xe4,0x5a,0xee,0x39}};
    DWORD params[20]={};params[0]=0x20004;params[2]=0x100000;params[10]=1;params[15]=0x20;
    ComPtr<IUnknown>dev,other,root,wrongroot,ps;HR(create(nullptr,params,boot,(void**)dev.GetAddressOf()));HR(create(nullptr,params,boot,(void**)other.GetAddressOf()));
    D3D12_ROOT_PARAMETER1 p[4]={};for(UINT i=0;i<3;i++){p[i].ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV;p[i].Descriptor.ShaderRegister=i;}
    p[3].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;p[3].Constants.Num32BitValues=4;
    D3D12_VERSIONED_ROOT_SIGNATURE_DESC sig={};sig.Version=D3D_ROOT_SIGNATURE_VERSION_1_1;sig.Desc_1_1={4,p,0,nullptr,(D3D12_ROOT_SIGNATURE_FLAGS)0x03000000};
    using Root=HRESULT(WINAPI *)(IUnknown*,UINT,const void*,SIZE_T,REFIID,void**);using Name=HRESULT(WINAPI *)(IUnknown*,const WCHAR*);
    auto makeRoot=[&](ID3DBlob*b,ComPtr<IUnknown>&r){return SLOT(dev,0x80,Root)(dev.Get(),1,b->GetBufferPointer(),b->GetBufferSize(),IID_ID3D12RootSignature,(void**)r.GetAddressOf());};
    ComPtr<ID3DBlob>b;HR(D3D12SerializeVersionedRootSignature(&sig,&b,nullptr));CK(b->GetBufferSize()==164);HR(makeRoot(b.Get(),root));
    HR(SLOT(root,0x30,Name)(root.Get(),L"XDXR PostBuildInfoQuery"));
    BYTE code[0x14a4];FILE*file=fopen("query.dxbc","rb");CK(file&&fread(code,1,sizeof code,file)==sizeof code&&fgetc(file)==EOF&&!ferror(file));fclose(file);
    CK(!memcmp(code+0x12c,(BYTE*)b->GetBufferPointer()+36,128));b.Reset();
    p[1].Descriptor.RegisterSpace=1;HR(D3D12SerializeVersionedRootSignature(&sig,&b,nullptr));CK(makeRoot(b.Get(),wrongroot)==E_NOTIMPL&&!wrongroot);b.Reset();
    p[1].Descriptor.RegisterSpace=0;p[3].Constants.Num32BitValues=10;HR(D3D12SerializeVersionedRootSignature(&sig,&b,nullptr));HR(makeRoot(b.Get(),wrongroot));
    struct Wire{IUnknown *root;const void*cs;SIZE_T size;BYTE tail[32];}w={wrongroot.Get(),code,sizeof code,{}};
    using Make=HRESULT(WINAPI *)(IUnknown*,const Wire*,UINT64,UINT64,REFIID,void**);
    auto make=[&](ComPtr<IUnknown>&o){return SLOT(o,0x1f0,Make)(o.Get(),&w,0,0,IID_ID3D12PipelineState,(void**)ps.GetAddressOf());};
    CK(make(dev)==E_INVALIDARG&&!ps);w.root=root.Get();CK(make(other)==E_INVALIDARG&&!ps);
    w.size--;CK(make(dev)==E_NOTIMPL&&!ps);w.size++;w.tail[31]=1;CK(make(dev)==E_NOTIMPL&&!ps);w.tail[31]=0;
    code[256]^=1;CK(make(dev)==E_NOTIMPL&&!ps);code[256]^=1;
    if(!GetProcAddress(GetModuleHandleW(L"d3d12core.dll"),"ScarlettBoundedPostbuildV1")){
        CK(make(dev)==E_NOTIMPL&&!ps);puts("PASS missing postbuild backend declined");return 0;}
    HR(make(dev));WCHAR wrong[32]={};CK(SLOT(ps,0x30,Name)(ps.Get(),wrong)==E_NOTIMPL);
    HR(SLOT(ps,0x30,Name)(ps.Get(),L"XDXR GetBVHCompactedSize"));printf("PASS %s PostBuildInfoQuery factory and negatives\n",staged?"staged":"linked");
    if(staged)return 0;
    auto guest=xs_bvh_root_host(dev.Get(),root.Get());ComPtr<ID3D12Device>d;HR(guest->GetDevice(IID_PPV_ARGS(&d)));
    D3D12_COMPUTE_PIPELINE_STATE_DESC pd={};pd.pRootSignature=guest;pd.CS={code,sizeof code};ComPtr<ID3D12PipelineState>bad;
    CK(FAILED(d->CreateComputePipelineState(&pd,IID_PPV_ARGS(&bad)))&&!bad);puts("PASS missing private window declined");
    const Query cases[]={
        {1,0,36,37,28,12,2,2368,0,0,0}, {2,0,36,37,28,12,2,2880,37,0,0},
        {2,0x80000000,36,37,28,12,2,2624,0,0,0}, {3,0,36,37,28,12,2,0,0,148,37},
        {2,0,36,37,27,12,2,2624,0,0,0}, {1,0,36,37,19,12,2,64,0,0,0},
        {2,0,36,37,28,13,2,320,0,0,0}, {2,0,36,37,28,12,1,2880,0,0,0},
        {1,0,0x4000000,0,28,12,2,0x100000040ull,0,0,0}, {0,0,36,37,28,12,2,0,0,0,0}
    };
    for(auto &c:cases) CK(!dispatch(ps.Get(),c));
    return 0;
}
int main()
{
    HMODULE dll=LoadLibraryW(L"d3d12_xs.dll");CK(dll);auto fn=GetProcAddress(dll,"D3D12XboxCreateDevice");
    Create create;CK(fn);memcpy(&create,&fn,sizeof create);CK(!verify(create,true));return verify(D3D12XboxCreateDevice,false);
}
