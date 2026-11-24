#include <d3d12.h>
#include <cstdio>
#include <cfloat>
#include <vector>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
extern "C" HRESULT WINAPI D3D12XboxCreateDevice(void *,const void *,REFIID,void **);
extern "C" ID3D12PipelineState *test_bvh4_host(IUnknown *,ID3D12RootSignature **);
#define SLOT(o,n,T) ((T)((*reinterpret_cast<void***>(o))[n/8]))
#define CK(x) do { if (!(x)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); return 1; } } while(0)
#define HR(x) do {HRESULT hr_=(x);if(FAILED(hr_)){fprintf(stderr,"FAIL %d: %s hr=%08lx\n",__LINE__,#x,hr_);return 1;}}while(0)
static const char *names[]={"PartitionConstruct","Construct","Refit"};
static int probe(unsigned stage,ID3D12PipelineState *ps,ID3D12RootSignature *root,unsigned mode)
{
    ComPtr<ID3D12Device>d; HR(root->GetDevice(IID_PPV_ARGS(&d)));
    // Independent guards at both ends of each view, plus gaps left untouched by the shader.
    constexpr unsigned total=12288;
    const unsigned off[]={768,2304,3072,3840,4608,5376,6144,6912,8448};
    const unsigned size[]={1024,64,64,128,64,64,192,128};
    const unsigned stride[]={0,0,4,8,4,0,24,4};
    UINT exp[total/4]; for(unsigned i=0;i<total/4;i++) exp[i]=0x7a550591u+i*977;
    auto w=[&](unsigned u){return exp+off[u]/4;};
    UINT *h=w(0); UINT k[]={0,0,0};
    float a[]={1,2,3,5,6,7},b[]={-1,-2,-3,2,3,4},empty[]={FLT_MAX,FLT_MAX,FLT_MAX,-FLT_MAX,-FLT_MAX,-FLT_MAX};
    h[0]=0; h[3]=2; h[10]=640; h[13]=256;h[16]=1;h[17]=4;h[20]=2;h[34]=0;h[35]=1;
    w(1)[0]=0;w(2)[1]=w(2)[2]=0;w(3)[0]=1;w(3)[1]=2;
    memcpy(w(6)+6,a,24);memcpy(w(6)+12,b,24);
    for(unsigned i=0;i<32;i++)w(7)[i]=i<2?1-i:~0u;
    const UINT64 guest=0x24ffffff00ull;
    const UINT bytes[]={308,304,288,296,268,296}; // whole, scalar miss, vec3 miss, box32, vec4 miss
    const bool apex=stage==0 && mode==1;
    if(apex){
        k[2]=~1u;h[10]=512;h[17]=1;h[19]=1;h[27]=1;h[35]=0;
    }
    if(stage==1){
        // Box32, with a nonzero allocator base and a 2x normalization on all axes.
        k[0]=6;w(5)[0]=128;
        const float scene[]={-16384,-16384,-16384,16384,16384,16384};memcpy(h+4,scene,sizeof scene);
    }
    if(stage==2){
        h[3]=1; h[14]=896; h[224]=~0u;
        const float matrix[]={1,0,0,2,0,1,0,4,0,0,1,8};memcpy(h+64,matrix,sizeof matrix);
        h[64+14]=mode<3?32:37;h[64+15]=(UINT)(guest>>8);h[64+30]=0;
        UINT *src=w(8);src[0]=0;
        if(mode==5){
            h[224]=85;h[230]=~0u;w(1)[6]=0;
            h[160]=38;h[161]=h[162]=h[163]=~0u;
            for(unsigned i=1;i<4;i++)memcpy(h+164+6*i,empty,24);
        }
        if(mode<3){const float vertices[]={1,2,3,-2,6,1,7,4,5,11,10,13};memcpy(src+64,vertices,sizeof vertices);src[76]=128;}
        else {src[64]=7;src[65]=src[66]=src[67]=~0u;memcpy(src+68,a,sizeof a);}
    }
    auto buffer=[&](D3D12_HEAP_TYPE type,ID3D12Resource **out){
        D3D12_HEAP_PROPERTIES hp={};hp.Type=type;hp.CreationNodeMask=hp.VisibleNodeMask=1;
        D3D12_RESOURCE_DESC rd={};rd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;rd.Width=total;
        rd.Height=rd.DepthOrArraySize=rd.MipLevels=rd.SampleDesc.Count=1;rd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if(type==D3D12_HEAP_TYPE_DEFAULT)rd.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        auto state=type==D3D12_HEAP_TYPE_UPLOAD?D3D12_RESOURCE_STATE_GENERIC_READ:D3D12_RESOURCE_STATE_COPY_DEST;
        return d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,state,nullptr,IID_PPV_ARGS(out));
    };
    ComPtr<ID3D12Resource> dst,rb,seed;HR(buffer(D3D12_HEAP_TYPE_DEFAULT,&dst));HR(buffer(D3D12_HEAP_TYPE_READBACK,&rb));HR(buffer(D3D12_HEAP_TYPE_UPLOAD,&seed));
    void *map;HR(seed->Map(0,nullptr,&map));memcpy(map,exp,total);seed->Unmap(0,nullptr);
    D3D12_DESCRIPTOR_HEAP_DESC hd={};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;hd.NumDescriptors=8;hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ComPtr<ID3D12DescriptorHeap> heap;HR(d->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&heap)));auto cpu=heap->GetCPUDescriptorHandleForHeapStart();
    for(unsigned u=0;u<(stage==2?2u:8u);u++){
        D3D12_UNORDERED_ACCESS_VIEW_DESC v={};v.ViewDimension=D3D12_UAV_DIMENSION_BUFFER;
        if(!stride[u]){v.Format=DXGI_FORMAT_R32_TYPELESS;v.Buffer.Flags=D3D12_BUFFER_UAV_FLAG_RAW;}
        v.Buffer.StructureByteStride=stride[u];unsigned elem=stride[u]?stride[u]:4;
        v.Buffer.FirstElement=off[u]/elem;v.Buffer.NumElements=size[u]/elem;
        d->CreateUnorderedAccessView(dst.Get(),nullptr,&v,cpu);cpu.ptr+=d->GetDescriptorHandleIncrementSize(hd.Type);
    }
    D3D12_COMMAND_QUEUE_DESC qd={};qd.Type=D3D12_COMMAND_LIST_TYPE_COMPUTE;ComPtr<ID3D12CommandQueue> queue;HR(d->CreateCommandQueue(&qd,IID_PPV_ARGS(&queue)));
    ComPtr<ID3D12CommandAllocator> alloc;HR(d->CreateCommandAllocator(qd.Type,IID_PPV_ARGS(&alloc)));
    ComPtr<ID3D12GraphicsCommandList> list;HR(d->CreateCommandList(1,qd.Type,alloc.Get(),ps,IID_PPV_ARGS(&list)));
    list->CopyResource(dst.Get(),seed.Get());D3D12_RESOURCE_BARRIER tr={};tr.Transition={dst.Get(),0,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_UNORDERED_ACCESS};list->ResourceBarrier(1,&tr);
    ID3D12DescriptorHeap *heaps[]={heap.Get()};list->SetDescriptorHeaps(1,heaps);list->SetComputeRootSignature(root);
    list->SetComputeRootDescriptorTable(0,heap->GetGPUDescriptorHandleForHeapStart());list->SetComputeRoot32BitConstants(1,3,k,0);
    if(stage==2){
        UINT range[]={(UINT)guest,(UINT)(guest>>32),bytes[mode]};
        list->SetComputeRoot32BitConstants(2,3,range,0);
        list->SetComputeRootShaderResourceView(3,seed->GetGPUVirtualAddress()+off[8]);
    }
    list->Dispatch(1,1,1);tr.Transition.StateBefore=D3D12_RESOURCE_STATE_UNORDERED_ACCESS;tr.Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_SOURCE;list->ResourceBarrier(1,&tr);
    list->CopyResource(rb.Get(),dst.Get());HR(list->Close());ID3D12CommandList *lists[]={list.Get()};queue->ExecuteCommandLists(1,lists);
    ComPtr<ID3D12Fence> fence;HR(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));
    HANDLE ev=CreateEventW(nullptr,FALSE,FALSE,nullptr);CK(ev);HR(queue->Signal(fence.Get(),1));HR(fence->SetEventOnCompletion(1,ev));CK(WaitForSingleObject(ev,10000)==WAIT_OBJECT_0);CloseHandle(ev);CK(fence->GetCompletedValue()==1);
    if(stage<2){
    if(stage==1){
        for(unsigned i=0;i<6;i++){a[i]*=2;b[i]*=2;}
        memcpy(w(6)+6,a,24);memcpy(w(6)+12,b,24);w(5)[0]=256;h[10]=512;h[15]=0x80808080u;
    }
    UINT parent=85;h[1]=stage?11:13;h[12]=parent;h[160]=apex?69:54;h[161]=~0u;h[162]=38;h[163]=~0u;
    memcpy(h+164,a,24);memcpy(h+170,empty,24);memcpy(h+176,b,24);memcpy(h+182,empty,24);
    h[(256+116)/4]=parent;if(!apex)h[(384+116)/4]=parent;w(1)[0]=2;w(2)[1]=apex?69:54;w(2)[2]=38;
    for(unsigned i=2;i<6;i++) w(3)[i]=~0u;
    w(4)[0]=w(4)[apex?4:2]=parent;w(4)[6]=~0u;memcpy(w(6),b,12);memcpy(w(6)+3,a+3,12);
    }else{
        // Translate the triangle or box. A vector extending beyond the range is ALL zero.
        const float boxes[][6]={{0,6,9,13,14,21},{0,6,9,9,10,13},{0,4,8,3,10,11},{3,6,11,7,10,15}};
        const float *want=mode==4?empty:boxes[mode==5?3:mode];
        memcpy(h+4,want,24);
        if(mode==5){memcpy(h+164,want,24);w(1)[6]=1;}
    }
    HR(rb->Map(0,nullptr,&map));unsigned misses=0;for(unsigned i=0;i<total/4;i++)if(((UINT*)map)[i]!=exp[i]){
        if(misses++<24) fprintf(stderr,"%s byte %u got %08x expected %08x\n",names[stage],4*i,((UINT*)map)[i],exp[i]);}
    rb->Unmap(0,nullptr);CK(!misses);printf("PASS %s case %u: %u fenced bytes including every guard\n",names[stage],mode,total);return 0;
}
using Create=decltype(&D3D12XboxCreateDevice);
extern "C" ID3D12RootSignature *xs_bvh_root_host(IUnknown *,IUnknown *);
static int verify(Create create,bool staged){
    DWORD p[20]={};p[0]=0x20004;p[2]=0x100000;p[10]=1;p[15]=0x20;
    const IID boot={0x5405c344,0xd457,0x444e,{0xb4,0xdd,0x23,0x66,0xe4,0x5a,0xee,0x39}};ComPtr<IUnknown> dev,other;HR(create(nullptr,p,boot,(void**)dev.GetAddressOf()));HR(create(nullptr,p,boot,(void**)other.GetAddressOf()));
    using Root=HRESULT(WINAPI *)(IUnknown*,UINT,const void*,SIZE_T,REFIID,void**);
    D3D12_ROOT_PARAMETER1 params[9]={};D3D12_VERSIONED_ROOT_SIGNATURE_DESC desc={};desc.Version=D3D_ROOT_SIGNATURE_VERSION_1_1;
    auto makeRoot=[&](UINT n,IUnknown**out,ID3DBlob**blob){desc.Desc_1_1={n,params,0,nullptr,(D3D12_ROOT_SIGNATURE_FLAGS)0x03000000};HRESULT hr=D3D12SerializeVersionedRootSignature(&desc,blob,nullptr);if(FAILED(hr))return hr;return SLOT(dev.Get(),0x80,Root)(dev.Get(),1,(*blob)->GetBufferPointer(),(*blob)->GetBufferSize(),IID_ID3D12RootSignature,(void**)out);};
    ComPtr<IUnknown> right,wrong;ComPtr<ID3DBlob> sig,util;
    for(unsigned i=0;i<8;i++){params[i].ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV;params[i].Descriptor.ShaderRegister=i;}
    params[8].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;params[8].Constants={0,0,3};HR(makeRoot(9,&right,&sig));
    params[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV;params[1].Descriptor.ShaderRegister=0;params[2]=params[8];params[2].Constants.Num32BitValues=16;HR(makeRoot(3,&wrong,&util));
    using Name=HRESULT(WINAPI*)(IUnknown*,const WCHAR*);HR(SLOT(right.Get(),0x30,Name)(right.Get(),L"XDXR BVH4Builder"));
    const unsigned sizes[]={0x5e60,0x7160,0x84e4};const WCHAR *labels[]={L"XDXR PartitionConstruct",L"XDXR Construct",L"XDXR Refit"};
    for(unsigned stage=0;stage<3;stage++){
        char file[64];snprintf(file,sizeof file,"%s.bin",names[stage]);FILE*f=fopen(file,"rb");CK(f);std::vector<BYTE>s(sizes[stage]);CK(fread(s.data(),1,s.size(),f)==s.size()&&fgetc(f)==EOF&&!ferror(f));fclose(f);
        CK(sig->GetBufferSize()==284&&!memcmp(s.data()+(stage==2?0x114:0x1a4),(BYTE*)sig->GetBufferPointer()+36,248));
        struct Wire{IUnknown*r;const void*cs;SIZE_T n;BYTE tail[32];} wire={wrong.Get(),s.data(),s.size(),{}};using Make=HRESULT(WINAPI*)(IUnknown*,const Wire*,UINT64,UINT64,REFIID,void**);void*out;
        auto make=[&](){out=dev.Get();return SLOT(dev.Get(),0x1f0,Make)(dev.Get(),&wire,0,0,IID_ID3D12PipelineState,&out);};
        CK(make()==E_INVALIDARG&&!out);wire.r=right.Get();s.back()^=1;CK(make()==E_NOTIMPL&&!out);s.back()^=1;
        out=dev.Get();CK(SLOT(other.Get(),0x1f0,Make)(other.Get(),&wire,0,0,IID_ID3D12PipelineState,&out)==E_INVALIDARG&&!out);
        wire.tail[31]=1;CK(make()==E_NOTIMPL&&!out);wire.tail[31]=0;wire.n--;CK(make()==E_NOTIMPL&&!out);wire.n++;
        if(stage&&!GetProcAddress(GetModuleHandleW(L"d3d12core.dll"),"ScarlettBoundedBVH4V1")){CK(make()==E_NOTIMPL&&!out);printf("PASS %s missing backend rejected\n",names[stage]);continue;}
        if(stage==2&&!staged){
            auto guestRoot=xs_bvh_root_host(dev.Get(),right.Get());ComPtr<ID3D12Device>d;HR(guestRoot->GetDevice(IID_PPV_ARGS(&d)));
            D3D12_COMPUTE_PIPELINE_STATE_DESC pd={};pd.pRootSignature=guestRoot;pd.CS={s.data(),s.size()};ComPtr<ID3D12PipelineState>bad;
            CK(FAILED(d->CreateComputePipelineState(&pd,IID_PPV_ARGS(&bad)))&&!bad);
            puts("PASS Refit without private window rejected by compiler");
        }
        HR(make());ComPtr<IUnknown>ps;ps.Attach((IUnknown*)out);HR(SLOT(out,0x30,Name)(ps.Get(),labels[stage]));printf("PASS %s factory %s root/owner/digest/size/tail/name\n",staged?"staged":"linked",names[stage]);
        if(staged)continue;
        ID3D12RootSignature*r=nullptr;auto host=test_bvh4_host(ps.Get(),&r);CK(r&&host);for(unsigned mode=0;mode<(stage==2?6u:stage==0?2u:1u);mode++)CK(!probe(stage,host,r,mode));
    }return 0;
}
int main(){
    HMODULE dll=LoadLibraryW(L"d3d12_xs.dll");CK(dll);FARPROC fn=GetProcAddress(dll,"D3D12XboxCreateDevice");CK(fn);
    Create create;memcpy(&create,&fn,sizeof create);CK(!verify(create,true));return verify(D3D12XboxCreateDevice,false);
}
