#include <d3d12.h>
#include <wrl/client.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>
using Microsoft::WRL::ComPtr;
extern "C" HRESULT WINAPI D3D12XboxCreateDevice(void *,const void *,REFIID,void **);
extern "C" ID3D12PipelineState *test_rearrange_host(IUnknown *,ID3D12RootSignature **);
#define SLOT(o,n,T) ((T)((*reinterpret_cast<void***>((o).Get()))[n/8]))
#define CK(x) do{if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);return 1;}}while(0)
#define HR(x) CK(SUCCEEDED(x))
using R=ComPtr<ID3D12Resource>;

static int dispatch(IUnknown *wrapped,bool triangle,UINT n,UINT selector,UINT copy,UINT window)
{
    ID3D12RootSignature *root=nullptr; auto ps=test_rearrange_host(wrapped,&root); CK(ps && root);
    ComPtr<ID3D12Device>d; HR(ps->GetDevice(IID_PPV_ARGS(&d)));
    const UINT stride[]={64,4,4,64,4,4,24},section=3072,total=9*section,cap=48;
    auto word=[&](UINT u,UINT elem){return ((1+u)*section+elem*stride[u])/4;};
    std::vector<UINT> seed(total/4); for(UINT i=0;i<seed.size();i++) seed[i]=0x719304b3+17*i;
    for(UINT i=0;i<n;i++) {
        seed[word(2,i)]=n-1-i;
        UINT *t=&seed[word(0,i)]; float xyz[12];
        for(UINT v=0;v<4;v++) for(UINT a=0;a<3;a++) xyz[3*v+a]=float(int(i)*9-int(v)*13+int(a)*7)*((a+v)&1?-1.f:1.f);
        memcpy(t,xyz,sizeof xyz);t[12]=i%3==2?0x88:8;t[13]=i*103+7;t[14]=i*257;t[15]=i%3;
        if(!triangle){float box[]={float(i)-2,-float(i),4,float(i)+2,1-float(i),9};memcpy(&seed[word(6,i)],box,24);}
    }
    UINT header[64]={}; header[3]=selector==~0u||triangle?n:5;header[16]=2;header[17]=7;
    header[19]=0x4182;header[20+7]=n;header[20+2*7]=n>=4?n-4:0;
    UINT live=(window<16 || (!triangle && selector==1 && window<112)) ? 0 : n;
    auto expected=seed;
    for(UINT i=0;i<live;i++) {
        const UINT j=seed[word(2,i)],dest=word(6,std::max(n,2u)+i);
        if(triangle){
            const UINT *t=&seed[word(0,j)]; if(copy) memcpy(&expected[word(3,i)],t,64);
            if(!t[15]) memcpy(&expected[dest],t+4,24);
            else {
                float coord[12],box[6];memcpy(coord,t,48);
                for(UINT a=0;a<3;a++){box[a]=box[a+3]=coord[a];for(UINT v=1;v<((t[12]&128)?4u:3u);v++){
                    box[a]=std::min(box[a],coord[v*3+a]);box[a+3]=std::max(box[a+3],coord[v*3+a]);}}
                memcpy(&expected[dest],box,24);
            }
        } else memcpy(&expected[dest],&seed[word(6,j)],24);
    }
    auto buffer=[&](R&r,UINT bytes,D3D12_HEAP_TYPE type){
        D3D12_HEAP_PROPERTIES hp={};hp.Type=type;hp.CreationNodeMask=hp.VisibleNodeMask=1; D3D12_RESOURCE_DESC rd={};
        rd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;rd.Width=bytes;rd.Height=rd.DepthOrArraySize=rd.MipLevels=rd.SampleDesc.Count=1;
        rd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;if(type==D3D12_HEAP_TYPE_DEFAULT)rd.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        return d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,type==D3D12_HEAP_TYPE_UPLOAD?D3D12_RESOURCE_STATE_GENERIC_READ:D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&r));};
    R upload,arena,rb,hdr;HR(buffer(upload,total,D3D12_HEAP_TYPE_UPLOAD));HR(buffer(arena,total,D3D12_HEAP_TYPE_DEFAULT));
    HR(buffer(rb,total,D3D12_HEAP_TYPE_READBACK));HR(buffer(hdr,512,D3D12_HEAP_TYPE_UPLOAD));void*m;
    HR(upload->Map(0,nullptr,&m));memcpy(m,seed.data(),total);upload->Unmap(0,nullptr);
    UINT k[]={0xffffffe0,0x35,copy,selector},w[]={k[0],k[1],window};
    HR(hdr->Map(0,nullptr,&m));memcpy(m,header,sizeof header);memcpy((BYTE*)m+256,k,sizeof k);hdr->Unmap(0,nullptr);
    D3D12_DESCRIPTOR_HEAP_DESC hd={D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,7,D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,1};
    ComPtr<ID3D12DescriptorHeap>heap; HR(d->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&heap)));auto cpu=heap->GetCPUDescriptorHandleForHeapStart();
    for(UINT i=0;i<7;i++){D3D12_UNORDERED_ACCESS_VIEW_DESC v={};v.ViewDimension=D3D12_UAV_DIMENSION_BUFFER;
        v.Buffer.FirstElement=word(i,0)*4/stride[i];v.Buffer.NumElements=(i==6?2*cap:cap);v.Buffer.StructureByteStride=stride[i];
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
        fprintf(stderr,"FAIL %s n=%u copy=%u selector=%x window=%u word %u: %08x != %08x\n",triangle?"tri":"bvh",n,copy,selector,window,i,((UINT*)m)[i],expected[i]);return 1;}
    rb->Unmap(0,nullptr);printf("PASS rearrange %s count=%u copy=%u selector=%x window=%u output and guards\n",triangle?"Triangles":"BVHs",n,copy,selector,window);return 0;
}

int main(){
    const IID boot={0x5405c344,0xd457,0x444e,{0xb4,0xdd,0x23,0x66,0xe4,0x5a,0xee,0x39}};
    DWORD params[20]={};params[0]=0x20004;params[2]=0x100000;params[10]=1;params[15]=0x20;
    ComPtr<IUnknown>dev,other,root,utility,ps;
    HR(D3D12XboxCreateDevice(nullptr,params,boot,(void**)dev.GetAddressOf()));HR(D3D12XboxCreateDevice(nullptr,params,boot,(void**)other.GetAddressOf()));
    D3D12_ROOT_PARAMETER1 p[8]={};for(UINT i=0;i<7;i++){p[i].ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV;p[i].Descriptor.ShaderRegister=i;}
    p[7].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;p[7].Constants.Num32BitValues=4;
    D3D12_VERSIONED_ROOT_SIGNATURE_DESC sig={};sig.Version=D3D_ROOT_SIGNATURE_VERSION_1_1;sig.Desc_1_1={8,p,0,nullptr,(D3D12_ROOT_SIGNATURE_FLAGS)0x03000000};
    using Root=HRESULT(WINAPI *)(IUnknown*,UINT,const void*,SIZE_T,REFIID,void**);auto getroot=[&](ID3DBlob*b,ComPtr<IUnknown>&r){return SLOT(dev,0x80,Root)(dev.Get(),1,b->GetBufferPointer(),b->GetBufferSize(),IID_ID3D12RootSignature,(void**)r.GetAddressOf());};
    ComPtr<ID3DBlob>blob,b;HR(D3D12SerializeVersionedRootSignature(&sig,&blob,nullptr));CK(blob->GetBufferSize()==260);HR(getroot(blob.Get(),root));
    using Name=HRESULT(WINAPI *)(IUnknown*,const WCHAR*);HR(SLOT(root,0x30,Name)(root.Get(),L"XDXR RearrangeElementsPass"));
    p[7].Constants.Num32BitValues=3;HR(D3D12SerializeVersionedRootSignature(&sig,&b,nullptr));CK(getroot(b.Get(),utility)==E_NOTIMPL&&!utility);b.Reset();
    memset(p,0,sizeof p);sig.Desc_1_1.NumParameters=3;p[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV;p[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV;
    p[2].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;p[2].Constants.Num32BitValues=16;HR(D3D12SerializeVersionedRootSignature(&sig,&b,nullptr));HR(getroot(b.Get(),utility));
    bool capable=GetProcAddress(GetModuleHandleW(L"d3d12core.dll"),"ScarlettBoundedRearrangeV1")!=nullptr;
    for(int z=0;z<2;z++){
        std::vector<BYTE> code(z?0x1794:0x1c80);FILE*f=fopen(z?"bvh.dxbc":"tri.dxbc","rb");CK(f && fread(code.data(),1,code.size(),f)==code.size() && fgetc(f)==EOF && !ferror(f));fclose(f);
        CK(!memcmp(code.data()+(z?0x114:0x144),(BYTE*)blob->GetBufferPointer()+36,224));
        struct Wire{IUnknown*root;const BYTE*cs;SIZE_T size;BYTE tail[32];}w={utility.Get(),code.data(),code.size(),{}};
        using Make=HRESULT(WINAPI *)(IUnknown*,const Wire*,UINT64,UINT64,REFIID,void**);
        auto make=[&](ComPtr<IUnknown>&owner){return SLOT(owner,0x1f0,Make)(owner.Get(),&w,0,0,IID_ID3D12PipelineState,(void**)ps.GetAddressOf());};
        CK(make(dev)==E_INVALIDARG&&!ps);w.root=root.Get();CK(make(other)==E_INVALIDARG&&!ps);
        code.back()^=1;CK(make(dev)==E_NOTIMPL&&!ps);code.back()^=1;w.tail[31]=1;CK(make(dev)==E_NOTIMPL&&!ps);w.tail[31]=0;
        if(!capable){CK(make(dev)==E_NOTIMPL&&!ps);continue;}
        HR(make(dev));HR(SLOT(ps,0x30,Name)(ps.Get(),z?L"XDXR RearrangeBVHs":L"XDXR RearrangeTriangles"));
        CK(!dispatch(ps.Get(),!z,37,~0u,0,256));CK(!dispatch(ps.Get(),!z,1,~0u,1,16));CK(!dispatch(ps.Get(),!z,37,~0u,1,15));
        if(!z) CK(!dispatch(ps.Get(),true,37,2,1,256));
        else {CK(!dispatch(ps.Get(),false,37,1,0,256));CK(!dispatch(ps.Get(),false,37,1,0,111));CK(!dispatch(ps.Get(),false,37,~1u,0,256));}
        ps.Reset();
    }
    puts(capable?"PASS both exact rearrange factories, owners and root schemas":"PASS both exact rearrange factories declined without backend");
}
