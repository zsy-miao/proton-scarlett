#include <d3d12.h>
#include <wrl/client.h>

#include <cstdio>
#include <cstring>
#include <vector>
using Microsoft::WRL::ComPtr;
extern "C" HRESULT WINAPI D3D12XboxCreateDevice(void *,const void *,REFIID,void **);
extern "C" ID3D12RootSignature *xs_bvh_root_host(IUnknown *,IUnknown *);
extern "C" ID3D12PipelineState *test_hierarchy_host(IUnknown *,ID3D12RootSignature **);
#define SLOT(o,n,T) ((T)((*reinterpret_cast<void***>((o).Get()))[n/8]))
#define CK(x) do{if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);return 1;}}while(0)
#define HR(x) CK(SUCCEEDED(x))
using R=ComPtr<ID3D12Resource>;

// Build the ordered radix tree top-down. Equal keys are ordered by leaf index.
static void tree(const std::vector<UINT64>&keys,UINT lo,UINT hi,UINT node,UINT *parent,UINT *children,UINT leaves)
{
    bool tie=keys[lo]==keys[hi];
    auto key=[&](UINT i)->UINT64{return tie?i:keys[i];};
    UINT64 bit=UINT64(1)<<(63-__builtin_clzll(key(lo)^key(hi)));
    UINT mid=lo; while (!(key(mid+1)&bit)) ++mid;
    UINT left=mid==lo?leaves-1+lo:mid, right=mid+1==hi?leaves-1+hi:mid+1;
    children[2*node]=left;children[2*node+1]=right;parent[left]=parent[right]=node;
    if(lo<mid)tree(keys,lo,mid,left,parent,children,leaves);
    if(mid+1<hi)tree(keys,mid+1,hi,right,parent,children,leaves);
}

static int dispatch(IUnknown *wrapped,bool wide,UINT n,UINT selector,UINT window,UINT live)
{
    ID3D12RootSignature *root=nullptr; auto ps=test_hierarchy_host(wrapped,&root); CK(ps && root);
    ComPtr<ID3D12Device>d; HR(ps->GetDevice(IID_PPV_ARGS(&d)));
    const UINT stride[]={wide?8u:4u,4,8},section=1024,total=5*section;
    auto word=[&](UINT u,UINT elem){return ((1+u)*section+elem*stride[u])/4;};
    std::vector<UINT> seed(total/4); for(UINT i=0;i<seed.size();i++) seed[i]=0x719304b3+17*i;
    std::vector<UINT64> keys(n);
    for(UINT i=0;i<n;i++) {
        UINT64 bucket=i/7,sub=(i%7)/2;
        keys[i]=(bucket<<(wide?48:24)) + sub*17;
        memcpy(&seed[word(0,i)],&keys[i],stride[0]);
    }
    UINT header[64]={};header[3]=selector==~0u?n:5;header[16]=2;header[17]=7;header[19]=0x4182;
    if(selector!=~0u)header[20+7*(selector==~1u?2:selector)]=selector==~1u?n-4:n;
    auto expected=seed; UINT *parent=&expected[word(1,0)],*children=&expected[word(2,0)];
    if(live)parent[0]=0;
    if(live==1){parent[1]=0;children[0]=1;children[1]=children[2]=children[3]=~0u;}
    if(live>1)tree(keys,0,live-1,0,parent,children,live);
    auto buffer=[&](R&r,UINT bytes,D3D12_HEAP_TYPE type){
        D3D12_HEAP_PROPERTIES hp={};hp.Type=type;hp.CreationNodeMask=hp.VisibleNodeMask=1; D3D12_RESOURCE_DESC rd={};
        rd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;rd.Width=bytes;rd.Height=rd.DepthOrArraySize=rd.MipLevels=rd.SampleDesc.Count=1;
        rd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;if(type==D3D12_HEAP_TYPE_DEFAULT)rd.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        return d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,type==D3D12_HEAP_TYPE_UPLOAD?D3D12_RESOURCE_STATE_GENERIC_READ:D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&r));};
    R upload,arena,rb,hdr;HR(buffer(upload,total,D3D12_HEAP_TYPE_UPLOAD));HR(buffer(arena,total,D3D12_HEAP_TYPE_DEFAULT));
    HR(buffer(rb,total,D3D12_HEAP_TYPE_READBACK));HR(buffer(hdr,512,D3D12_HEAP_TYPE_UPLOAD));void*m;
    HR(upload->Map(0,nullptr,&m));memcpy(m,seed.data(),total);upload->Unmap(0,nullptr);
    UINT k[]={0xfffffff8,0x35,selector},w[]={k[0],k[1],window};
    HR(hdr->Map(0,nullptr,&m));memcpy(m,header,sizeof header);memcpy((BYTE*)m+256,k,sizeof k);hdr->Unmap(0,nullptr);
    D3D12_DESCRIPTOR_HEAP_DESC hd={D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,3,D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,1};
    ComPtr<ID3D12DescriptorHeap>heap; HR(d->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&heap)));auto cpu=heap->GetCPUDescriptorHandleForHeapStart();
    for(UINT i=0;i<3;i++){D3D12_UNORDERED_ACCESS_VIEW_DESC v={};v.ViewDimension=D3D12_UAV_DIMENSION_BUFFER;
        v.Buffer.FirstElement=word(i,0)*4/stride[i];v.Buffer.NumElements=(i==0?48u:96u);v.Buffer.StructureByteStride=stride[i];
        d->CreateUnorderedAccessView(arena.Get(),nullptr,&v,cpu);cpu.ptr+=d->GetDescriptorHandleIncrementSize(hd.Type);}
    D3D12_COMMAND_QUEUE_DESC qd={}; qd.Type=D3D12_COMMAND_LIST_TYPE_COMPUTE;ComPtr<ID3D12CommandQueue>q;HR(d->CreateCommandQueue(&qd,IID_PPV_ARGS(&q)));
    ComPtr<ID3D12CommandAllocator>al;ComPtr<ID3D12GraphicsCommandList>l;HR(d->CreateCommandAllocator(qd.Type,IID_PPV_ARGS(&al)));
    HR(d->CreateCommandList(1,qd.Type,al.Get(),ps,IID_PPV_ARGS(&l)));l->CopyResource(arena.Get(),upload.Get());
    auto trans=[&](D3D12_RESOURCE_STATES from,D3D12_RESOURCE_STATES to){D3D12_RESOURCE_BARRIER b={};b.Transition={arena.Get(),0,from,to};l->ResourceBarrier(1,&b);};
    trans(D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);l->SetDescriptorHeaps(1,heap.GetAddressOf());
    l->SetComputeRootSignature(root);l->SetComputeRootDescriptorTable(0,heap->GetGPUDescriptorHandleForHeapStart());
    l->SetComputeRootConstantBufferView(1,hdr->GetGPUVirtualAddress()+256);l->SetComputeRoot32BitConstants(2,3,w,0);l->SetComputeRootShaderResourceView(3,hdr->GetGPUVirtualAddress());
    l->Dispatch(2,1,1); trans(D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);l->CopyResource(rb.Get(),arena.Get());HR(l->Close());
    ID3D12CommandList*lists[]={l.Get()};q->ExecuteCommandLists(1,lists);ComPtr<ID3D12Fence>f;HR(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&f)));
    HANDLE e=CreateEventW(nullptr,FALSE,FALSE,nullptr);CK(e);HR(q->Signal(f.Get(),1));HR(f->SetEventOnCompletion(1,e));CK(WaitForSingleObject(e,15000)==WAIT_OBJECT_0 && f->GetCompletedValue()==1);CloseHandle(e);
    HR(rb->Map(0,nullptr,&m));for(UINT i=0;i<expected.size();i++) if(((UINT*)m)[i]!=expected[i]){
        fprintf(stderr,"FAIL hierarchy %u count=%u selector=%x window=%u word %u: %08x != %08x\n",wide,n,selector,window,i,((UINT*)m)[i],expected[i]);return 1;}
    rb->Unmap(0,nullptr);printf("PASS hierarchy %u count=%u selector=%x window=%u live=%u output and guards\n",wide,n,selector,window,live);return 0;
}

using Create=decltype(&D3D12XboxCreateDevice);
static int verify(Create create,bool staged){
    const IID boot={0x5405c344,0xd457,0x444e,{0xb4,0xdd,0x23,0x66,0xe4,0x5a,0xee,0x39}};
    DWORD params[20]={};params[0]=0x20004;params[2]=0x100000;params[10]=1;params[15]=0x20;
    ComPtr<IUnknown>dev,other,root,unrelated,ps;
    HR(create(nullptr,params,boot,(void**)dev.GetAddressOf()));HR(create(nullptr,params,boot,(void**)other.GetAddressOf()));
    D3D12_ROOT_PARAMETER1 p[4]={};for(UINT i=0;i<3;i++){p[i].ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV;p[i].Descriptor.ShaderRegister=(i+1)%3;}
    p[3].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;p[3].Constants.Num32BitValues=3;
    D3D12_VERSIONED_ROOT_SIGNATURE_DESC sig={};sig.Version=D3D_ROOT_SIGNATURE_VERSION_1_1;sig.Desc_1_1={4,p,0,nullptr,(D3D12_ROOT_SIGNATURE_FLAGS)0x03000000};
    using Root=HRESULT(WINAPI *)(IUnknown*,UINT,const void*,SIZE_T,REFIID,void**);auto getroot=[&](ID3DBlob*b,ComPtr<IUnknown>&r){return SLOT(dev,0x80,Root)(dev.Get(),1,b->GetBufferPointer(),b->GetBufferSize(),IID_ID3D12RootSignature,(void**)r.GetAddressOf());};
    ComPtr<ID3DBlob>blob,b;HR(D3D12SerializeVersionedRootSignature(&sig,&blob,nullptr));CK(blob->GetBufferSize()==164);HR(getroot(blob.Get(),root));
    using Name=HRESULT(WINAPI *)(IUnknown*,const WCHAR*);HR(SLOT(root,0x30,Name)(root.Get(),L"XDXR ConstructHierarchyPass"));
    p[0].Descriptor.ShaderRegister=0; p[1].Descriptor.ShaderRegister=1; p[2].Descriptor.ShaderRegister=2;HR(D3D12SerializeVersionedRootSignature(&sig,&b,nullptr));CK(getroot(b.Get(),unrelated)==E_NOTIMPL&&!unrelated);b.Reset();
    // LoadInstancesPass is a valid root of the same serialized size.
    p[3].Constants.Num32BitValues=10;HR(D3D12SerializeVersionedRootSignature(&sig,&b,nullptr));HR(getroot(b.Get(),unrelated));
    bool capable=GetProcAddress(GetModuleHandleW(L"d3d12core.dll"),"ScarlettBoundedHierarchyV1")!=nullptr;
    for(int z=0;z<2;z++){
        std::vector<BYTE> code(z?0x2094:0x1e68);FILE*f=fopen(z?"split1.dxbc":"split0.dxbc","rb");CK(f && fread(code.data(),1,code.size(),f)==code.size() && fgetc(f)==EOF && !ferror(f));fclose(f);
        CK(!memcmp(code.data()+0x12c,(BYTE*)blob->GetBufferPointer()+36,128));
        struct Wire{IUnknown*root;const BYTE*cs;SIZE_T size;BYTE tail[32];}w={unrelated.Get(),code.data(),code.size(),{}};
        using Make=HRESULT(WINAPI *)(IUnknown*,const Wire*,UINT64,UINT64,REFIID,void**);
        auto make=[&](ComPtr<IUnknown>&owner){return SLOT(owner,0x1f0,Make)(owner.Get(),&w,0,0,IID_ID3D12PipelineState,(void**)ps.GetAddressOf());};
        CK(make(dev)==E_INVALIDARG&&!ps);w.root=root.Get();CK(make(other)==E_INVALIDARG&&!ps);
        if(z){ // BitonicInnerSort 1 is another registered 0x2094-byte PSO.
            auto sibling=code;FILE *f=fopen("inner64.dxbc","rb");CK(f&&fread(sibling.data(),1,sibling.size(),f)==sibling.size()&&fgetc(f)==EOF&&!ferror(f));fclose(f);
            w.cs=sibling.data();CK(make(dev)==E_INVALIDARG&&!ps);w.cs=code.data();
        }
        code.back()^=1;CK(make(dev)==E_NOTIMPL&&!ps);code.back()^=1;w.tail[31]=1;CK(make(dev)==E_NOTIMPL&&!ps);w.tail[31]=0;
        w.size--;CK(make(dev)==E_NOTIMPL&&!ps);w.size++;
        if(!capable){CK(make(dev)==E_NOTIMPL&&!ps);continue;}
        HR(make(dev)); const WCHAR *names[]={L"XDXR BuildSplits 0",L"XDXR BuildSplits 1"};
        CK(SLOT(ps,0x30,Name)(ps.Get(),names[1-z])==E_NOTIMPL); HR(SLOT(ps,0x30,Name)(ps.Get(),names[z]));
        printf("PASS %s BuildSplits %d factory exact root/digest/owner/name\n",staged?"staged":"linked",z);
        if(staged){ps.Reset();continue;}
        auto guest=xs_bvh_root_host(dev.Get(),root.Get());CK(guest);ComPtr<ID3D12Device>d;HR(guest->GetDevice(IID_PPV_ARGS(&d)));
        D3D12_COMPUTE_PIPELINE_STATE_DESC pd={};pd.pRootSignature=guest;pd.CS={code.data(),code.size()};ComPtr<ID3D12PipelineState>bad;
        CK(FAILED(d->CreateComputePipelineState(&pd,IID_PPV_ARGS(&bad)))&&!bad);
        puts("PASS exact guest-only host root rejected");
        CK(!dispatch(ps.Get(),z,0,~0u,256,0)); CK(!dispatch(ps.Get(),z,1,~0u,16,1));
        CK(!dispatch(ps.Get(),z,37,~0u,16,37)); CK(!dispatch(ps.Get(),z,37,~0u,15,0));
        CK(!dispatch(ps.Get(),z,13,1,112,13)); CK(!dispatch(ps.Get(),z,13,1,111,0));
        CK(!dispatch(ps.Get(),z,37,~1u,256,37));
        ps.Reset();
    }
    if(!staged)puts(capable?"PASS both exact hierarchy factories, owners and root schemas":"PASS both exact hierarchy factories declined without backend");
    return 0;
}
int main(){
    HMODULE dll=LoadLibraryW(L"d3d12_xs.dll");CK(dll);FARPROC fn=GetProcAddress(dll,"D3D12XboxCreateDevice");Create create;
    CK(fn);memcpy(&create,&fn,sizeof create);CK(!verify(create,true));return verify(D3D12XboxCreateDevice,false);
}
