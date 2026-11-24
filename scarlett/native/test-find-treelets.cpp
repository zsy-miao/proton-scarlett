#include <d3d12.h>
#include <cstdio>
#include <algorithm>
#include <vector>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
extern "C" HRESULT WINAPI D3D12XboxCreateDevice(void *,const void *,REFIID,void **);
extern "C" ID3D12PipelineState *test_treelets_host(IUnknown *,ID3D12RootSignature **);
#define SLOT(o,n,T) ((T)((*reinterpret_cast<void***>(o))[n/8]))
#define CK(x) do { if (!(x)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); return 1; } } while(0)
#define HR(x) do {HRESULT hr_=(x);if(FAILED(hr_)){fprintf(stderr,"FAIL %d: %s hr=%08lx\n",__LINE__,#x,hr_);return 1;}}while(0)
static int probe(ID3D12PipelineState *ps,ID3D12RootSignature *root,unsigned mode)
{
    ComPtr<ID3D12Device>d; HR(root->GetDevice(IID_PPV_ARGS(&d)));
    // Independent guards at both ends of each view, plus gaps left untouched by the shader.
    constexpr unsigned total=8192;
    const unsigned off[]={768,1536,2304,3072,4096,5120}; // last two: guest CBV and header snapshot
    const unsigned stride[]={4,8,0,24};
    UINT exp[total/4]; for(unsigned i=0;i<total/4;i++) exp[i]=0x7a550591u+i*977;
    auto w=[&](unsigned u){return exp+off[u]/4;};
    const unsigned n=(mode==0||mode==2)?4:7, internal=n-1;
    const UINT size[]={4*(2*n-1),8*internal,4*internal,24*(2*n-1)};
    const UINT sx4[]={0,100,2,105},sx7[]={0,100,40,3,110,44,9};
    const UINT *sx=n==4?sx4:sx7;
    for(unsigned i=0;i<internal;i++){
        w(1)[2*i]=2*i+1;w(1)[2*i+1]=2*i+2;w(0)[2*i+1]=w(0)[2*i+2]=i;w(2)[i]=0;
    }
    for(unsigned i=0;i<n;i++){
        float box[]={(float)sx[i],0,0,(float)(sx[i]+1),1,1};memcpy(w(3)+6*(internal+i),box,24);
    }
    const UINT64 guest=0x22fffffff8ull;
    UINT k[]={(UINT)guest,(UINT)(guest>>32),n,mode==5?0u:10u,(mode==1||mode==3)?2u:mode==4?~1u:~0u};
    memcpy(w(4),k,sizeof k);w(5)[3]=n;w(5)[16]=2;w(5)[17]=17;w(5)[19]=5;w(5)[34]=mode==4?n-2:n;
    const UINT range[]={(UINT)guest,(UINT)(guest>>32),mode==2?15u:mode==3?12u:144u};
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
    D3D12_DESCRIPTOR_HEAP_DESC hd={};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;hd.NumDescriptors=4;hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ComPtr<ID3D12DescriptorHeap> heap;HR(d->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&heap)));auto cpu=heap->GetCPUDescriptorHandleForHeapStart();
    for(unsigned u=0;u<4;u++){
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
    list->SetComputeRootDescriptorTable(0,heap->GetGPUDescriptorHandleForHeapStart());
    list->SetComputeRootConstantBufferView(1,seed->GetGPUVirtualAddress()+off[4]);
    list->SetComputeRoot32BitConstants(2,3,range,0);
    list->SetComputeRootShaderResourceView(3,seed->GetGPUVirtualAddress()+off[5]);
    list->Dispatch(1,1,1);tr.Transition.StateBefore=D3D12_RESOURCE_STATE_UNORDERED_ACCESS;tr.Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_SOURCE;list->ResourceBarrier(1,&tr);
    list->CopyResource(rb.Get(),dst.Get());HR(list->Close());ID3D12CommandList *lists[]={list.Get()};queue->ExecuteCommandLists(1,lists);
    ComPtr<ID3D12Fence> fence;HR(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));
    HANDLE ev=CreateEventW(nullptr,FALSE,FALSE,nullptr);CK(ev);HR(queue->Signal(fence.Get(),1));HR(fence->SetEventOnCompletion(1,ev));CK(WaitForSingleObject(ev,10000)==WAIT_OBJECT_0);CloseHandle(ev);CK(fence->GetCompletedValue()==1);
    if(mode!=2 && mode!=3 && mode!=5){
        // Independently computed minimum surface-area partitions (ordered child IDs).
        const UINT split4[]={2,1,5,3,4,6},split7[]={1,4,10,7,8,11,12,5,3,2,6,9};
        const UINT *child=n==4?split4:split7;
        const UINT counts4[]={4,2,2},counts7[]={7,4,3,2,2,2};
        memcpy(w(1),child,8*internal);memcpy(w(2),n==4?counts4:counts7,4*internal);
        for(unsigned i=0;i<internal;i++)for(unsigned j=0;j<2;j++)w(0)[child[2*i+j]]=i;
        auto bounds=[&](auto &&self,UINT id)->void {
            if(id>=internal)return;
            UINT a=child[2*id],b=child[2*id+1];self(self,a);self(self,b);
            float aa[6],bb[6];memcpy(aa,w(3)+6*a,24);memcpy(bb,w(3)+6*b,24);
            for(unsigned j=0;j<6;j++)aa[j]=j<3?std::min(aa[j],bb[j]):std::max(aa[j],bb[j]);
            memcpy(w(3)+6*id,aa,24);
        };
        bounds(bounds,0);
    }
    HR(rb->Map(0,nullptr,&map));unsigned misses=0;for(unsigned i=0;i<total/4;i++)if(((UINT*)map)[i]!=exp[i]){
        if(misses++<24) fprintf(stderr,"FAIL case %u byte %u got %08x expected %08x\n",mode,4*i,((UINT*)map)[i],exp[i]);}
    rb->Unmap(0,nullptr);CK(!misses);printf("PASS FindTreelets case %u: %u fenced bytes including every guard\n",mode,total);return 0;
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
    for(unsigned i=0;i<6;i++){params[i].ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV;params[i].Descriptor.ShaderRegister=i;}
    params[6].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;params[6].Constants={0,0,5};HR(makeRoot(7,&right,&sig));
    params[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV;params[1].Descriptor.ShaderRegister=0;params[2]=params[6];params[2].Constants.Num32BitValues=16;HR(makeRoot(3,&wrong,&util));
    using Name=HRESULT(WINAPI*)(IUnknown*,const WCHAR*);HR(SLOT(right.Get(),0x30,Name)(right.Get(),L"XDXR TreeletReorder"));
    {
        FILE*f=fopen("find.bin","rb");CK(f);std::vector<BYTE>s(0x4dd8);CK(fread(s.data(),1,s.size(),f)==s.size()&&fgetc(f)==EOF&&!ferror(f));fclose(f);
        CK(sig->GetBufferSize()==236&&!memcmp(s.data()+0x144,(BYTE*)sig->GetBufferPointer()+36,200));
        struct Wire{IUnknown*r;const void*cs;SIZE_T n;BYTE tail[32];} wire={wrong.Get(),s.data(),s.size(),{}};using Make=HRESULT(WINAPI*)(IUnknown*,const Wire*,UINT64,UINT64,REFIID,void**);void*out;
        auto make=[&](){out=dev.Get();return SLOT(dev.Get(),0x1f0,Make)(dev.Get(),&wire,0,0,IID_ID3D12PipelineState,&out);};
        CK(make()==E_INVALIDARG&&!out);wire.r=right.Get();s.back()^=1;CK(make()==E_NOTIMPL&&!out);s.back()^=1;
        out=dev.Get();CK(SLOT(other.Get(),0x1f0,Make)(other.Get(),&wire,0,0,IID_ID3D12PipelineState,&out)==E_INVALIDARG&&!out);
        wire.tail[31]=1;CK(make()==E_NOTIMPL&&!out);wire.tail[31]=0;wire.n--;CK(make()==E_NOTIMPL&&!out);wire.n++;
        if(!GetProcAddress(GetModuleHandleW(L"d3d12core.dll"),"ScarlettBoundedTreeletsV1")){CK(make()==E_NOTIMPL&&!out);puts("PASS FindTreelets missing backend rejected");return 0;}
        if(!staged){
            auto guestRoot=xs_bvh_root_host(dev.Get(),right.Get());ComPtr<ID3D12Device>d;HR(guestRoot->GetDevice(IID_PPV_ARGS(&d)));
            D3D12_COMPUTE_PIPELINE_STATE_DESC pd={};pd.pRootSignature=guestRoot;pd.CS={s.data(),s.size()};ComPtr<ID3D12PipelineState>bad;
            CK(FAILED(d->CreateComputePipelineState(&pd,IID_PPV_ARGS(&bad)))&&!bad);
            puts("PASS FindTreelets without private window rejected by compiler");
        }
        HR(make());ComPtr<IUnknown>ps;ps.Attach((IUnknown*)out);HR(SLOT(out,0x30,Name)(ps.Get(),L"XDXR FindTreelets"));printf("PASS %s factory FindTreelets root/owner/digest/size/tail/name\n",staged?"staged":"linked");
        if(staged)return 0;
        ID3D12RootSignature*r=nullptr;auto host=test_treelets_host(ps.Get(),&r);CK(r&&host);for(unsigned mode=0;mode<6;mode++)CK(!probe(host,r,mode));
    }return 0;
}
int main(){
    HMODULE dll=LoadLibraryW(L"d3d12_xs.dll");CK(dll);FARPROC fn=GetProcAddress(dll,"D3D12XboxCreateDevice");CK(fn);
    Create create;memcpy(&create,&fn,sizeof create);CK(!verify(create,true));return verify(D3D12XboxCreateDevice,false);
}
