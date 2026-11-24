#include <d3d12.h>
#include <cstdio>
#include <cfloat>
#include <algorithm>
#include <cmath>
#include <array>
#include <vector>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
extern "C" HRESULT WINAPI D3D12XboxCreateDevice(void *,const void *,REFIID,void **);
extern "C" ID3D12PipelineState *test_apex_host(IUnknown *,ID3D12RootSignature **);
#define SLOT(o,n,T) ((T)((*reinterpret_cast<void***>(o))[n/8]))
#define CK(x) do { if (!(x)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); return 1; } } while(0)
#define HR(x) do {HRESULT hr_=(x);if(FAILED(hr_)){fprintf(stderr,"FAIL %d: %s hr=%08lx\n",__LINE__,#x,hr_);return 1;}}while(0)
static const char *names[]={"ResetKDOP","BuildKDOP","BuildApexPointMap"};
using V=std::array<double,3>;
static V direction(UINT face,UINT x,UINT y,UINT k){
    double u=2.*x/k-1,v=2.*y/k-1,length=sqrt(1+u*u+v*v);
    V d=face==0?V{1,u,v}:face==1?V{-v,1,u}:V{-u,-v,1};for(auto &a:d)a/=length;return d;
}
static double dot(V a,V b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
static UINT bits(float f){UINT u;memcpy(&u,&f,4);return u;}
static float fp(UINT u){float f;memcpy(&f,&u,4);return f;}
using R=ComPtr<ID3D12Resource>;
static int probe(UINT stage,ID3D12PipelineState *ps,ID3D12RootSignature *root,UINT mode){
    ComPtr<ID3D12Device>d;HR(ps->GetDevice(IID_PPV_ARGS(&d)));
    const UINT section=3072,total=section*4,kdop=section/4,apex=2*kdop;
    std::vector<UINT> seed(total/4),expected;for(UINT i=0;i<seed.size();i++)seed[i]=0x53ba1000+17*i;
    UINT k=2,table=3*(k+1)*(k+1),count=stage==0?37:stage==1?2:3*k*k;
    if((stage!=1&&mode==1)||(stage==1&&mode==4))count=0;
    UINT kdopCap=stage==0&&mode==2?17:48,apexCap=stage==2&&mode==2?6*k*k+3:12*k*k;
    const UINT64 hdr=0x35ffffff00ULL,atom=0x4a00040000ULL,leaf=hdr+256;
    UINT c[]={UINT(hdr>>8),UINT(atom>>8),k,count,UINT(leaf),UINT(leaf>>32)};
    UINT win[]={UINT(hdr),UINT(hdr>>32),512,UINT(atom),UINT(atom>>32),table*8};
    UINT input[128]={};input[3]=count;input[11]=0;input[13]=256;
    // Two records: triangle at +0 and box at +16. On header-selected jobs include a fourth triangle vertex.
    const V tri[]={{-2,4,1},{3,1,6},{5,-1,3},{-9,8,12}};V lo={-3,-2,-4},hi={2,6,7};
    for(UINT i=0;i<4;i++)for(UINT j=0;j<3;j++)input[64+3*i+j]=bits(float(tri[i][j]));
    input[64+15]=1;
    for(UINT j=0;j<3;j++){input[80+4+j]=bits(float(lo[j]));input[80+7+j]=bits(float(hi[j]));}
    if(stage==1){
        if(mode==1){c[3]=1;c[4]=c[5]=0;input[11]=1;input[64+15]=256;}
        if(mode==2){win[2]=256+64+40-1;hi={0,0,0};}
        if(mode==3)win[5]--; // The last fmin is one byte short, but its fmax still fits.
        for(UINT i=0;i<table;i++){seed[kdop+i*2]=bits(-FLT_MAX);seed[kdop+i*2+1]=bits(FLT_MAX);}
    }
    if(stage==2)for(UINT i=0;i<table;i++)for(UINT side=0;side<2;side++)seed[kdop+2*i+side]=bits(side?float(-7-int(i%9)):float(3+((7*i)%13)));
    expected=seed;std::vector<bool> approx(seed.size());
    // For map outputs use three independent plane equations per float3 instead of reproducing the shader's intersection solver.
    struct Planes{UINT word;V normals[3];double offsets[3];};std::vector<Planes> checks;
    if(!stage){for(UINT i=0;i<std::min(count,kdopCap);i++){expected[kdop+2*i]=bits(-FLT_MAX);expected[kdop+2*i+1]=bits(FLT_MAX);}}
    else if(stage==1 && count){
        for(UINT f=0,i=0;f<3;f++)for(UINT y=0;y<=k;y++)for(UINT x=0;x<=k;x++,i++){
            auto n=direction(f,x,y,k);double mx=-DBL_MAX,mn=DBL_MAX;
            auto point=[&](V v){double t=dot(v,n);mx=std::max(mx,t);mn=std::min(mn,t);};
            for(UINT j=0;j<(mode==1?4u:3u);j++)point(tri[j]);
            for(UINT j=0;j<8;j++)point({j&1?hi[0]:lo[0],j&2?hi[1]:lo[1],j&4?hi[2]:lo[2]});
            for(UINT side=0;side<2;side++)if((2*i+side+1)*4<=win[5]){UINT w=kdop+2*i+side;double t=side?mn:mx;
                expected[w]=bits(float(t*((side?t<0:t>0)?1.00001:1.)));approx[w]=true;}
        }
    }else if(stage==2){
        for(UINT i=0;i<count;i++)for(UINT side=0;side<2;side++)for(UINT half=0;half<2;half++){
            UINT dest=side*6*k*k+2*i+half;if(dest>=apexCap)continue;
            Planes p={apex+3*dest,{},{}};
            for(UINT j=0;j<3;j++){UINT corner=j+half,x=i%k+(corner&1),y=(i/k)%k+(corner>>1),f=i/(k*k);
                p.normals[j]=direction(f,x,y,k);p.offsets[j]=fp(seed[kdop+2*((f*(k+1)+y)*(k+1)+x)+side]);approx[p.word+j]=true;}
            checks.push_back(p);
        }
    }
    auto buffer=[&](R&r,UINT bytes,D3D12_HEAP_TYPE type){D3D12_HEAP_PROPERTIES hp={};hp.Type=type;hp.CreationNodeMask=hp.VisibleNodeMask=1;
        D3D12_RESOURCE_DESC rd={};rd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;rd.Width=bytes;rd.Height=rd.DepthOrArraySize=rd.MipLevels=rd.SampleDesc.Count=1;
        rd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;if(type==D3D12_HEAP_TYPE_DEFAULT)rd.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        return d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,type==D3D12_HEAP_TYPE_UPLOAD?D3D12_RESOURCE_STATE_GENERIC_READ:D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&r));};
    R upload,arena,rb,h;HR(buffer(upload,total,D3D12_HEAP_TYPE_UPLOAD));HR(buffer(arena,total,D3D12_HEAP_TYPE_DEFAULT));
    HR(buffer(rb,total,D3D12_HEAP_TYPE_READBACK));HR(buffer(h,768,D3D12_HEAP_TYPE_UPLOAD));void*m;
    HR(upload->Map(0,nullptr,&m));memcpy(m,seed.data(),total);upload->Unmap(0,nullptr);
    HR(h->Map(0,nullptr,&m));memcpy(m,input,sizeof input);memcpy((BYTE*)m+512,c,sizeof c);h->Unmap(0,nullptr);
    D3D12_DESCRIPTOR_HEAP_DESC hd={D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,2,D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,1};
    ComPtr<ID3D12DescriptorHeap>heap;HR(d->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&heap)));auto cpu=heap->GetCPUDescriptorHandleForHeapStart();
    for(UINT i=0;i<2;i++){D3D12_UNORDERED_ACCESS_VIEW_DESC v={};v.ViewDimension=D3D12_UAV_DIMENSION_BUFFER;
        UINT stride=i?12:8;v.Buffer.FirstElement=(i+1)*section/stride;v.Buffer.NumElements=i?apexCap:kdopCap;v.Buffer.StructureByteStride=stride;
        d->CreateUnorderedAccessView(arena.Get(),nullptr,&v,cpu);cpu.ptr+=d->GetDescriptorHandleIncrementSize(hd.Type);}
    D3D12_COMMAND_QUEUE_DESC qd={};qd.Type=D3D12_COMMAND_LIST_TYPE_COMPUTE;ComPtr<ID3D12CommandQueue>q;HR(d->CreateCommandQueue(&qd,IID_PPV_ARGS(&q)));
    ComPtr<ID3D12CommandAllocator>al;ComPtr<ID3D12GraphicsCommandList>l;HR(d->CreateCommandAllocator(qd.Type,IID_PPV_ARGS(&al)));
    HR(d->CreateCommandList(1,qd.Type,al.Get(),ps,IID_PPV_ARGS(&l)));l->CopyResource(arena.Get(),upload.Get());
    auto trans=[&](D3D12_RESOURCE_STATES from,D3D12_RESOURCE_STATES to){D3D12_RESOURCE_BARRIER b={};b.Transition={arena.Get(),0,from,to};l->ResourceBarrier(1,&b);};
    trans(D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);l->SetComputeRootSignature(root);
    if(stage==1){l->SetComputeRootConstantBufferView(0,h->GetGPUVirtualAddress()+512);l->SetComputeRoot32BitConstants(1,6,win,0);
        l->SetComputeRootShaderResourceView(2,h->GetGPUVirtualAddress());l->SetComputeRootUnorderedAccessView(3,arena->GetGPUVirtualAddress()+section);}
    else{l->SetDescriptorHeaps(1,heap.GetAddressOf());l->SetComputeRootDescriptorTable(0,heap->GetGPUDescriptorHandleForHeapStart());l->SetComputeRoot32BitConstants(1,6,c,0);}
    l->Dispatch(2,1,1);trans(D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);l->CopyResource(rb.Get(),arena.Get());HR(l->Close());
    ID3D12CommandList*lists[]={l.Get()};q->ExecuteCommandLists(1,lists);ComPtr<ID3D12Fence>f;HR(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&f)));
    HANDLE e=CreateEventW(nullptr,FALSE,FALSE,nullptr);CK(e);HR(q->Signal(f.Get(),1));HR(f->SetEventOnCompletion(1,e));CK(WaitForSingleObject(e,15000)==WAIT_OBJECT_0&&f->GetCompletedValue()==1);CloseHandle(e);
    HR(rb->Map(0,nullptr,&m));auto out=(const UINT*)m;
    for(UINT i=0;i<expected.size();i++){
        if(!approx[i]?out[i]!=expected[i]:stage==1&&!(fabs(fp(out[i])-fp(expected[i]))<2e-5)){
            fprintf(stderr,"%s case %u word %u got %08x (%g) expected %08x (%g)\n",names[stage],mode,i,out[i],fp(out[i]),expected[i],fp(expected[i]));return 1;}}
    for(auto &p:checks){V v={fp(out[p.word]),fp(out[p.word+1]),fp(out[p.word+2])};
        for(UINT j=0;j<3;j++)CK(fabs(dot(v,p.normals[j])-p.offsets[j])<3e-5);}
    rb->Unmap(0,nullptr);HR(h->Map(0,nullptr,&m));CK(!memcmp(m,input,sizeof input)&&!memcmp((BYTE*)m+512,c,sizeof c));h->Unmap(0,nullptr);
    printf("PASS %s case %u: %u fenced bytes including every guard\n",names[stage],mode,total);return 0;
}
using Create=decltype(&D3D12XboxCreateDevice);
extern "C" ID3D12RootSignature *xs_bvh_root_host(IUnknown *,IUnknown *);
static int verify(Create create,bool staged){
    DWORD p[20]={};p[0]=0x20004;p[2]=0x100000;p[10]=1;p[15]=0x20;
    const IID boot={0x5405c344,0xd457,0x444e,{0xb4,0xdd,0x23,0x66,0xe4,0x5a,0xee,0x39}};ComPtr<IUnknown> dev,other;HR(create(nullptr,p,boot,(void**)dev.GetAddressOf()));HR(create(nullptr,p,boot,(void**)other.GetAddressOf()));
    using Root=HRESULT(WINAPI *)(IUnknown*,UINT,const void*,SIZE_T,REFIID,void**);
    D3D12_ROOT_PARAMETER1 params[3]={};D3D12_VERSIONED_ROOT_SIGNATURE_DESC desc={};desc.Version=D3D_ROOT_SIGNATURE_VERSION_1_1;
    auto makeRoot=[&](UINT n,IUnknown**out,ID3DBlob**blob){desc.Desc_1_1={n,params,0,nullptr,(D3D12_ROOT_SIGNATURE_FLAGS)0x03000000};HRESULT hr=D3D12SerializeVersionedRootSignature(&desc,blob,nullptr);if(FAILED(hr))return hr;return SLOT(dev.Get(),0x80,Root)(dev.Get(),1,(*blob)->GetBufferPointer(),(*blob)->GetBufferSize(),IID_ID3D12RootSignature,(void**)out);};
    ComPtr<IUnknown> right,wrong;ComPtr<ID3DBlob> sig,util;
    for(unsigned i=0;i<2;i++){params[i].ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV;params[i].Descriptor.ShaderRegister=i;}
    params[2].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;params[2].Constants={0,0,6};HR(makeRoot(3,&right,&sig));
    params[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV;params[1].Descriptor.ShaderRegister=0;params[2].Constants.Num32BitValues=16;HR(makeRoot(3,&wrong,&util));
    using Name=HRESULT(WINAPI*)(IUnknown*,const WCHAR*);HR(SLOT(right.Get(),0x30,Name)(right.Get(),L"XDXR ApexPointMapComputer"));
    const unsigned sizes[]={0x10d0,0x1d5c,0x1ce0};const WCHAR *labels[]={L"XDXR ResetKDOP",L"XDXR BuildKDOP",L"XDXR BuildApexPointMap"};
    for(unsigned stage=0;stage<3;stage++){
        char file[64];snprintf(file,sizeof file,"%s.dxbc",names[stage]);FILE*f=fopen(file,"rb");CK(f);std::vector<BYTE>s(sizes[stage]);CK(fread(s.data(),1,s.size(),f)==s.size()&&fgetc(f)==EOF&&!ferror(f));fclose(f);
        CK(sig->GetBufferSize()==140&&!memcmp(s.data()+(stage==2?0x114:stage?0xe4:0xfc),(BYTE*)sig->GetBufferPointer()+36,104));
        struct Wire{IUnknown*r;const void*cs;SIZE_T n;BYTE tail[32];} wire={wrong.Get(),s.data(),s.size(),{}};using Make=HRESULT(WINAPI*)(IUnknown*,const Wire*,UINT64,UINT64,REFIID,void**);void*out;
        auto make=[&](){out=dev.Get();return SLOT(dev.Get(),0x1f0,Make)(dev.Get(),&wire,0,0,IID_ID3D12PipelineState,&out);};
        CK(make()==E_INVALIDARG&&!out);wire.r=right.Get();s.back()^=1;CK(make()==E_NOTIMPL&&!out);s.back()^=1;
        out=dev.Get();CK(SLOT(other.Get(),0x1f0,Make)(other.Get(),&wire,0,0,IID_ID3D12PipelineState,&out)==E_INVALIDARG&&!out);
        wire.tail[31]=1;CK(make()==E_NOTIMPL&&!out);wire.tail[31]=0;wire.n--;CK(make()==E_NOTIMPL&&!out);wire.n++;
        if(stage&&!GetProcAddress(GetModuleHandleW(L"d3d12core.dll"),"ScarlettBoundedApexV1")){CK(make()==E_NOTIMPL&&!out);printf("PASS %s missing backend rejected\n",names[stage]);continue;}
        if(stage==1&&!staged){
            auto guestRoot=xs_bvh_root_host(dev.Get(),right.Get());ComPtr<ID3D12Device>d;HR(guestRoot->GetDevice(IID_PPV_ARGS(&d)));
            D3D12_COMPUTE_PIPELINE_STATE_DESC pd={};pd.pRootSignature=guestRoot;pd.CS={s.data(),s.size()};ComPtr<ID3D12PipelineState>bad;
            CK(FAILED(d->CreateComputePipelineState(&pd,IID_PPV_ARGS(&bad)))&&!bad);
            puts("PASS BuildKDOP without private windows rejected by compiler");
        }
        HR(make());ComPtr<IUnknown>ps;ps.Attach((IUnknown*)out);WCHAR badName[32]={};CK(SLOT(out,0x30,Name)(ps.Get(),badName)==E_NOTIMPL);HR(SLOT(out,0x30,Name)(ps.Get(),labels[stage]));printf("PASS %s factory %s root/owner/digest/size/tail/name\n",staged?"staged":"linked",names[stage]);
        if(staged)continue;
        ID3D12RootSignature*r=nullptr;auto host=test_apex_host(ps.Get(),&r);CK(r&&host);for(unsigned mode=0;mode<(stage==1?5u:3u);mode++)CK(!probe(stage,host,r,mode));
    }return 0;
}
int main(){
    HMODULE dll=LoadLibraryW(L"d3d12_xs.dll");CK(dll);FARPROC fn=GetProcAddress(dll,"D3D12XboxCreateDevice");CK(fn);
    Create create;memcpy(&create,&fn,sizeof create);CK(!verify(create,true));return verify(D3D12XboxCreateDevice,false);
}
