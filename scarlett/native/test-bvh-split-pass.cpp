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
extern "C" ID3D12PipelineState *test_bvh_split_pass_host(IUnknown *,ID3D12RootSignature **);
static int exercise(ID3D12Device *d,IUnknown *pipeline,UINT test){
    constexpr UINT G=192, old=4, slots=12; // 768 bytes: aligned for all four UAV view strides.
    constexpr UINT64 A=0x10ffffff00ULL,B=0x21fffffff8ULL;
    const bool deep=test==1, overflow=test==2,badAtom=test==3,shortRoot=test==4,shortBox=test==5,
        half=test==6,center=test==7,leaf=test==8;
    const UINT words[]={32*slots,1,6*slots,2,320,16};std::vector<UINT> initial[6],expected[6];
    for(UINT b=0;b<6;b++){initial[b].resize(G+words[b]+G);for(UINT i=0;i<initial[b].size();i++)initial[b][i]=0xc71a0000u+211*i+b;}
    initial[1][G]=test==9?0:2;initial[3][G]=2;initial[3][G+1]=0;initial[5][G+3]=old;
    auto *src=initial[4].data()+G;float centerBox[]={1,-5,-8,5,7,-4};memcpy(src+4,centerBox,sizeof centerBox);
    const UINT kids[]={69,85,133,149,165,181};
    const float boxes[][6]={{-1,-2,-3,4,5,6},{-2,-3,-4,5,6,7},{-4,-1,-1,1,2,3},{-8,-7,-6,2,3,4},{-2,-3,-1,1,7,6},{-3,-2,-1,4,5,6}};
    for(UINT k=0;k<6;k++){
        UINT base=k<2?64:k<4?128:160,j=k%2;src[base+j]=kids[k];src[base+2]=src[base+3]=0;
        if(half&&k<2){const UINT h[][3]={{0xc000bc00,0x4400c200,0x46004500},{0xc200c000,0x4500c400,0x47004600}};memcpy(src+base+4+3*j,h[j],12);}
        else memcpy(src+base+4+6*j,boxes[k],24);
    }
    for(UINT lane=0;lane<2;lane++){
        UINT slot=initial[3][G+lane];UINT *e=initial[0].data()+G+32*slot;
        float m[]={-1,0,0,10.f+lane,0,2,0,-5,0,0,1,20};memcpy(e,m,sizeof m);
        e[14]=0x80000000u|(leaf?8u:half?36u:37u);e[15]=UINT(A>>8);e[30]=center?0x40000000:0;
    }
    const UINT limit=test==10?1:2,readBytes=shortRoot?271:shortBox?319:words[4]*4,atomBytes=badAtom?15:16;
    for(UINT b=0;b<6;b++)expected[b]=initial[b];
    if(!overflow&&!badAtom&&!shortRoot&&!leaf&&initial[1][G]){
        UINT append=old;
        for(UINT lane=0;lane<limit;lane++){
            UINT orig=initial[3][G+lane],n=deep&&!lane?4:2;auto *e=initial[0].data()+G+32*orig;
            for(UINT j=0;j<n;j++){
                UINT k=n==4?j+2:j,slot=j?append++:orig;
                UINT *out=expected[0].data()+G+32*slot;memcpy(out,e,124);out[14]=0x80000000u|kids[k];out[29]=0;
                // A short x3 max read returns three zeros, never a partial vector.
                const float *box=boxes[k];float x=10.f+lane-(center?3.f:0.f),z=20.f-(center?6.f:0.f);
                bool zero=shortBox&&k==1;
                float aabb[]={x-(zero?0:box[3]),2*box[1]-5,z+box[2],x-box[0],2*(zero?0:box[4])-5,z+(zero?0:box[5])};
                if(test!=11 || slot<old) memcpy(expected[2].data()+G+6*slot,aabb,sizeof aabb);
            }
        }
        expected[5][G+3]=append;
    }
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
        l->CopyResource(dst[b].Get(),seed[b].Get());barrier(dst[b],D3D12_RESOURCE_STATE_COPY_DEST,b==4?D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE:D3D12_RESOURCE_STATE_UNORDERED_ACCESS);}
    ID3D12RootSignature *r=nullptr;auto host=test_bvh_split_pass_host(pipeline,&r);CK(host&&r);l->SetComputeRootSignature(r);l->SetPipelineState(host);
    auto va=[&](UINT b){return dst[b]->GetGPUVirtualAddress()+G*4;};
    D3D12_DESCRIPTOR_HEAP_DESC hd={D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,4,D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,1};
    ComPtr<ID3D12DescriptorHeap> heap;HR(d->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&heap)));auto cpu=heap->GetCPUDescriptorHandleForHeapStart();
    for(UINT i=0;i<4;i++){
        D3D12_UNORDERED_ACCESS_VIEW_DESC v={};v.ViewDimension=D3D12_UAV_DIMENSION_BUFFER;
        UINT stride=i==2?6:1;v.Buffer.FirstElement=G/stride;v.Buffer.NumElements=(test==11 && i==2)?old:words[i]/stride;
        if(i)v.Buffer.StructureByteStride=stride*4;else{v.Format=DXGI_FORMAT_R32_TYPELESS;v.Buffer.Flags=D3D12_BUFFER_UAV_FLAG_RAW;}
        d->CreateUnorderedAccessView(dst[i].Get(),nullptr,&v,cpu);cpu.ptr+=d->GetDescriptorHandleIncrementSize(hd.Type);
    }
    ID3D12DescriptorHeap *heaps[]={heap.Get()};l->SetDescriptorHeaps(1,heaps);l->SetComputeRootDescriptorTable(0,heap->GetGPUDescriptorHandleForHeapStart());
    const UINT pc[]={UINT(B),UINT(B>>32),0xbaadf00d,0xdeadbeef,limit,deep?1u:0u,overflow?5u:slots,~0u};
    const UINT window[]={UINT(A),UINT(A>>32),readBytes,UINT(B),UINT(B>>32),atomBytes};
    l->SetComputeRoot32BitConstants(1,8,pc,0);l->SetComputeRoot32BitConstants(2,6,window,0);
    l->SetComputeRootShaderResourceView(3,va(4));l->SetComputeRootUnorderedAccessView(4,va(5));
    l->Dispatch(2,1,1);
    for(UINT b=0;b<6;b++){barrier(dst[b],b==4?D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE:D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);l->CopyResource(rb[b].Get(),dst[b].Get());}
    HR(l->Close());ID3D12CommandList *lists[]={l.Get()};q->ExecuteCommandLists(1,lists);
    ComPtr<ID3D12Fence>f;HR(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&f)));HANDLE e=CreateEventW(nullptr,FALSE,FALSE,nullptr);CK(e);
    HR(q->Signal(f.Get(),1));HR(f->SetEventOnCompletion(1,e));CK(WaitForSingleObject(e,20000)==WAIT_OBJECT_0&&f->GetCompletedValue()==1);CloseHandle(e);
    for(UINT b=0;b<6;b++){UINT*m;HR(rb[b]->Map(0,nullptr,(void**)&m));for(UINT i=0;i<expected[b].size();i++)if(m[i]!=expected[b][i]){
        printf("FAIL %s case=%u buffer=%u i=%u got=%x expected=%x\n","splitpass",test,b,i,m[i],expected[b][i]);return 1;}rb[b]->Unmap(0,nullptr);}
    printf("PASS splitpass case=%u fenced all six complete buffers\n",test);return 0;
}
using Create=decltype(&D3D12XboxCreateDevice);
static int verify(Create create, bool staged){
    const IID boot={0x5405c344,0xd457,0x444e,{0xb4,0xdd,0x23,0x66,0xe4,0x5a,0xee,0x39}};
    DWORD params[20]={};params[0]=0x20004;params[2]=0x100000;params[10]=1;params[15]=0x20;
    ComPtr<IUnknown>dev,other,root[3],ps;HR(create(nullptr,params,boot,(void **)dev.GetAddressOf()));HR(create(nullptr,params,boot,(void **)other.GetAddressOf()));
    using Root=HRESULT(WINAPI *)(IUnknown*,UINT,const void*,SIZE_T,REFIID,void**);using Name=HRESULT(WINAPI *)(IUnknown*,const WCHAR*);
    for(UINT z=0;z<3;z++){
        D3D12_ROOT_PARAMETER1 rp[7]={};const UINT p=z==1?2:z==2?3:4;
        for(UINT i=0;i<p;i++){rp[i].ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV;rp[i].Descriptor.ShaderRegister=i;}
        if(z==1){rp[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV;rp[1].Descriptor.ShaderRegister=0;}
        rp[p].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;rp[p].Constants.Num32BitValues=z?16:8;
        if(z==2) for(UINT i=0;i<2;i++){
            rp[i]={};rp[i].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;rp[i].Constants={1-i,0,2};
            rp[i+2]={};rp[i+2].ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV;rp[i+2].Descriptor.ShaderRegister=i;
        }
        D3D12_VERSIONED_ROOT_SIGNATURE_DESC sig={};sig.Version=D3D_ROOT_SIGNATURE_VERSION_1_1;sig.Desc_1_1={p+1,rp,0,nullptr,(D3D12_ROOT_SIGNATURE_FLAGS)0x03000000};
        ComPtr<ID3DBlob>blob;HR(D3D12SerializeVersionedRootSignature(&sig,&blob,nullptr));auto n=blob->GetBufferSize();CK(n==(z==1?140:z==2?164:188));
        auto make=[&](UINT node,const void*b){return SLOT(dev,0x80,Root)(dev.Get(),node,b,n,IID_ID3D12RootSignature,(void**)root[z].GetAddressOf());};
        CK(make(0,blob->GetBufferPointer())==E_NOTIMPL&&!root[z]);std::vector<BYTE>bytes(n);memcpy(bytes.data(),blob->GetBufferPointer(),n);bytes.back()^=1;
        CK(make(1,bytes.data())==E_NOTIMPL&&!root[z]);
        rp[1].Descriptor.RegisterSpace=1;ComPtr<ID3DBlob>other_blob;HR(D3D12SerializeVersionedRootSignature(&sig,&other_blob,nullptr));
        CK(other_blob->GetBufferSize()==n&&make(1,other_blob->GetBufferPointer())==E_NOTIMPL&&!root[z]);
        HR(make(1,blob->GetBufferPointer()));
        const WCHAR *name=!z?L"XDXR SplitInstancesPass":z==1?L"XDXR UtilityShaders":L"XDXR BitonicSort";WCHAR wrong[32]={};
        CK(SLOT(root[z],0x30,Name)(root[z].Get(),wrong)==E_NOTIMPL);HR(SLOT(root[z],0x30,Name)(root[z].Get(),name));
        if(!staged) CK(!xs_bvh_root_host(other.Get(),root[z].Get())&&xs_bvh_root_host(dev.Get(),root[z].Get()));
        printf("PASS root kind=%u bytes=%zu exact/owner/name\n",z,n);
    }
    ComPtr<ID3D12Device>d;if(!staged) HR(xs_bvh_root_host(dev.Get(),root[0].Get())->GetDevice(IID_PPV_ARGS(&d)));
    {
        std::vector<BYTE>code(0x88f4);FILE*f=fopen("original.bin","rb");CK(f&&fread(code.data(),1,code.size(),f)==code.size()&&fgetc(f)==EOF&&!ferror(f));fclose(f);
        struct Wire{IUnknown *root;const void *cs;SIZE_T size;BYTE tail[32];}w={root[0].Get(),code.data(),code.size(),{}};
        using Make=HRESULT(WINAPI *)(IUnknown*,const Wire*,UINT64,UINT64,REFIID,void**);
        auto make=[&](ComPtr<IUnknown>&o){return SLOT(o,0x1f0,Make)(o.Get(),&w,0,0,IID_ID3D12PipelineState,(void**)ps.GetAddressOf());};
        CK(make(other)==E_INVALIDARG&&!ps);for(UINT z:{1u,2u}){w.root=root[z].Get();CK(make(dev)==E_INVALIDARG&&!ps);}w.root=root[0].Get();
        w.size--;CK(make(dev)==E_NOTIMPL&&!ps);w.size++;w.tail[31]=1;CK(make(dev)==E_NOTIMPL&&!ps);w.tail[31]=0;
        code.back()^=1;CK(make(dev)==E_NOTIMPL&&!ps);code.back()^=1;
        if(!GetProcAddress(GetModuleHandleW(L"d3d12core.dll"),"ScarlettBoundedSplitPassV1")){CK(make(dev)==E_NOTIMPL&&!ps);puts("PASS split pass missing core rejected");return 0;}
        // The exact guest root alone lacks both authorized windows. Never dispatch it.
        if(!staged){
            D3D12_COMPUTE_PIPELINE_STATE_DESC pd={};pd.pRootSignature=xs_bvh_root_host(dev.Get(),root[0].Get());pd.CS={code.data(),code.size()};
            ComPtr<ID3D12PipelineState>bad;CK(FAILED(d->CreateComputePipelineState(&pd,IID_PPV_ARGS(&bad)))&&!bad);
            puts("PASS compiler declined exact guest shader without private windows");
        }
        HR(make(dev));
        WCHAR wrong[32]={};CK(SLOT(ps,0x30,Name)(ps.Get(),wrong)==E_NOTIMPL);
        HR(SLOT(ps,0x30,Name)(ps.Get(),L"XDXR SplitInstancesPass"));
        if(staged){puts("PASS staged DLL split-pass factory");return 0;}
        puts("PASS factory +1f0 owner/schema/digest/size/tail/name");
        for(UINT test=0;test<12;test++)CK(!exercise(d.Get(),ps.Get(),test));
        ps.Reset();
    }
    return 0;
}
int main(){
    HMODULE dll=LoadLibraryW(L"d3d12_xs.dll");CK(dll);FARPROC fn=GetProcAddress(dll,"D3D12XboxCreateDevice");Create create;
    CK(fn);memcpy(&create,&fn,sizeof create);CK(!verify(create,true));
    return verify(D3D12XboxCreateDevice,false);
}
