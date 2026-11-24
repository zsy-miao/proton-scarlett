#include <d3d12.h>
#include <wrl/client.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
using Microsoft::WRL::ComPtr;
#define CK(x) do{if(!(x)){printf("FAIL %d %s\n",__LINE__,#x);return 1;}}while(0)
#define HR(x) CK(SUCCEEDED(x))
using R=ComPtr<ID3D12Resource>;
using P=ComPtr<ID3D12PipelineState>;
template<class Key> int exercise(ID3D12Device*d,ID3D12RootSignature*root,P *p,UINT variant){
    const UINT capacity=8192,guard=64, Kbytes=capacity*sizeof(Key)+2*guard,Ibytes=capacity*4+2*guard;
    UINT n=variant==4?5093:variant==5?0:capacity,sorted=variant==6?0:n;
    bool indirect=variant>=3;
    std::vector<BYTE> sk(Kbytes,0xcd),si(Ibytes,0x7b),header(128,0xef);
    std::vector<Key> original(n);const UINT mask=0x5ea1b032;
    uint32_t rng=0x3412u;auto random=[&](){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;};
    for(UINT i=0;i<n;i++){
        Key k=Key((uint64_t(random())<<32)|random());
        switch(variant){case 0:k=Key(-1)-i;break;case 1:k=Key(i)+(Key(1)<<(sizeof(Key)*8-1))-n/2;break;
            case 2:k=(i%5==0)?Key(-1):(i%3==0?0:1+(i%31));break;default:break;}
        original[i]=k;auto index=i^mask;memcpy(sk.data()+guard+i*sizeof k,&k,sizeof k);memcpy(si.data()+guard+i*4,&index,4);
    }
    memcpy(header.data()+guard+12,&n,4);
    auto buffer=[&](R &res,UINT bytes,D3D12_HEAP_TYPE h,D3D12_RESOURCE_STATES st){
        D3D12_HEAP_PROPERTIES hp={};hp.Type=h;hp.CreationNodeMask=hp.VisibleNodeMask=1;D3D12_RESOURCE_DESC rd={};
        rd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;rd.Width=bytes;rd.Height=rd.DepthOrArraySize=rd.MipLevels=rd.SampleDesc.Count=1;
        rd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;if(h==D3D12_HEAP_TYPE_DEFAULT)rd.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        return d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,st,nullptr,IID_PPV_ARGS(&res));};
    R uk,ui,uh,dk,di,rk,ri;auto upload=[&](R &r,const std::vector<BYTE>&v){HRESULT hr=buffer(r,UINT(v.size()),D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);
        if(FAILED(hr)){return hr;}void*m=nullptr;hr=r->Map(0,nullptr,&m);if(SUCCEEDED(hr)){memcpy(m,v.data(),v.size());r->Unmap(0,nullptr);}return hr;};
    HR(upload(uk,sk));HR(upload(ui,si));HR(upload(uh,header));
    HR(buffer(dk,Kbytes,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_COPY_DEST));HR(buffer(di,Ibytes,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_COPY_DEST));
    HR(buffer(rk,Kbytes,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST));HR(buffer(ri,Ibytes,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST));
    D3D12_COMMAND_QUEUE_DESC qd={};qd.Type=D3D12_COMMAND_LIST_TYPE_COMPUTE;ComPtr<ID3D12CommandQueue>q;HR(d->CreateCommandQueue(&qd,IID_PPV_ARGS(&q)));
    ComPtr<ID3D12CommandAllocator>al;ComPtr<ID3D12GraphicsCommandList>l;HR(d->CreateCommandAllocator(qd.Type,IID_PPV_ARGS(&al)));
    HR(d->CreateCommandList(1,qd.Type,al.Get(),nullptr,IID_PPV_ARGS(&l)));
    l->CopyResource(dk.Get(),uk.Get());l->CopyResource(di.Get(),ui.Get());
    auto trans=[&](R&r,D3D12_RESOURCE_STATES from,D3D12_RESOURCE_STATES to){D3D12_RESOURCE_BARRIER b={};b.Transition={r.Get(),0,from,to};l->ResourceBarrier(1,&b);};
    auto barrier=[&](){D3D12_RESOURCE_BARRIER b={};b.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV;l->ResourceBarrier(1,&b);};
    trans(dk,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);trans(di,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    UINT64 guest=0x21ffffff00ull;UINT b1[]={indirect?17:n,indirect?UINT(guest>>8):0},win[]={UINT(guest),UINT(guest>>32),variant==6?15u:16u};
    l->SetComputeRootSignature(root);l->SetComputeRoot32BitConstants(0,2,b1,0);l->SetComputeRootUnorderedAccessView(2,dk->GetGPUVirtualAddress()+guard);
    l->SetComputeRootUnorderedAccessView(3,di->GetGPUVirtualAddress()+guard);l->SetComputeRoot32BitConstants(4,3,win,0);l->SetComputeRootShaderResourceView(5,uh->GetGPUVirtualAddress()+guard);
    auto dispatch=[&](UINT index,UINT k,UINT j){UINT b0[]={k,j};l->SetComputeRoot32BitConstants(1,2,b0,0);l->SetPipelineState(p[index].Get());l->Dispatch(capacity/2048,1,1);barrier();};
    dispatch(0,0,0);for(UINT k=4096;k<=capacity;k*=2){for(UINT j=k/2;j>=2048;j/=2)dispatch(1,k,j);dispatch(2,k,1024);}
    trans(dk,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);trans(di,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);
    l->CopyResource(rk.Get(),dk.Get());l->CopyResource(ri.Get(),di.Get());HR(l->Close());ID3D12CommandList *lists[]={l.Get()};q->ExecuteCommandLists(1,lists);
    ComPtr<ID3D12Fence>f;HR(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&f)));HANDLE e=CreateEventW(nullptr,FALSE,FALSE,nullptr);CK(e);
    HR(q->Signal(f.Get(),1));HR(f->SetEventOnCompletion(1,e));CK(WaitForSingleObject(e,20000)==WAIT_OBJECT_0 && f->GetCompletedValue()==1);CloseHandle(e);
    BYTE *kb,*ib,*hb;HR(rk->Map(0,nullptr,(void**)&kb));HR(ri->Map(0,nullptr,(void**)&ib));HR(uh->Map(0,nullptr,(void**)&hb));CK(!memcmp(hb,header.data(),header.size()));
    auto guards=[&](BYTE *m,const std::vector<BYTE>&v,size_t live){return !memcmp(m,v.data(),guard)&&!memcmp(m+guard+live,v.data()+guard+live,v.size()-guard-live);};
    CK(guards(kb,sk,sorted*sizeof(Key))&&guards(ib,si,sorted*4));auto expected=original;std::sort(expected.begin(),expected.end());std::vector<bool>seen(n,false);
    for(UINT i=0;i<sorted;i++){Key k;UINT id;memcpy(&k,kb+guard+i*sizeof k,sizeof k);memcpy(&id,ib+guard+i*4,4);id^=mask;
        if(k!=expected[i]||id>=n||seen[id]||original[id]!=k){printf("FAIL width=%zu variant=%u i=%u actual=%llx expected=%llx id=%u\n",sizeof k,variant,i,(unsigned long long)k,(unsigned long long)expected[i],id);return 1;}seen[id]=true;}
    rk->Unmap(0,nullptr);ri->Unmap(0,nullptr);uh->Unmap(0,nullptr);printf("PASS bitonic %zu-bit variant=%u sorted=%u guards and payloads\n",8*sizeof(Key),variant,sorted);return 0;
}
int main(){
    ComPtr<ID3D12Device>d;HR(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&d)));
    const char*names[]={"pre","outer","inner","pre64","outer64","inner64"};const UINT sizes[]={0x20c8,0x1468,0x1f5c,0x2204,0x1554,0x2094};
    for(UINT layout=0;layout<3;layout++){
        D3D12_ROOT_PARAMETER p[6]={};for(UINT i=0;i<2;i++){p[i].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;p[i].Constants={1-i,0,2};
            p[2+i].ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV;p[2+i].Descriptor.ShaderRegister=i;}
        p[4].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;p[4].Constants={0,0x5358,3};
        p[5].ParameterType=layout==1?D3D12_ROOT_PARAMETER_TYPE_UAV:D3D12_ROOT_PARAMETER_TYPE_SRV;p[5].Descriptor.RegisterSpace=0x5358;
        D3D12_ROOT_SIGNATURE_DESC sig={layout?6u:4u,p,0,nullptr,D3D12_ROOT_SIGNATURE_FLAG_NONE};ComPtr<ID3DBlob>b;HR(D3D12SerializeRootSignature(&sig,D3D_ROOT_SIGNATURE_VERSION_1,&b,nullptr));
        ComPtr<ID3D12RootSignature>r;HR(d->CreateRootSignature(1,b->GetBufferPointer(),b->GetBufferSize(),IID_PPV_ARGS(&r)));
        P ps[6];for(UINT i=0;i<6;i++){char path[32];snprintf(path,sizeof path,"%s.dxbc",names[i]);FILE*f=fopen(path,"rb");CK(f);
            std::vector<BYTE>code(sizes[i]);CK(fread(code.data(),1,code.size(),f)==code.size()&&fgetc(f)==EOF&&!ferror(f));fclose(f);
            D3D12_COMPUTE_PIPELINE_STATE_DESC pd={};pd.pRootSignature=r.Get();pd.CS={code.data(),code.size()};HRESULT hr=d->CreateComputePipelineState(&pd,IID_PPV_ARGS(&ps[i]));
            if(layout<2)CK(FAILED(hr)&&!ps[i]);else HR(hr);}
        printf("PASS six PSOs layout=%u %s\n",layout,layout<2?"rejected":"created");
        if(layout==2)for(UINT variant=0;variant<7;variant++){
            CK(!exercise<UINT>(d.Get(),r.Get(),ps,variant));CK(!exercise<UINT64>(d.Get(),r.Get(),ps+3,variant));}
    }
}
