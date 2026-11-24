#include <d3d12.h>
#include <wrl/client.h>
#include <algorithm>
#include <vector>
#include <cstdio>
#include <cstring>
using Microsoft::WRL::ComPtr;
using R=ComPtr<ID3D12Resource>;
#define CK(x) do{if(!(x)){printf("FAIL line %d %s\n",__LINE__,#x);return 1;}}while(0)
#define HR(x) do{HRESULT hr_=(x);if(FAILED(hr_)){printf("FAIL line %d hr=%lx %s\n",__LINE__,(long)hr_,#x);return 1;}}while(0)
#define SLOT(o,n,T) ((T)((*reinterpret_cast<void***>((o).Get()))[n/8]))
extern "C" HRESULT WINAPI D3D12XboxCreateDevice(void *,const void *,REFIID,void **);
extern "C" ID3D12RootSignature *xs_bvh_root_host(IUnknown *,IUnknown *);
extern "C" ID3D12PipelineState *test_bvh_radix_host(IUnknown *,ID3D12RootSignature **);
static int exercise(ID3D12Device *d, IUnknown *pipeline,bool count,UINT test){
    constexpr UINT G=16; // guard DWORDs at each end, root VAs 64-byte aligned
    const UINT groups=count?3:2, stride=groups+2, shift=test*8,n=test==0?0:test==1?32:test==2?33:64;
    const UINT words[]={count?2048*groups:64*4,count?256*stride:4};
    std::vector<UINT> expected[2],initial[2];
    for(UINT b=0;b<2;b++){initial[b].resize(G+words[b]+G);for(UINT i=0;i<initial[b].size();i++)initial[b][i]=0xc70a0001u+211*i+b;}
    if(count){for(UINT i=0,rnd=0x375deu;i<words[0];i++){
        rnd^=rnd<<13;rnd^=rnd>>17;rnd^=rnd<<5;
        // Probe high/low logical half-waves, neighboring waves, rotating pages and distinct groups.
        UINT byte=test==0?((i&32)?0xff:0):test==1?((i>>6)+3*(i>>11))&255:test==2?i&255:(rnd>>24);
        initial[0][G+i]=(rnd&~(255u<<shift))|(byte<<shift);}}
    expected[0]=initial[0];expected[1]=initial[1];
    if(count){for(UINT k=0;k<256;k++)for(UINT g=0;g<groups;g++)expected[1][G+k*stride+g]=0;
        for(UINT i=0;i<words[0];i++)++expected[1][G+((initial[0][G+i]>>shift)&255)*stride+i/2048];}
    else std::fill(expected[0].begin()+G,expected[0].begin()+G+4*n,0);
    auto buffer=[&](R &r,UINT words_,D3D12_HEAP_TYPE h,D3D12_RESOURCE_STATES st){
        D3D12_HEAP_PROPERTIES hp={};hp.Type=h;hp.CreationNodeMask=hp.VisibleNodeMask=1;
        D3D12_RESOURCE_DESC rd={};rd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;rd.Width=4*words_;
        rd.Height=rd.DepthOrArraySize=rd.MipLevels=rd.SampleDesc.Count=1;rd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if(h==D3D12_HEAP_TYPE_DEFAULT)rd.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        return d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,st,nullptr,IID_PPV_ARGS(&r));};
    D3D12_COMMAND_QUEUE_DESC qd={};qd.Type=D3D12_COMMAND_LIST_TYPE_COMPUTE;
    ComPtr<ID3D12CommandQueue>q;ComPtr<ID3D12CommandAllocator>a;ComPtr<ID3D12GraphicsCommandList>l;
    HR(d->CreateCommandQueue(&qd,IID_PPV_ARGS(&q)));HR(d->CreateCommandAllocator(qd.Type,IID_PPV_ARGS(&a)));
    HR(d->CreateCommandList(1,qd.Type,a.Get(),nullptr,IID_PPV_ARGS(&l)));
    auto barrier=[&](R&r,D3D12_RESOURCE_STATES from,D3D12_RESOURCE_STATES to){D3D12_RESOURCE_BARRIER b={};b.Transition={r.Get(),0,from,to};l->ResourceBarrier(1,&b);};
    R seed[2],dst[2],rb[2];
    for(UINT b=0;b<2;b++){
        HR(buffer(seed[b],initial[b].size(),D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ));HR(buffer(dst[b],initial[b].size(),D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_COPY_DEST));
        HR(buffer(rb[b],initial[b].size(),D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST));void*m;
        HR(seed[b]->Map(0,nullptr,&m));memcpy(m,initial[b].data(),initial[b].size()*4);seed[b]->Unmap(0,nullptr);
        l->CopyResource(dst[b].Get(),seed[b].Get());barrier(dst[b],D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);}
    ID3D12RootSignature *r=nullptr;auto host=test_bvh_radix_host(pipeline,&r);CK(host&&r);l->SetComputeRootSignature(r);l->SetPipelineState(host);
    for(UINT b=0;b<6;b++)l->SetComputeRootUnorderedAccessView(b,dst[b?1:0]->GetGPUVirtualAddress()+G*4);
    const UINT k[]={count?stride:n,shift,0x7777u};l->SetComputeRoot32BitConstants(6,3,k,0);l->Dispatch(groups,1,1);
    for(UINT b=0;b<2;b++){barrier(dst[b],D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);l->CopyResource(rb[b].Get(),dst[b].Get());}
    HR(l->Close());ID3D12CommandList *lists[]={l.Get()};q->ExecuteCommandLists(1,lists);
    ComPtr<ID3D12Fence>f;HR(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&f)));HANDLE e=CreateEventW(nullptr,FALSE,FALSE,nullptr);CK(e);
    HR(q->Signal(f.Get(),1));HR(f->SetEventOnCompletion(1,e));CK(WaitForSingleObject(e,20000)==WAIT_OBJECT_0&&f->GetCompletedValue()==1);CloseHandle(e);
    for(UINT b=0;b<2;b++){UINT*m;HR(rb[b]->Map(0,nullptr,(void**)&m));for(UINT i=0;i<expected[b].size();i++)if(m[i]!=expected[b][i]){
        printf("FAIL %s case=%u buffer=%u i=%u got=%x expected=%x\n",count?"count":"reset",test,b,i,m[i],expected[b][i]);return 1;}rb[b]->Unmap(0,nullptr);}
    printf("PASS %s case=%u groups=%u all input/output/guards\n",count?"count":"reset",test,groups);return 0;
}
int main(){
    const IID boot={0x5405c344,0xd457,0x444e,{0xb4,0xdd,0x23,0x66,0xe4,0x5a,0xee,0x39}};
    DWORD params[20]={};params[0]=0x20004;params[2]=0x100000;params[10]=1;params[15]=0x20;
    ComPtr<IUnknown>dev,other,root[3],ps;HR(D3D12XboxCreateDevice(nullptr,params,boot,(void **)dev.GetAddressOf()));HR(D3D12XboxCreateDevice(nullptr,params,boot,(void **)other.GetAddressOf()));
    using Root=HRESULT(WINAPI *)(IUnknown*,UINT,const void*,SIZE_T,REFIID,void**);using Name=HRESULT(WINAPI *)(IUnknown*,const WCHAR*);
    for(UINT z=0;z<3;z++){
        D3D12_ROOT_PARAMETER1 rp[7]={};const UINT p=z?2:6;
        for(UINT i=0;i<p;i++){rp[i].ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV;rp[i].Descriptor.ShaderRegister=i;}
        if(z==2){rp[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV;rp[1].Descriptor.ShaderRegister=0;}
        rp[p].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;rp[p].Constants.Num32BitValues=z==2?16:z==1?1:3;
        D3D12_VERSIONED_ROOT_SIGNATURE_DESC sig={};sig.Version=D3D_ROOT_SIGNATURE_VERSION_1_1;sig.Desc_1_1={p+1,rp,0,nullptr,(D3D12_ROOT_SIGNATURE_FLAGS)0x03000000};
        ComPtr<ID3DBlob>blob;HR(D3D12SerializeVersionedRootSignature(&sig,&blob,nullptr));auto n=blob->GetBufferSize();CK(n==(z?140:236));
        auto make=[&](UINT node,const void*b){return SLOT(dev,0x80,Root)(dev.Get(),node,b,n,IID_ID3D12RootSignature,(void**)root[z].GetAddressOf());};
        CK(make(0,blob->GetBufferPointer())==E_NOTIMPL&&!root[z]);std::vector<BYTE>bytes(n);memcpy(bytes.data(),blob->GetBufferPointer(),n);bytes.back()^=1;
        CK(make(1,bytes.data())==E_NOTIMPL&&!root[z]);
        rp[1].Descriptor.RegisterSpace=1;ComPtr<ID3DBlob>other_blob;HR(D3D12SerializeVersionedRootSignature(&sig,&other_blob,nullptr));
        CK(other_blob->GetBufferSize()==n&&make(1,other_blob->GetBufferPointer())==E_NOTIMPL&&!root[z]);
        HR(make(1,blob->GetBufferPointer()));
        const WCHAR *name=z==0?L"XDXR RadixSortCountAndAlloc":z==1?L"XDXR RadixSortPrefixSum":L"XDXR UtilityShaders";WCHAR wrong[32]={};
        CK(SLOT(root[z],0x30,Name)(root[z].Get(),wrong)==E_NOTIMPL);HR(SLOT(root[z],0x30,Name)(root[z].Get(),name));
        CK(!xs_bvh_root_host(other.Get(),root[z].Get())&&xs_bvh_root_host(dev.Get(),root[z].Get()));
        printf("PASS root kind=%u bytes=%zu exact/owner/name\n",z,n);
    }
    ComPtr<ID3D12Device>d;HR(xs_bvh_root_host(dev.Get(),root[0].Get())->GetDevice(IID_PPV_ARGS(&d)));
    for(UINT i=0;i<2;i++){
        std::vector<BYTE>code(i?0x1ffc:0xfec);FILE*f=fopen(i?"private-radix-count.bin":"private-radix-reset.bin","rb");CK(f&&fread(code.data(),1,code.size(),f)==code.size()&&fgetc(f)==EOF&&!ferror(f));fclose(f);
        struct Wire{IUnknown *root;const void *cs;SIZE_T size;BYTE tail[32];}w={root[0].Get(),code.data(),code.size(),{}};
        using Make=HRESULT(WINAPI *)(IUnknown*,const Wire*,UINT64,UINT64,REFIID,void**);
        auto make=[&](ComPtr<IUnknown>&o){return SLOT(o,0x1f0,Make)(o.Get(),&w,0,0,IID_ID3D12PipelineState,(void**)ps.GetAddressOf());};
        CK(make(other)==E_INVALIDARG&&!ps);for(UINT z:{1u,2u}){w.root=root[z].Get();CK(make(dev)==E_INVALIDARG&&!ps);}w.root=root[0].Get();
        w.size--;CK(make(dev)==E_NOTIMPL&&!ps);w.size++;w.tail[31]=1;CK(make(dev)==E_NOTIMPL&&!ps);w.tail[31]=0;
        code.back()^=1;CK(make(dev)==E_NOTIMPL&&!ps);code.back()^=1;
        if(i&&!GetProcAddress(GetModuleHandleW(L"d3d12core.dll"),"ScarlettRadixCountV1")){CK(make(dev)==E_NOTIMPL&&!ps);puts("PASS count missing core rejected");continue;}
        HR(make(dev));ID3D12RootSignature *bound=nullptr;CK(test_bvh_radix_host(ps.Get(),&bound)&&bound==xs_bvh_root_host(dev.Get(),root[0].Get()));
        WCHAR wrong[32]={};CK(SLOT(ps,0x30,Name)(ps.Get(),wrong)==E_NOTIMPL);
        HR(SLOT(ps,0x30,Name)(ps.Get(),i?L"XDXR RadixSortCount":L"XDXR RadixReset"));
        printf("PASS factory +1f0 kind=%u owner/schema/digest/size/tail/name\n",i);
        for(UINT test=0;test<4;test++)CK(!exercise(d.Get(),ps.Get(),i,test));
        ps.Reset();
    }
}
