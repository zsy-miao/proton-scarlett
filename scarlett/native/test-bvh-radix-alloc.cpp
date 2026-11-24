#include <algorithm>
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
static int exercise(ID3D12Device *d, IUnknown *pipeline,UINT test){
    constexpr UINT G=16,groups=3,N=groups*2048,stride=5,log=6,l1=stride*256,l2=l1>>log;
    const UINT shift=test==9?13:(test%4)*8, words[]={N,N,l1,l2,N,N}; UINT visible[]={N,N,l1,l2,N,N};
    if(test==5) --visible[1]; // reject last input group
    if(test==6) visible[5]-=71; // shortest output controls both stores
    if(test==8) --visible[3];
    if(test==7) visible[2]=255*stride; // exclude live cells in the last bucket
    std::vector<UINT> initial[6],expected[6],hist(l1);UINT rng=0x58f531;
    for(UINT b=0;b<6;++b){initial[b].resize(words[b]+2*G);for(UINT i=0;i<initial[b].size();++i)initial[b][i]=0xc20b0001u+71*i+b;}
    for(UINT i=0;i<N;++i){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;
        UINT digit=test==0?255:test==1?((i&32)?i%7:0):test==2?(i/64)%256:(rng&255);
        initial[0][G+i]=(rng&~(255u<<shift))|(digit<<shift);initial[1][G+i]=0x957ae130u^(i*341);
        hist[digit*stride+i/2048]++;}
    UINT total=0;
    for(UINT j=0;j<l2;++j){UINT block=test==9?l2-1-j:j;initial[3][G+block]=total;UINT local=0;
        for(UINT off=0;off<1u<<log;++off){UINT cell=(block<<log)+off;initial[2][G+cell]=local;local+=hist[cell];} total+=local;}
    CK(total==N);
    if(test==4){initial[2][G+19*stride]=0xfffffff0u;initial[3][G+8]=0xfffffff0u;}
    for(UINT b=0;b<6;++b)expected[b]=initial[b];
    std::vector<UINT> order(N);for(UINT i=0;i<N;++i)order[i]=i;
    auto digit=[&](UINT i){return (initial[0][G+i]>>shift)&255;};
    auto cellFor=[&](UINT i){return digit(i)*stride+i/2048;};
    std::stable_sort(order.begin(),order.end(),[&](UINT a,UINT b){
        UINT x=cellFor(a),y=cellFor(b);
        if(test==9 && (x>>log)!=(y>>log))return (x>>log)>(y>>log);
        return x<y;});
    for(UINT i=0;i<N;++i){UINT src=order[i],g=src/2048,cell=digit(src)*stride+g;
        if(test==4&&(cell==19*stride||(cell>>log)==8))continue;
        if((g+1)*2048>visible[1] || cell>=visible[2] || (cell>>log)>=visible[3] || i>=visible[5])continue;
        expected[4][G+i]=initial[0][G+src];expected[5][G+i]=initial[1][G+src];}
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
    R seed[6],dst[6],rb[6];
    for(UINT b=0;b<6;b++){
        HR(buffer(seed[b],initial[b].size(),D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ));HR(buffer(dst[b],initial[b].size(),D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_COPY_DEST));
        HR(buffer(rb[b],initial[b].size(),D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST));void*m;
        HR(seed[b]->Map(0,nullptr,&m));memcpy(m,initial[b].data(),initial[b].size()*4);seed[b]->Unmap(0,nullptr);
        l->CopyResource(dst[b].Get(),seed[b].Get());barrier(dst[b],D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);}
    ID3D12RootSignature *r=nullptr;auto host=test_bvh_radix_host(pipeline,&r);CK(host&&r);l->SetComputeRootSignature(r);l->SetPipelineState(host);
    D3D12_DESCRIPTOR_HEAP_DESC hd={D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,6,D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,1};
    ComPtr<ID3D12DescriptorHeap> heap;HR(d->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&heap)));auto cpu=heap->GetCPUDescriptorHandleForHeapStart();
    for(UINT i=0;i<6;++i){D3D12_UNORDERED_ACCESS_VIEW_DESC v={};v.Format=DXGI_FORMAT_R32_TYPELESS;v.ViewDimension=D3D12_UAV_DIMENSION_BUFFER;
        v.Buffer.FirstElement=G;v.Buffer.NumElements=visible[i];v.Buffer.Flags=D3D12_BUFFER_UAV_FLAG_RAW;d->CreateUnorderedAccessView(dst[i].Get(),nullptr,&v,cpu);
        cpu.ptr+=d->GetDescriptorHandleIncrementSize(hd.Type);}
    ID3D12DescriptorHeap *heaps[]={heap.Get()};l->SetDescriptorHeaps(1,heaps);l->SetComputeRootDescriptorTable(0,heap->GetGPUDescriptorHandleForHeapStart());
    const UINT k[]={stride,shift,log};l->SetComputeRoot32BitConstants(1,3,k,0);l->Dispatch(groups,1,1);
    for(UINT b=0;b<6;b++){barrier(dst[b],D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);l->CopyResource(rb[b].Get(),dst[b].Get());}
    HR(l->Close());ID3D12CommandList *lists[]={l.Get()};q->ExecuteCommandLists(1,lists);
    ComPtr<ID3D12Fence>f;HR(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&f)));HANDLE e=CreateEventW(nullptr,FALSE,FALSE,nullptr);CK(e);
    HR(q->Signal(f.Get(),1));HR(f->SetEventOnCompletion(1,e));CK(WaitForSingleObject(e,20000)==WAIT_OBJECT_0&&f->GetCompletedValue()==1);CloseHandle(e);
    for(UINT b=0;b<6;b++){UINT*m;HR(rb[b]->Map(0,nullptr,(void**)&m));for(UINT i=0;i<expected[b].size();i++)if(m[i]!=expected[b][i]){
        printf("FAIL %s case=%u buffer=%u i=%u got=%x expected=%x\n","RadixAlloc",test,b,i,m[i],expected[b][i]);return 1;}rb[b]->Unmap(0,nullptr);}
    printf("PASS %s case=%u groups=%u all input/output/guards\n","RadixAlloc",test,groups);return 0;
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
        CK(make(1,bytes.data())==E_NOTIMPL&&!root[z]);HR(make(1,blob->GetBufferPointer()));
        const WCHAR *name=z==0?L"XDXR RadixSortCountAndAlloc":z==1?L"XDXR RadixSortPrefixSum":L"XDXR UtilityShaders";WCHAR wrong[32]={};
        CK(SLOT(root[z],0x30,Name)(root[z].Get(),wrong)==E_NOTIMPL);HR(SLOT(root[z],0x30,Name)(root[z].Get(),name));
        CK(!xs_bvh_root_host(other.Get(),root[z].Get())&&xs_bvh_root_host(dev.Get(),root[z].Get()));
        printf("PASS root kind=%u bytes=%zu exact/owner/name\n",z,n);
    }
    ComPtr<ID3D12Device>d;HR(xs_bvh_root_host(dev.Get(),root[0].Get())->GetDevice(IID_PPV_ARGS(&d)));
    {
        std::vector<BYTE>code(0x3f38);FILE*f=fopen("private-radix-alloc.bin","rb");CK(f&&fread(code.data(),1,code.size(),f)==code.size()&&fgetc(f)==EOF&&!ferror(f));fclose(f);
        struct Wire{IUnknown *root;const void *cs;SIZE_T size;BYTE tail[32];}w={root[0].Get(),code.data(),code.size(),{}};
        using Make=HRESULT(WINAPI *)(IUnknown*,const Wire*,UINT64,UINT64,REFIID,void**);
        auto make=[&](ComPtr<IUnknown>&o){return SLOT(o,0x1f0,Make)(o.Get(),&w,0,0,IID_ID3D12PipelineState,(void**)ps.GetAddressOf());};
        CK(make(other)==E_INVALIDARG&&!ps);for(UINT z:{1u,2u}){w.root=root[z].Get();CK(make(dev)==E_INVALIDARG&&!ps);}w.root=root[0].Get();
        w.size--;CK(make(dev)==E_NOTIMPL&&!ps);w.size++;w.tail[31]=1;CK(make(dev)==E_NOTIMPL&&!ps);w.tail[31]=0;
        code.back()^=1;CK(make(dev)==E_NOTIMPL&&!ps);code.back()^=1;
        HR(make(dev));WCHAR wrong[32]={};CK(SLOT(ps,0x30,Name)(ps.Get(),wrong)==E_NOTIMPL);
        HR(SLOT(ps,0x30,Name)(ps.Get(),L"XDXR RadixSortAlloc"));
        puts("PASS RadixAlloc factory +1f0 owner/schema/digest/size/tail/name");
        for(UINT test=0;test<10;++test)CK(!exercise(d.Get(),ps.Get(),test));
    }
}
