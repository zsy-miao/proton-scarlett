#include <d3d12.h>
#include <wrl/client.h>
#include <cstdio>
#include <vector>
#include <cstring>
using Microsoft::WRL::ComPtr;
#define CK(x) do {if(!(x)) {printf("FAIL %d %s\n",__LINE__,#x);return 1;}} while(0)
#define HR(x) CK(SUCCEEDED(x))
using R=ComPtr<ID3D12Resource>;
extern "C" HRESULT WINAPI D3D12XboxCreateDevice(void *,const void *,REFIID,void **);
extern "C" ID3D12PipelineState *test_bvh_radix_host(IUnknown *, ID3D12RootSignature **);
#define SLOT(o,n,T) ((T)((*reinterpret_cast<void***>((o).Get()))[(n)/8]))
int main() {
    const IID boot={0x5405c344,0xd457,0x444e,{0xb4,0xdd,0x23,0x66,0xe4,0x5a,0xee,0x39}};
    DWORD params[20]={};params[0]=0x20004;params[2]=0x100000;params[10]=1;params[15]=0x20;
    ComPtr<IUnknown> dev,other,prefix,utility;
    HR(D3D12XboxCreateDevice(nullptr,params,boot,(void**)dev.GetAddressOf()));
    HR(D3D12XboxCreateDevice(nullptr,params,boot,(void**)other.GetAddressOf()));
    using SetName=HRESULT(WINAPI *)(IUnknown *,const WCHAR *);
    for(UINT schema=0;schema<2;schema++) {
        D3D12_ROOT_PARAMETER1 p[3]={};
        p[0].ParameterType=schema?D3D12_ROOT_PARAMETER_TYPE_UAV:D3D12_ROOT_PARAMETER_TYPE_SRV;
        p[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV;p[1].Descriptor.ShaderRegister=schema;
        p[2].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;p[2].Constants.Num32BitValues=schema?1:16;
        D3D12_VERSIONED_ROOT_SIGNATURE_DESC desc={};desc.Version=D3D_ROOT_SIGNATURE_VERSION_1_1;
        desc.Desc_1_1={3,p,0,nullptr,(D3D12_ROOT_SIGNATURE_FLAGS)0x03000000};
        ComPtr<ID3DBlob>b;HR(D3D12SerializeVersionedRootSignature(&desc,&b,nullptr));CK(b->GetBufferSize()==140);
        auto &root=schema?prefix:utility;
        using Make=HRESULT(WINAPI *)(IUnknown *,UINT,const void *,SIZE_T,REFIID,void **);
        auto make=[&]{return SLOT(dev,0x80,Make)(dev.Get(),1,b->GetBufferPointer(),b->GetBufferSize(),IID_ID3D12RootSignature,(void**)root.GetAddressOf());};
        auto bytes=(BYTE*)b->GetBufferPointer();bytes[139]^=1;CK(make()==E_NOTIMPL&&!root);bytes[139]^=1;
        HR(make());HR(SLOT(root,0x30,SetName)(root.Get(),schema?L"XDXR RadixSortPrefixSum":L"XDXR UtilityShaders"));
        if(schema)CK(SLOT(root,0x30,SetName)(root.Get(),L"XDXR RadixSortCountAndAlloc")==E_NOTIMPL);
    }
    const size_t sizes[]={0x1360,0x1380,0x13d0,0x1454,0x1644,0x1644,0x16cc};
    using Make=HRESULT(WINAPI *)(IUnknown *,const void *,UINT64,UINT64,REFIID,void **);
    struct Wire {IUnknown *root;const void *code;SIZE_T size;BYTE tail[32];};
    {
        BYTE utility_cs[0x1410];FILE *in=fopen("private-builder.bin","rb");CK(in);
        CK(fread(utility_cs,1,sizeof utility_cs,in)==sizeof utility_cs&&fgetc(in)==EOF&&!ferror(in));fclose(in);
        Wire w={prefix.Get(),utility_cs,sizeof utility_cs,{}};void *bad=dev.Get();
        CK(SLOT(dev,0x1f0,Make)(dev.Get(),&w,0,0,IID_ID3D12PipelineState,&bad)==E_INVALIDARG&&!bad);
    }
    HANDLE ev=CreateEventW(nullptr,FALSE,FALSE,nullptr); CK(ev);
    for(UINT n=64,k=0;n<=4096;n*=2,k++){
        char name[64];snprintf(name,sizeof name,"private-prefix%u.bin",n);
        FILE* file=fopen(name,"rb");CK(file);std::vector<BYTE> shader(sizes[k]);
        CK(fread(shader.data(),1,shader.size(),file)==shader.size() && fgetc(file)==EOF && !ferror(file));fclose(file);
        Wire wire={prefix.Get(),shader.data(),shader.size(),{}};
        ComPtr<IUnknown> wrapper;
        auto make=[&](const ComPtr<IUnknown>&owner){return SLOT(owner,0x1f0,Make)(owner.Get(),&wire,0,0,IID_ID3D12PipelineState,(void**)wrapper.GetAddressOf());};
        CK(make(other)==E_INVALIDARG&&!wrapper);wire.root=utility.Get();CK(make(dev)==E_INVALIDARG&&!wrapper);wire.root=prefix.Get();
        shader.back()^=1;CK(make(dev)==E_NOTIMPL&&!wrapper);shader.back()^=1;
        --wire.size;CK(make(dev)==E_NOTIMPL&&!wrapper);++wire.size;
        wire.tail[31]=1;CK(make(dev)==E_NOTIMPL&&!wrapper);wire.tail[31]=0;
        HR(make(dev));WCHAR good[32];swprintf(good,32,L"XDXR RadixSortPrefixSum%u",n);
        CK(SLOT(wrapper,0x30,SetName)(wrapper.Get(),n==1024?L"XDXR RadixSortPrefixSum2048":L"XDXR RadixSortPrefixSum1024")==E_NOTIMPL);
        HR(SLOT(wrapper,0x30,SetName)(wrapper.Get(),good));
        ID3D12RootSignature *rs=nullptr;auto ps=test_bvh_radix_host(wrapper.Get(),&rs);CK(rs&&ps);
        ComPtr<ID3D12Device> d;HR(ps->GetDevice(IID_PPV_ARGS(&d)));
        D3D12_COMMAND_QUEUE_DESC qd={}; qd.Type=D3D12_COMMAND_LIST_TYPE_COMPUTE;
        ComPtr<ID3D12CommandQueue> q;HR(d->CreateCommandQueue(&qd,IID_PPV_ARGS(&q)));
        ComPtr<ID3D12Fence> f;HR(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&f)));UINT64 serial=0;
        const UINT threads=n<=128?32:n<=512?64:n==1024?256:512;
        const UINT w=n/threads, groups=3, guard=16, sum=guard+4096*groups+guard, words=sum+groups+guard;
        for(UINT mode=0;mode<4;mode++){
            std::vector<UINT> data(words);for(UINT i=0;i<words;i++)data[i]=0xc18d0000u+i;
            UINT rng=0x103f3597u;
            for(UINT g=0;g<groups;g++)for(UINT i=0;i<n;i++){
                rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;
                const UINT lane=(i/w)%64, j=i%w;
                data[guard+g*n+i]=mode==0?1+g:mode==1?rng:mode==2?
                    ((lane==0||lane==31||lane==32||lane==63)&&(j==0||j+1==w)?(g+1)*(i+3):0):
                    (i%2==0?0xfffffff0u:i+g);
            }
            std::vector<UINT> expected=data;
            for(UINT g=0;g<groups;g++){UINT total=0;for(UINT i=0;i<n;i++){UINT index=guard+n*g+i;
                expected[index]=total;total+=data[index];}if(mode&1)expected[sum+g]=total;}
            auto buffer=[&](R&r,D3D12_HEAP_TYPE type,D3D12_RESOURCE_STATES st){
                D3D12_HEAP_PROPERTIES hp={};hp.Type=type;hp.CreationNodeMask=hp.VisibleNodeMask=1;
                D3D12_RESOURCE_DESC rd={};rd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;rd.Width=words*4;
                rd.Height=rd.DepthOrArraySize=rd.MipLevels=rd.SampleDesc.Count=1;rd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
                if(type==D3D12_HEAP_TYPE_DEFAULT)rd.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
                return d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,st,nullptr,IID_PPV_ARGS(&r));};
            R src,dst,rb;HR(buffer(src,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ));HR(buffer(dst,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_COPY_DEST));
            HR(buffer(rb,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST));UINT*m=nullptr;HR(src->Map(0,nullptr,(void**)&m));memcpy(m,data.data(),words*4);src->Unmap(0,nullptr);
            ComPtr<ID3D12CommandAllocator> al;ComPtr<ID3D12GraphicsCommandList> l;HR(d->CreateCommandAllocator(qd.Type,IID_PPV_ARGS(&al)));
            HR(d->CreateCommandList(1,qd.Type,al.Get(),ps,IID_PPV_ARGS(&l)));
            auto transition=[&](D3D12_RESOURCE_STATES from,D3D12_RESOURCE_STATES to){D3D12_RESOURCE_BARRIER bar={};bar.Transition={dst.Get(),0,from,to};l->ResourceBarrier(1,&bar);};
            l->CopyResource(dst.Get(),src.Get());transition(D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            l->SetComputeRootSignature(rs);l->SetComputeRootUnorderedAccessView(0,dst->GetGPUVirtualAddress()+4*guard);l->SetComputeRootUnorderedAccessView(1,dst->GetGPUVirtualAddress()+4*sum);
            l->SetComputeRoot32BitConstant(2,mode,0);l->Dispatch(groups,1,1);
            transition(D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);l->CopyResource(rb.Get(),dst.Get());HR(l->Close());
            ID3D12CommandList *lists[]={l.Get()};q->ExecuteCommandLists(1,lists);HR(q->Signal(f.Get(),++serial));HR(f->SetEventOnCompletion(serial,ev));CK(WaitForSingleObject(ev,20000)==WAIT_OBJECT_0&&f->GetCompletedValue()==serial);
            HR(rb->Map(0,nullptr,(void**)&m));for(UINT i=0;i<words;i++)if(m[i]!=expected[i]){printf("FAIL n=%u mode=%u i=%u got=%08x wanted=%08x\n",n,mode,i,m[i],expected[i]);return 1;}
            rb->Unmap(0,nullptr);printf("PASS N=%u T=%u W=%u b0=%u groups=%u words=%u\n",n,threads,w,mode,groups,words);
        }
    }
    CloseHandle(ev);return 0;
}
