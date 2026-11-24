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
extern "C" ID3D12PipelineState *test_bvh_load_pass_host(IUnknown *,ID3D12RootSignature **);
static UINT bits(float f){UINT u;memcpy(&u,&f,4);return u;}
static int exercise(ID3D12Device *d,IUnknown *pipeline,UINT test){
    const UINT G=16,N=3,old=2;const UINT64 A=0x10ffffff00ULL,B=0x2100000000ULL,blas=A+768,desc=A+256;
    const bool indirect=test==1||test==5, overflow=test==2, badCounter=test==3, badRead=test==5, empty=test==4, packed=test==6, score=test==7;
    const UINT words[]={32*8,6*8,2*8,48,320};std::vector<UINT> initial[5],expected[5];
    for(UINT b=0;b<5;b++){initial[b].resize(G+words[b]+G);for(UINT i=0;i<initial[b].size();i++)initial[b][i]=0xc71a0000u+211*i+b;}
    auto *src=initial[4].data()+G,*atom=initial[3].data()+G;atom[3]=old;
    for(UINT j=0;j<6;j++)atom[4+j]=atom[16+j]=bits(j<3?3.402823466e38f:-3.402823466e38f);
    for(UINT i=0;i<N;i++){
        UINT *e=src+64+16*i;std::fill(e,e+16,0);e[0]=e[5]=e[10]=bits(1);e[3]=bits(float(i+1));e[7]=bits(2);e[11]=bits(3);e[12]=0xff000004+i;e[14]=UINT(blas);e[15]=UINT(blas>>32);
        UINT64 p=desc+i*64;memcpy(src+2*i,&p,8);
    }
    src[192+12]=8;src[192+15]=0;const float v[]={-3,8,-5,2,-4,11,9,12,1};memcpy(src+208,v,sizeof v);src[208+12]=0;
    if(score){ // Two child boxes, combined half area 640; child half areas 11 and 12.
        src[192]=0;src[192+12]=13;src[208]=8;src[209]=16;src[210]=src[211]=0;
        float child[]={-3,-4,-5,-2,-2,-2,7,10,9,9,12,11};memcpy(src+212,child,sizeof child);
    }
    const UINT readBytes=badRead?7:words[4]*4, atomBytes=badCounter?15:words[3]*4;
    const UINT flags=(indirect?0:16)|(score?0:1)|(packed?2:0)|(score?0:128),cap=overflow?4:8;
    for(UINT b=0;b<5;b++)expected[b]=initial[b];
    if(!overflow&&!badCounter&&!badRead&&!empty){
        auto *acc=expected[3].data()+G;if(!score)acc[3]=old+N;
        for(UINT i=0;i<N;i++){
            UINT slot=(score?0:old)+i;UINT *e=expected[0].data()+G+32*slot;memcpy(e,src+64+16*i,56);e[14]=score?0x8000000d:0x80000008;e[15]=UINT(blas>>8);
            float inv[]={-float(i+1),-2,-3,1,0,0,0,1,0,0,0,1};memcpy(e+16,inv,sizeof inv);if(!packed){e[28]=i;e[29]=0;}e[30]=0;
            if(score){auto *key=expected[2].data()+G+2*slot;key[0]=i;key[1]=~bits(-617.f);}
            if(!packed){float box[]={float(i)-2,-2,-2,float(i)+10,14,14};memcpy(expected[1].data()+G+6*slot,box,sizeof box);}
        }
        if(!packed){float box[]={-2,-2,-2,12,14,14},cent[]={4,6,6,6,6,6};memcpy(acc+4,box,sizeof box);memcpy(acc+16,cent,sizeof cent);}
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
    R seed[5],dst[5],rb[5];
    for(UINT b=0;b<5;b++){
        HR(buffer(seed[b],initial[b].size(),D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ));HR(buffer(dst[b],initial[b].size(),D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_COPY_DEST));
        HR(buffer(rb[b],initial[b].size(),D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST));void*m;
        HR(seed[b]->Map(0,nullptr,&m));memcpy(m,initial[b].data(),initial[b].size()*4);seed[b]->Unmap(0,nullptr);
        l->CopyResource(dst[b].Get(),seed[b].Get());barrier(dst[b],D3D12_RESOURCE_STATE_COPY_DEST,b==4?D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE:D3D12_RESOURCE_STATE_UNORDERED_ACCESS);}
    ID3D12RootSignature *r=nullptr;auto host=test_bvh_load_pass_host(pipeline,&r);CK(host&&r);l->SetComputeRootSignature(r);l->SetPipelineState(host);
    auto va=[&](UINT b){return dst[b]->GetGPUVirtualAddress()+G*4;};
    UINT64 input=indirect?A:desc, centroid=B+64;
    const UINT pc[]={UINT(B),UINT(B>>32),UINT(input),UINT(input>>32),UINT(centroid),UINT(centroid>>32),empty?0:N,cap,flags,0};
    const UINT window[]={UINT(A),UINT(A>>32),readBytes,UINT(B),UINT(B>>32),atomBytes};
    for(UINT b=0;b<3;b++)l->SetComputeRootUnorderedAccessView(b,va(b));
    l->SetComputeRoot32BitConstants(3,10,pc,0);l->SetComputeRoot32BitConstants(4,6,window,0);l->SetComputeRootShaderResourceView(5,va(4));l->SetComputeRootUnorderedAccessView(6,va(3));
    l->Dispatch(2,1,1);
    for(UINT b=0;b<5;b++){barrier(dst[b],b==4?D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE:D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);l->CopyResource(rb[b].Get(),dst[b].Get());}
    HR(l->Close());ID3D12CommandList *lists[]={l.Get()};q->ExecuteCommandLists(1,lists);
    ComPtr<ID3D12Fence>f;HR(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&f)));HANDLE e=CreateEventW(nullptr,FALSE,FALSE,nullptr);CK(e);
    HR(q->Signal(f.Get(),1));HR(f->SetEventOnCompletion(1,e));CK(WaitForSingleObject(e,20000)==WAIT_OBJECT_0&&f->GetCompletedValue()==1);CloseHandle(e);
    for(UINT b=0;b<5;b++){UINT*m;HR(rb[b]->Map(0,nullptr,(void**)&m));for(UINT i=0;i<expected[b].size();i++)if(m[i]!=expected[b][i]){
        printf("FAIL %s case=%u buffer=%u i=%u got=%x expected=%x\n","loadpass",test,b,i,m[i],expected[b][i]);return 1;}rb[b]->Unmap(0,nullptr);}
    printf("PASS loadpass case=%u fenced all five complete buffers\n",test);return 0;
}
using Create=decltype(&D3D12XboxCreateDevice);
static int verify(Create create, bool staged){
    const IID boot={0x5405c344,0xd457,0x444e,{0xb4,0xdd,0x23,0x66,0xe4,0x5a,0xee,0x39}};
    DWORD params[20]={};params[0]=0x20004;params[2]=0x100000;params[10]=1;params[15]=0x20;
    ComPtr<IUnknown>dev,other,root[3],ps;HR(create(nullptr,params,boot,(void **)dev.GetAddressOf()));HR(create(nullptr,params,boot,(void **)other.GetAddressOf()));
    using Root=HRESULT(WINAPI *)(IUnknown*,UINT,const void*,SIZE_T,REFIID,void**);using Name=HRESULT(WINAPI *)(IUnknown*,const WCHAR*);
    for(UINT z=0;z<3;z++){
        D3D12_ROOT_PARAMETER1 rp[7]={};const UINT p=z==1?2:3;
        for(UINT i=0;i<p;i++){rp[i].ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV;rp[i].Descriptor.ShaderRegister=i;}
        if(z==1){rp[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV;rp[1].Descriptor.ShaderRegister=0;}
        rp[p].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;rp[p].Constants.Num32BitValues=z?16:10;
        if(z==2) for(UINT i=0;i<2;i++){
            rp[i]={};rp[i].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;rp[i].Constants={1-i,0,2};
            rp[i+2]={};rp[i+2].ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV;rp[i+2].Descriptor.ShaderRegister=i;
        }
        D3D12_VERSIONED_ROOT_SIGNATURE_DESC sig={};sig.Version=D3D_ROOT_SIGNATURE_VERSION_1_1;sig.Desc_1_1={p+1,rp,0,nullptr,(D3D12_ROOT_SIGNATURE_FLAGS)0x03000000};
        ComPtr<ID3DBlob>blob;HR(D3D12SerializeVersionedRootSignature(&sig,&blob,nullptr));auto n=blob->GetBufferSize();CK(n==(z==1?140:164));
        auto make=[&](UINT node,const void*b){return SLOT(dev,0x80,Root)(dev.Get(),node,b,n,IID_ID3D12RootSignature,(void**)root[z].GetAddressOf());};
        CK(make(0,blob->GetBufferPointer())==E_NOTIMPL&&!root[z]);std::vector<BYTE>bytes(n);memcpy(bytes.data(),blob->GetBufferPointer(),n);bytes.back()^=1;
        CK(make(1,bytes.data())==E_NOTIMPL&&!root[z]);
        rp[1].Descriptor.RegisterSpace=1;ComPtr<ID3DBlob>other_blob;HR(D3D12SerializeVersionedRootSignature(&sig,&other_blob,nullptr));
        CK(other_blob->GetBufferSize()==n&&make(1,other_blob->GetBufferPointer())==E_NOTIMPL&&!root[z]);
        HR(make(1,blob->GetBufferPointer()));
        const WCHAR *name=!z?L"XDXR LoadInstancesPass":z==1?L"XDXR UtilityShaders":L"XDXR BitonicSort";WCHAR wrong[32]={};
        CK(SLOT(root[z],0x30,Name)(root[z].Get(),wrong)==E_NOTIMPL);HR(SLOT(root[z],0x30,Name)(root[z].Get(),name));
        if(!staged) CK(!xs_bvh_root_host(other.Get(),root[z].Get())&&xs_bvh_root_host(dev.Get(),root[z].Get()));
        printf("PASS root kind=%u bytes=%zu exact/owner/name\n",z,n);
    }
    ComPtr<ID3D12Device>d;if(!staged) HR(xs_bvh_root_host(dev.Get(),root[0].Get())->GetDevice(IID_PPV_ARGS(&d)));
    {
        std::vector<BYTE>code(0x1230c);FILE*f=fopen("original.bin","rb");CK(f&&fread(code.data(),1,code.size(),f)==code.size()&&fgetc(f)==EOF&&!ferror(f));fclose(f);
        struct Wire{IUnknown *root;const void *cs;SIZE_T size;BYTE tail[32];}w={root[0].Get(),code.data(),code.size(),{}};
        using Make=HRESULT(WINAPI *)(IUnknown*,const Wire*,UINT64,UINT64,REFIID,void**);
        auto make=[&](ComPtr<IUnknown>&o){return SLOT(o,0x1f0,Make)(o.Get(),&w,0,0,IID_ID3D12PipelineState,(void**)ps.GetAddressOf());};
        CK(make(other)==E_INVALIDARG&&!ps);for(UINT z:{1u,2u}){w.root=root[z].Get();CK(make(dev)==E_INVALIDARG&&!ps);}w.root=root[0].Get();
        w.size--;CK(make(dev)==E_NOTIMPL&&!ps);w.size++;w.tail[31]=1;CK(make(dev)==E_NOTIMPL&&!ps);w.tail[31]=0;
        code.back()^=1;CK(make(dev)==E_NOTIMPL&&!ps);code.back()^=1;
        if(!GetProcAddress(GetModuleHandleW(L"d3d12core.dll"),"ScarlettBoundedLoadPassV1")){CK(make(dev)==E_NOTIMPL&&!ps);puts("PASS load pass missing core rejected");return 0;}
        // The exact guest root alone lacks both authorized windows. Never dispatch it.
        if(!staged){
            D3D12_COMPUTE_PIPELINE_STATE_DESC pd={};pd.pRootSignature=xs_bvh_root_host(dev.Get(),root[0].Get());pd.CS={code.data(),code.size()};
            ComPtr<ID3D12PipelineState>bad;CK(FAILED(d->CreateComputePipelineState(&pd,IID_PPV_ARGS(&bad)))&&!bad);
            puts("PASS compiler declined exact guest shader without private windows");
        }
        HR(make(dev));
        WCHAR wrong[32]={};CK(SLOT(ps,0x30,Name)(ps.Get(),wrong)==E_NOTIMPL);
        HR(SLOT(ps,0x30,Name)(ps.Get(),L"XDXR LoadInstancesPass"));
        if(staged){puts("PASS staged DLL load-pass factory");return 0;}
        puts("PASS factory +1f0 owner/schema/digest/size/tail/name");
        for(UINT test=0;test<8;test++)CK(!exercise(d.Get(),ps.Get(),test));
        ps.Reset();
    }
    return 0;
}
int main(){
    HMODULE dll=LoadLibraryW(L"d3d12_xs.dll");CK(dll);FARPROC fn=GetProcAddress(dll,"D3D12XboxCreateDevice");Create create;
    CK(fn);memcpy(&create,&fn,sizeof create);CK(!verify(create,true));
    return verify(D3D12XboxCreateDevice,false);
}
