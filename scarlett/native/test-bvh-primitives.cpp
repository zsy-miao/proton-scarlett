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
extern "C" ID3D12PipelineState *test_bvh_primitives_host(IUnknown *,ID3D12RootSignature **);
static int triangles(ID3D12Device *d,IUnknown *pipeline,bool reload,UINT test){
    constexpr UINT G=64;constexpr UINT64 A=0x10ffffffc0ULL,B=0x21fffffff0ULL;
    const UINT words[]={128,64,64,128,128,64};std::vector<UINT> initial[6],expected[6];
    for(UINT b=0;b<6;b++){initial[b].resize(G+words[b]+G);for(UINT i=0;i<initial[b].size();i++) initial[b][i]=0xc71a0000u+211*i+b;}
    auto data=[&](UINT b){return initial[b].data()+G;};
    const bool empty=test==1,packed=test==2,shortRead=reload&&test==3,compact=!reload&&test>=3&&test<=5,reject=compact&&test>=4;
    UINT pc[]={UINT(B),UINT(B>>32),0,0,UINT(B-64),UINT((B-64)>>32),(1u<<16)|12,0,empty?0u:1u,test==4?2u:8u,1,0x103,compact?4u:reload?8u:0u,0xf00dcafe};
    UINT window[]={UINT(A),UINT(A>>32),shortRead?163u:512u,UINT(B-64),UINT((B-64)>>32),!reload&&test==5?79u:104u};
    float pos[][3]={{-1,2,3},{4,-5,6},{7,8,-9}};
    data(3)[3]=reload?(empty?0:1):7;data(3)[13]=128;data(3)[15]=0;
    auto meta=data(3)+32+12;meta[0]=0x100;meta[1]=0;meta[2]=~0u;meta[3]=9;
    UINT *geom=data(1)+8;UINT64 vertex=A+128+(packed?1:0);memcpy(geom,&vertex,8);geom[2]=geom[3]=geom[4]=geom[5]=0;geom[6]=packed?5:12;geom[7]=packed?34:6;
    if(!reload){window[0]=UINT(B+52);window[1]=UINT((B+52)>>32);window[2]=12;}
    if(!reload&&packed)pc[6]=2u<<16|5;
    float bound[]={100,100,100,-100,-100,-100};memcpy(data(5)+20,bound,24);memcpy(data(5),bound,24);data(5)[19]=2;data(4)[2]=0;
    if(!packed)memcpy(reload?data(4)+32:data(1),pos,sizeof pos);
    else {const UINT16 h[]={0xbc00,0x4000,0x4400,0xc500,0x4700,0x4800};for(UINT i=0;i<3;i++){pos[i][2]=0;memcpy((BYTE*)(reload?data(4)+32:data(1))+(reload?1:0)+5*i,h+2*i,4);}}
    float fourth[3]={10,11,-12};
    if(reload&&test==4){ // Unaligned R16 indices start two bytes into the source; rotate triangle.
        UINT64 idx=A+66;memcpy(geom+2,&idx,8);geom[7]|=57u<<16;const UINT16 indices[]={2,0,1};memcpy((BYTE*)data(4)+66,indices,6);
        std::swap(pos[0],pos[2]);std::swap(pos[1],pos[2]);
    }
    if(reload&&test==5){ // A paired record selects the third vertex of primitive 1 for the fourth output.
        meta[2]=1;memcpy(data(4)+32+15,fourth,12);
    }
    if(shortRead)memset(pos[2],0,12);
    for(UINT b=0;b<6;b++) expected[b]=initial[b];
    if(!empty&&!reject){
        UINT *o=expected[reload?3:0].data()+G+16*(reload?2:compact?2:1);memcpy(o,pos,sizeof pos);
        if(reload&&test==5){memcpy(o+9,fourth,12);}else if(reload){o[9]=o[10]=o[11]=0x7fc00000;}else{o[12]=pc[11];o[13]=0;o[14]=~0u;o[15]=9;expected[compact?5:3][G+(compact?19:3)]++;}
        for(UINT j=0;j<3;j++){float lo=std::min(pos[0][j],std::min(pos[1][j],pos[2][j])),hi=std::max(pos[0][j],std::max(pos[1][j],pos[2][j])),mid;
            if(reload&&test==5){lo=std::min(lo,fourth[j]);hi=std::max(hi,fourth[j]);}mid=(lo+hi)*.5f;
            memcpy(&expected[5][G+20+j],&lo,4);memcpy(&expected[5][G+23+j],&hi,4);memcpy(&expected[5][G+j],&mid,4);memcpy(&expected[5][G+3+j],&mid,4);}
    }
    // The view ends at the vertex payload. The following metadata is in the resource, outside the UAV.
    if(!reload&&test==6)memcpy(expected[0].data()+G+28,data(0)+28,16);
    if(reload&&test==6){ // Bounds recomputation disabled: vertices still update; both bounds structures stay untouched.
        pc[12]=0; expected[5]=initial[5];
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
        l->CopyResource(dst[b].Get(),seed[b].Get());barrier(dst[b],D3D12_RESOURCE_STATE_COPY_DEST,b==1 || b==2 || b==4?D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE:D3D12_RESOURCE_STATE_UNORDERED_ACCESS);}
    ID3D12RootSignature *r=nullptr;auto host=test_bvh_primitives_host(pipeline,&r);CK(host&&r);l->SetComputeRootSignature(r);l->SetPipelineState(host);
    auto va=[&](UINT b){return dst[b]->GetGPUVirtualAddress()+G*4;};
    D3D12_DESCRIPTOR_HEAP_DESC hd={D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,8,D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,1};
    ComPtr<ID3D12DescriptorHeap> heap;HR(d->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&heap)));auto cpu=heap->GetCPUDescriptorHandleForHeapStart();
    for(UINT i=0;i<8;i++){
        if(i<5){UINT b=i==4?3:0;D3D12_UNORDERED_ACCESS_VIEW_DESC v={};v.ViewDimension=D3D12_UAV_DIMENSION_BUFFER;v.Format=DXGI_FORMAT_R32_TYPELESS;v.Buffer={G, !reload&&test==6&&i==0?28u:words[b],0,0,D3D12_BUFFER_UAV_FLAG_RAW};d->CreateUnorderedAccessView(dst[b].Get(),nullptr,&v,cpu);}
        else {UINT b=i==5?1:2;D3D12_SHADER_RESOURCE_VIEW_DESC v={};v.ViewDimension=D3D12_SRV_DIMENSION_BUFFER;v.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            if(reload&&i==5){v.Buffer={G/8,words[b]/8,32,D3D12_BUFFER_SRV_FLAG_NONE};}else{v.Format=DXGI_FORMAT_R32_TYPELESS;v.Buffer={G,words[b],0,D3D12_BUFFER_SRV_FLAG_RAW};}d->CreateShaderResourceView(dst[b].Get(),&v,cpu);}
        cpu.ptr+=d->GetDescriptorHandleIncrementSize(hd.Type);
    }
    ID3D12DescriptorHeap *heaps[]={heap.Get()};l->SetDescriptorHeaps(1,heaps);l->SetComputeRootDescriptorTable(0,heap->GetGPUDescriptorHandleForHeapStart());
    l->SetComputeRoot32BitConstants(1,14,pc,0);l->SetComputeRoot32BitConstants(2,6,window,0);
    l->SetComputeRootShaderResourceView(3,va(4));l->SetComputeRootUnorderedAccessView(4,va(5));
    l->Dispatch(2,1,1);
    for(UINT b=0;b<6;b++){barrier(dst[b],b==1 || b==2 || b==4?D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE:D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);l->CopyResource(rb[b].Get(),dst[b].Get());}
    HR(l->Close());ID3D12CommandList *lists[]={l.Get()};q->ExecuteCommandLists(1,lists);
    ComPtr<ID3D12Fence>f;HR(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&f)));HANDLE e=CreateEventW(nullptr,FALSE,FALSE,nullptr);CK(e);
    HR(q->Signal(f.Get(),1));HR(f->SetEventOnCompletion(1,e));CK(WaitForSingleObject(e,20000)==WAIT_OBJECT_0&&f->GetCompletedValue()==1);CloseHandle(e);
    for(UINT b=0;b<6;b++){UINT*m;HR(rb[b]->Map(0,nullptr,(void**)&m));for(UINT i=0;i<expected[b].size();i++)if(m[i]!=expected[b][i]){
        printf("FAIL %s case=%u buffer=%u i=%u got=%x expected=%x\n",reload?"reload":"load",test,b,i,m[i],expected[b][i]);return 1;}rb[b]->Unmap(0,nullptr);}
    printf("PASS triangle %s case=%u fenced all six complete buffers\n",reload?"reload":"load",test);return 0;
}
static UINT bits(float f){UINT u;memcpy(&u,&f,4);return u;}
static int procedurals(IUnknown *pipeline,bool reload,UINT test){
    ID3D12RootSignature *root=nullptr;auto host=test_bvh_primitives_host(pipeline,&root);CK(host&&root);
    ComPtr<ID3D12Device>d; HR(host->GetDevice(IID_PPV_ARGS(&d)));
    // 0: primitive output; 1: header/records; 2: bounded atomic arena; 3: t0; 4: bounded AABB inputs.
    const UINT G=64,cap=48,old=2,first=5,geomFlags=0x5a103; const UINT words[]={16*cap,32+16*cap,48,16*cap,16*cap};
    const UINT64 A=0x10ffffff80ULL,B=0x20ffffffe0ULL;
    const UINT n=test==4?0:test==0?37:7, flags=reload?(test==0?0:8):((test==1||test==2||test==3)?4:0);
    const bool rollback=!reload&&test==2,shortCounter=!reload&&test==3,shortRead=reload&&test==3;
    const UINT capacity=rollback?old+n-1:cap, atomBytes=shortCounter?15:reload&&test==2?79:192,readBytes=shortRead?24*4+23:4*words[4];
    const bool centroid=test!=5;
    std::vector<UINT> initial[5],expected[5];
    for(UINT j=0;j<5;j++){initial[j].resize(G+words[j]+G);for(UINT i=0;i<initial[j].size();i++) initial[j][i]=0xc71a0000u+211*i+j;}
    auto data=[&](UINT j){return initial[j].data()+G;};data(1)[3]=reload?n:old;data(1)[13]=128;data(2)[3]=old;
    for(UINT k=0;k<6;k++) data(2)[4+k]=data(2)[16+k]=bits(k<3?10000.f:-10000.f);
    auto box=[](UINT index,float b[6]){float x=float(index);b[0]=5-x;b[1]=2*x-19;b[2]=14-3*x;b[3]=8+x;b[4]=3*x+2;b[5]=22+2*x;};
    for(UINT i=0;i<n;i++){
        float b[6];box(i,b);
        if(!reload) memcpy(data(3)+8*i,b,24);
        if(reload){UINT g=i%2,j=n-1-i,stride=g?32:40,off=g?400:24;
            data(1)[32+16*i+12]=(g<<8)|0xb5; data(1)[32+16*i+13]=j;
            memcpy(data(4)+off+(stride/4)*j,b,24);}
    }
    if(reload){for(UINT g=0;g<2;g++){UINT off=g?400:24;UINT64 addr=A+4*off;memcpy(data(3)+8*g,&addr,8);data(3)[8*g+6]=g?32:40;}}
    for(UINT j=0;j<5;j++)expected[j]=initial[j];
    if(!reload&&!rollback&&!shortCounter) expected[(flags&4)?2:1][G+3]+=n;
    for(UINT i=0;i<n&&!rollback&&!shortCounter;i++){
        float b[6];box(i,b);
        if(reload){if(shortRead){std::fill(b+(i==n-1?3:0),b+6,0.f);}
            memcpy(expected[1].data()+G+32+i*16+4,b,24);
        } else {UINT *p=expected[0].data()+G+16*(((flags&4)?old:first)+i);std::fill(p,p+16,0u);p[1]=p[2]=p[3]=~0u;
            memcpy(p+4,b,24);p[12]=geomFlags;p[13]=i;}
        if(!reload || (flags&8)) for(UINT j=0;j<(centroid?2u:1u);j++)for(UINT k=0;k<6;k++){
            const UINT off=(j?16:4)+k;if((off+1)*4>atomBytes)continue;
            auto &w=expected[2][G+off];float v;memcpy(&v,&w,4);
            const float c=j?(b[k%3]+b[k%3+3])*0.5f:b[k];w=bits(k<3?std::min(c,v):std::max(c,v));}
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
        l->CopyResource(dst[b].Get(),seed[b].Get());barrier(dst[b],D3D12_RESOURCE_STATE_COPY_DEST,b>=3?D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE:D3D12_RESOURCE_STATE_UNORDERED_ACCESS);}
    R cb;HR(buffer(cb,64,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ));void *m;
    const UINT64 c=centroid?B+64:0;const UINT pc[]={UINT(B),UINT(B>>32),0,0,UINT(c),UINT(c>>32),32,0,n,capacity,first,geomFlags,flags,0};
    HR(cb->Map(0,nullptr,&m));memcpy(m,pc,sizeof pc);cb->Unmap(0,nullptr);
    D3D12_DESCRIPTOR_HEAP_DESC hd={D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,3,D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,1};
    ComPtr<ID3D12DescriptorHeap>heap;HR(d->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&heap)));auto cpu=heap->GetCPUDescriptorHandleForHeapStart();
    D3D12_SHADER_RESOURCE_VIEW_DESC srv={};srv.Format=reload?DXGI_FORMAT_UNKNOWN:DXGI_FORMAT_R32_TYPELESS;srv.ViewDimension=D3D12_SRV_DIMENSION_BUFFER;
    srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;srv.Buffer.FirstElement=G/(reload?8:1);
    srv.Buffer.NumElements=reload?2:words[3];srv.Buffer.StructureByteStride=reload?32:0;srv.Buffer.Flags=reload?D3D12_BUFFER_SRV_FLAG_NONE:D3D12_BUFFER_SRV_FLAG_RAW;
    d->CreateShaderResourceView(dst[3].Get(),&srv,cpu);
    for(UINT b=0;b<2;b++){D3D12_UNORDERED_ACCESS_VIEW_DESC v={};v.ViewDimension=D3D12_UAV_DIMENSION_BUFFER;v.Format=b?DXGI_FORMAT_R32_TYPELESS:DXGI_FORMAT_UNKNOWN;
        v.Buffer.FirstElement=b?G:G/16;v.Buffer.NumElements=b?words[b]:cap;v.Buffer.StructureByteStride=b?0:64;v.Buffer.Flags=b?D3D12_BUFFER_UAV_FLAG_RAW:D3D12_BUFFER_UAV_FLAG_NONE;
        cpu.ptr+=d->GetDescriptorHandleIncrementSize(hd.Type);d->CreateUnorderedAccessView(dst[b].Get(),nullptr,&v,cpu);}
    l->SetComputeRootSignature(root);l->SetPipelineState(host);l->SetDescriptorHeaps(1,heap.GetAddressOf());l->SetComputeRootDescriptorTable(0,heap->GetGPUDescriptorHandleForHeapStart());
    l->SetComputeRootConstantBufferView(1,cb->GetGPUVirtualAddress());const UINT window[]={UINT(A),UINT(A>>32),readBytes,UINT(B),UINT(B>>32),atomBytes};
    l->SetComputeRoot32BitConstants(2,6,window,0);l->SetComputeRootShaderResourceView(3,dst[4]->GetGPUVirtualAddress()+4*G);l->SetComputeRootUnorderedAccessView(4,dst[2]->GetGPUVirtualAddress()+4*G);
    l->Dispatch(2,1,1);
    for(UINT b=0;b<5;b++){barrier(dst[b],b>=3?D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE:D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);l->CopyResource(rb[b].Get(),dst[b].Get());}
    HR(l->Close());ID3D12CommandList *lists[]={l.Get()};q->ExecuteCommandLists(1,lists);
    ComPtr<ID3D12Fence>f;HR(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&f)));HANDLE e=CreateEventW(nullptr,FALSE,FALSE,nullptr);CK(e);
    HR(q->Signal(f.Get(),1));HR(f->SetEventOnCompletion(1,e));CK(WaitForSingleObject(e,20000)==WAIT_OBJECT_0&&f->GetCompletedValue()==1);CloseHandle(e);
    for(UINT b=0;b<5;b++){UINT*m;HR(rb[b]->Map(0,nullptr,(void**)&m));for(UINT i=0;i<expected[b].size();i++)if(m[i]!=expected[b][i]){
        printf("FAIL %s case=%u buffer=%u i=%u got=%x expected=%x\n",reload?"reload":"load",test,b,i,m[i],expected[b][i]);return 1;}rb[b]->Unmap(0,nullptr);}
    printf("PASS procedural %s case=%u fenced all five complete buffers\n",reload?"reload":"load",test);return 0;
}
using Create=decltype(&D3D12XboxCreateDevice);
static int verify(Create create,bool staged){
    std::vector<BYTE> family;
    const IID boot={0x5405c344,0xd457,0x444e,{0xb4,0xdd,0x23,0x66,0xe4,0x5a,0xee,0x39}};
    DWORD params[20]={};params[0]=0x20004;params[2]=0x100000;params[10]=1;params[15]=0x20;
    ComPtr<IUnknown>dev,other,root[3],ps;HR(create(nullptr,params,boot,(void **)dev.GetAddressOf()));HR(create(nullptr,params,boot,(void **)other.GetAddressOf()));
    using Root=HRESULT(WINAPI *)(IUnknown*,UINT,const void*,SIZE_T,REFIID,void**);using Name=HRESULT(WINAPI *)(IUnknown*,const WCHAR*);
    for(UINT z=0;z<3;z++){
        D3D12_ROOT_PARAMETER1 rp[9]={};const UINT p=z==1?2:8;
        for(UINT i=0;i<p;i++){rp[i].ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV;rp[i].Descriptor.ShaderRegister=i;}
        if(!z) for(UINT i=5;i<8;i++){rp[i].ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV;rp[i].Descriptor.ShaderRegister=i-5;}
        if(z==1){rp[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV;rp[1].Descriptor.ShaderRegister=0;}
        rp[p].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;rp[p].Constants.Num32BitValues=z==1?16:z==2?3:14;
        D3D12_VERSIONED_ROOT_SIGNATURE_DESC sig={};sig.Version=D3D_ROOT_SIGNATURE_VERSION_1_1;sig.Desc_1_1={p+1,rp,0,nullptr,(D3D12_ROOT_SIGNATURE_FLAGS)0x03000000};
        ComPtr<ID3DBlob>blob;HR(D3D12SerializeVersionedRootSignature(&sig,&blob,nullptr));auto n=blob->GetBufferSize();CK(n==(z==1?140:284));
        if(!z){auto ptr=(BYTE*)blob->GetBufferPointer();family.assign(ptr+36,ptr+n);}
        auto make=[&](UINT node,const void*b){return SLOT(dev,0x80,Root)(dev.Get(),node,b,n,IID_ID3D12RootSignature,(void**)root[z].GetAddressOf());};
        CK(make(0,blob->GetBufferPointer())==E_NOTIMPL&&!root[z]);std::vector<BYTE>bytes(n);memcpy(bytes.data(),blob->GetBufferPointer(),n);bytes.back()^=1;
        CK(make(1,bytes.data())==E_NOTIMPL&&!root[z]);
        rp[1].Descriptor.RegisterSpace=1;ComPtr<ID3DBlob>other_blob;HR(D3D12SerializeVersionedRootSignature(&sig,&other_blob,nullptr));
        CK(other_blob->GetBufferSize()==n&&make(1,other_blob->GetBufferPointer())==E_NOTIMPL&&!root[z]);
        HR(make(1,blob->GetBufferPointer()));
        const WCHAR *name=!z?L"XDXR LoadPrimitivesPass":z==1?L"XDXR UtilityShaders":L"XDXR BVH4Builder";WCHAR wrong[32]={};
        CK(SLOT(root[z],0x30,Name)(root[z].Get(),wrong)==E_NOTIMPL);HR(SLOT(root[z],0x30,Name)(root[z].Get(),name));
        if(!staged) CK(!xs_bvh_root_host(other.Get(),root[z].Get())&&xs_bvh_root_host(dev.Get(),root[z].Get()));
        printf("PASS root kind=%u bytes=%zu exact/owner/name\n",z,n);
    }
    ComPtr<ID3D12Device>d;if(!staged) HR(xs_bvh_root_host(dev.Get(),root[0].Get())->GetDevice(IID_PPV_ARGS(&d)));
    struct Kernel {const char *file,*marker;const WCHAR *name;UINT size,rts;bool proc,reload;};
    const Kernel kernels[]={
        {"load.bin","ScarlettBoundedTrianglesV1",L"XDXR LoadTriangles",0xa304,0x15c,false,false},
        {"load.dxbc","ScarlettBoundedProceduralsV1",L"XDXR LoadProceduralGeometry",0x21b4,0x12c,true,false},
        {"reload.bin","ScarlettBoundedTrianglesV1",L"XDXR ReloadTriangles",0xb1e4,0x114,false,true},
        {"reload.dxbc","ScarlettBoundedProceduralsV1",L"XDXR ReloadProcedurals",0x1bc8,0x114,true,true}};
    for(const auto &k:kernels) {
        std::vector<BYTE>code(k.size);FILE*f=fopen(k.file,"rb");CK(f&&fread(code.data(),1,code.size(),f)==code.size()&&fgetc(f)==EOF&&!ferror(f));fclose(f);
        CK(family.size()==248 && !memcmp(code.data()+k.rts,family.data(),family.size()));
        struct Wire{IUnknown *root;const void *cs;SIZE_T size;BYTE tail[32];}w={root[0].Get(),code.data(),code.size(),{}};
        using Make=HRESULT(WINAPI *)(IUnknown*,const Wire*,UINT64,UINT64,REFIID,void**);
        auto make=[&](ComPtr<IUnknown>&o){return SLOT(o,0x1f0,Make)(o.Get(),&w,0,0,IID_ID3D12PipelineState,(void**)ps.GetAddressOf());};
        CK(make(other)==E_INVALIDARG&&!ps);for(UINT z:{1u,2u}){w.root=root[z].Get();CK(make(dev)==E_INVALIDARG&&!ps);}w.root=root[0].Get();
        w.size--;CK(make(dev)==E_NOTIMPL&&!ps);w.size++;w.tail[31]=1;CK(make(dev)==E_NOTIMPL&&!ps);w.tail[31]=0;
        code.back()^=1;CK(make(dev)==E_NOTIMPL&&!ps);code.back()^=1;
        if(!GetProcAddress(GetModuleHandleW(L"d3d12core.dll"),k.marker)){CK(make(dev)==E_NOTIMPL&&!ps);printf("PASS %s missing backend rejected\n",k.file);continue;}
        // The exact guest root alone lacks both authorized windows. Never dispatch it.
        if(!staged){
        D3D12_COMPUTE_PIPELINE_STATE_DESC pd={};pd.pRootSignature=xs_bvh_root_host(dev.Get(),root[0].Get());pd.CS={code.data(),code.size()};
        ComPtr<ID3D12PipelineState>bad;CK(FAILED(d->CreateComputePipelineState(&pd,IID_PPV_ARGS(&bad)))&&!bad);
        puts("PASS compiler declined exact guest shader without private windows");
        }
        HR(make(dev));ID3D12RootSignature *bound=nullptr;if(!staged) CK(test_bvh_primitives_host(ps.Get(),&bound)&&bound!=xs_bvh_root_host(dev.Get(),root[0].Get()));
        WCHAR wrong[32]={};CK(SLOT(ps,0x30,Name)(ps.Get(),wrong)==E_NOTIMPL);
        HR(SLOT(ps,0x30,Name)(ps.Get(),k.name));
        printf("PASS %s factory %s owner/schema/digest/size/tail/name\n",staged?"staged":"linked",k.file);
        if(!staged) for(UINT test=0;test<(k.proc?6u:7u);test++)
            CK(!(k.proc?procedurals(ps.Get(),k.reload,test):triangles(d.Get(),ps.Get(),k.reload,test)));
        ps.Reset();
    }
    return 0;
}
int main(){
    HMODULE dll=LoadLibraryW(L"d3d12_xs.dll");CK(dll);FARPROC fn=GetProcAddress(dll,"D3D12XboxCreateDevice");Create create;
    CK(fn);memcpy(&create,&fn,sizeof create);CK(!verify(create,true));
    return verify(D3D12XboxCreateDevice,false);
}
