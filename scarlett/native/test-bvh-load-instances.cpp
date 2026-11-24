#include <d3d12.h>
#include <wrl/client.h>
#include <array>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cfloat>
#include <cstdint>
using Microsoft::WRL::ComPtr;
#define CK(x) do{if(!(x)){printf("FAIL line %u: %s\n",__LINE__,#x); return 1;}}while(0)
#define HR(x) CK(SUCCEEDED(x))
constexpr UINT W=2048, pad=16;
using Buf=std::array<UINT,W>;
static UINT bits(float f){UINT u;memcpy(&u,&f,4);return u;}
static float val(UINT u){float f;memcpy(&f,&u,4);return f;}
#define SLOT(o,n,T) ((T)((*reinterpret_cast<void***>((o).Get()))[n/8]))
static int factory(BYTE (&code)[0x752c]){
    // Exercise the adapter DLL staged in the tool, not an in-process copy of the factory.
    HMODULE lib=LoadLibraryW(L"d3d12_xs.dll");CK(lib);
    using Create=HRESULT(WINAPI *)(void *,const void *,REFIID,void **);
    FARPROC sym=GetProcAddress(lib,"D3D12XboxCreateDevice");Create create;memcpy(&create,&sym,sizeof create);CK(create);
    const IID boot={0x5405c344,0xd457,0x444e,{0xb4,0xdd,0x23,0x66,0xe4,0x5a,0xee,0x39}};
    DWORD params[20]={};params[0]=0x20004;params[2]=0x100000;params[10]=1;params[15]=0x20;
    ComPtr<IUnknown> dev,other,root,ps;
    HR(create(nullptr,params,boot,(void **)dev.GetAddressOf()));HR(create(nullptr,params,boot,(void **)other.GetAddressOf()));
    D3D12_ROOT_PARAMETER1 rp[3]={};rp[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV;rp[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV;
    rp[2].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;rp[2].Constants.Num32BitValues=16;
    D3D12_VERSIONED_ROOT_SIGNATURE_DESC sig={};sig.Version=D3D_ROOT_SIGNATURE_VERSION_1_1;
    sig.Desc_1_1={3,rp,0,nullptr,(D3D12_ROOT_SIGNATURE_FLAGS)0x03000000};
    ComPtr<ID3DBlob> blob;HR(D3D12SerializeVersionedRootSignature(&sig,&blob,nullptr));
    using Root=HRESULT(WINAPI *)(IUnknown *,UINT,const void *,SIZE_T,REFIID,void **);
    HR(SLOT(dev,0x80,Root)(dev.Get(),1,blob->GetBufferPointer(),blob->GetBufferSize(),IID_ID3D12RootSignature,(void **)root.GetAddressOf()));
    struct Wire{IUnknown *root;const void *code;SIZE_T size;BYTE tail[32];}w={root.Get(),code,sizeof code,{}};
    using Make=HRESULT(WINAPI *)(IUnknown *,const Wire *,UINT64,UINT64,REFIID,void **);
    auto make=[&](const ComPtr<IUnknown>&owner){return SLOT(owner,0x1f0,Make)(owner.Get(),&w,0,0,IID_ID3D12PipelineState,(void **)ps.GetAddressOf());};
    CK(make(other)==E_INVALIDARG&&!ps);
    code[sizeof code-1]^=1;CK(make(dev)==E_NOTIMPL&&!ps);code[sizeof code-1]^=1;
    w.size--;CK(make(dev)==E_NOTIMPL&&!ps);w.size++;w.tail[31]=1;CK(make(dev)==E_NOTIMPL&&!ps);w.tail[31]=0;
#ifdef TEST_NO_LOAD_COMPILER
    CK(make(dev)==E_NOTIMPL&&!ps);puts("PASS fifth unavailable compiler declined");return 0;
#else
    HR(make(dev));using Name=HRESULT(WINAPI *)(IUnknown *,const WCHAR *);
    CK(SLOT(ps,0x30,Name)(ps.Get(),L"XDXR WriteInstances")==E_NOTIMPL);HR(SLOT(ps,0x30,Name)(ps.Get(),L"XDXR LoadInstances"));
    puts("PASS adapter DLL fifth +1f0: identity, owner, size, zero tail, exact name");return 0;
#endif
}
int main(){
    ComPtr<ID3D12Device>d;HR(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&d)));
    BYTE code[0x752c];FILE*f=fopen("private-load-instances.bin","rb");CK(f && fread(code,1,sizeof code,f)==sizeof code && fgetc(f)==EOF&&!ferror(f));fclose(f);
    CK(factory(code)==0);
#ifdef TEST_NO_LOAD_COMPILER
    return 0;
#endif
    for(UINT layout=0;layout<4;layout++){
        UINT t=0,u=1,b=2,r=3,s=4,a=5;
        if(layout==3){a=0;b=1;s=2;u=3;r=4;t=5;}
        D3D12_ROOT_PARAMETER1 p[6]={};
        p[t].ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV;p[u].ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV;
        p[b].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;p[b].Constants={0,0,16};
        p[r].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;p[r].Constants={0,0x5358,6};
        p[s].ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV;p[s].Descriptor.RegisterSpace=0x5358;
        p[a].ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV;p[a].Descriptor.RegisterSpace=0x5358;
        D3D12_VERSIONED_ROOT_SIGNATURE_DESC spec={};spec.Version=D3D_ROOT_SIGNATURE_VERSION_1_1;
        spec.Desc_1_1={layout==0?3u:layout==1?5u:6u,p,0,nullptr,D3D12_ROOT_SIGNATURE_FLAG_NONE};
        ComPtr<ID3DBlob> wire;HR(D3D12SerializeVersionedRootSignature(&spec,&wire,nullptr));
        ComPtr<ID3D12RootSignature>root;HR(d->CreateRootSignature(1,wire->GetBufferPointer(),wire->GetBufferSize(),IID_PPV_ARGS(&root)));
        ComPtr<ID3D12PipelineState> ps;D3D12_COMPUTE_PIPELINE_STATE_DESC pd={};pd.pRootSignature=root.Get();pd.CS={code,sizeof code};
        HRESULT hr=d->CreateComputePipelineState(&pd,IID_PPV_ARGS(&ps));printf("PSO hr=%08lx layout=%u\n",(unsigned long)hr,layout);
        if(layout<2){CK(FAILED(hr)&&!ps);printf("PASS rejected layout=%u\n",layout);continue;}HR(hr);CK(ps);
        for(UINT variant=0;variant<10;variant++){
            // 0 empty; 1 two waves; 2 active global partitions; 3 short triangle load;
            // 4 short centroid atomic; 5 misaligned centroid VA; 6 invalid node.
            const UINT count=variant==1?33:variant==0||variant==2?0:1, N=4,M=40;
            const bool global=variant==2;const UINT P=global?N:1, part=global?0xfffffffe:P;
            const UINT64 base=UINT64_C(0x10ffffff00), blas=base+512, centroid=base+1024+(variant==5?1:0);
            const UINT readBytes=variant==3?512+64+35:1536, atomBytes=variant==4?1024+23:1280;
            Buf tlas{},src{},dst{},atom{};
            for(UINT i=0;i<W;i++){tlas[i]=0xabcd0000u+i;src[i]=0xacdc0000u+i;dst[i]=0x99110000u+i;atom[i]=0x99aa0000u+i;}
            auto *h=tlas.data()+pad;h[16]=N;h[17]=M;h[19]=global?10:0;h[13]=1024;h[(80+28*P)/4]=count;
            for(UINT i=0;i<count;i++){
                UINT id=count-1-i;h[(108+28*N)/4+P*M+i]=id;
                auto *node=h+256+32*id;std::fill(node,node+32,0);
                node[0]=bits(1);node[5]=bits(-1);node[10]=bits(1);
                node[3]=bits(float(int(i)-20));node[7]=bits(2);node[11]=bits(float(int(i)-17));
                node[14]=variant==6?0:variant==7?13:variant==8?12:variant==9?13:8;node[15]=UINT(blas>>8);
            }
            auto *v=src.data()+pad+(512+64)/4;
            float vertices[]={-3,8,-5, 2,-4,11, 9,12,1};
            memcpy(v,vertices,sizeof vertices);v[12]=0;
            if(variant>=7){
                src[pad+512/4]=variant==9?1u<<20:0;std::fill(v,v+64,0);v[0]=1;
                if(variant==7){float box[]={-2,-4,-6,4,6,8};memcpy(v+4,box,sizeof box);}
                if(variant==8){v[4]=0xc400c000;v[5]=0x4400c600;v[6]=0x48004600;} // fp16: -2,-4,-6,4,6,8
                if(variant==9){for(UINT side=0;side<2;side++)for(UINT k=0;k<6;k++){float pt[]={side?4.f:-2.f,side?6.f:-4.f,side?8.f:-6.f};memcpy(v+18*side+3*k,pt,12);}}
            }
            float box1[]={-9,3,-5,3,7,7},box3[]={-2,-8,5,8,-4,13};
            memcpy(src.data()+pad+(84+28)/4,box1,sizeof box1);memcpy(src.data()+pad+(84+3*28)/4,box3,sizeof box3);
            UINT bb=global?16:84+28*P;
            for(UINT i=0;i<6;i++){atom[pad+bb/4+i]=atom[pad+256+i]=bits(i<3?FLT_MAX:-FLT_MAX);}
            Buf expectDst=dst,expectAtom=atom;
            for(UINT i=0;i<(global?2:count);i++){
                float x=float(int(i)-20),z=float(int(i)-17);
                float box[]={-3+x,variant==3?-6.f:-10.f,-5+z, (variant==3?2.f:9.f)+x,6,11+z};
                if(global)memcpy(box,i?box3:box1,sizeof box);
                if(variant==6) for(UINT j=0;j<6;j++)box[j]=j<3?FLT_MAX:-FLT_MAX;
                if(variant>=7){float bx[]={x-2,-4,z-6,x+4,6,z+8};memcpy(box,bx,sizeof box);}
                memcpy(expectDst.data()+pad+6*i,box,sizeof box);
                auto apply=[&](UINT off,UINT j,float y){UINT k=pad+off/4+j;if(off+4*j+4<=atomBytes)
                    expectAtom[k]=bits(j<3?std::min(val(expectAtom[k]),y):std::max(val(expectAtom[k]),y));};
                for(UINT j=0;j<6;j++){apply(bb,j,box[j]);if(variant!=5)apply(1024,j,(box[j%3]+box[j%3+3])*0.5f);}
            }
            using R=ComPtr<ID3D12Resource>;R in,read,seed1,seed2,out,accum,rb1,rb2;
            auto make=[&](R &rr,D3D12_HEAP_TYPE ht,D3D12_RESOURCE_STATES st){
                D3D12_HEAP_PROPERTIES hp={};hp.Type=ht;hp.CreationNodeMask=hp.VisibleNodeMask=1;
                D3D12_RESOURCE_DESC rd={};rd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;rd.Width=sizeof(Buf);
                rd.Height=rd.DepthOrArraySize=rd.MipLevels=rd.SampleDesc.Count=1;rd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
                if(ht==D3D12_HEAP_TYPE_DEFAULT)rd.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
                return d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,st,nullptr,IID_PPV_ARGS(&rr));
            };
            for(auto rr:{std::addressof(in),std::addressof(read),std::addressof(seed1),std::addressof(seed2)}) HR(make(*rr,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ));
            for(auto rr:{std::addressof(out),std::addressof(accum)})HR(make(*rr,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_COPY_DEST));
            for(auto rr:{std::addressof(rb1),std::addressof(rb2)})HR(make(*rr,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST));
            auto put=[&](R &rr,const Buf &buf){void *ptr=nullptr;auto hr=rr->Map(0,nullptr,&ptr);if(SUCCEEDED(hr)){memcpy(ptr,buf.data(),sizeof buf);rr->Unmap(0,nullptr);}return hr;};
            HR(put(in,tlas));HR(put(read,src));HR(put(seed1,dst));HR(put(seed2,atom));
            D3D12_COMMAND_QUEUE_DESC qd={};qd.Type=D3D12_COMMAND_LIST_TYPE_COMPUTE;
            ComPtr<ID3D12CommandQueue>q;ComPtr<ID3D12CommandAllocator>al;ComPtr<ID3D12GraphicsCommandList>l;
            HR(d->CreateCommandQueue(&qd,IID_PPV_ARGS(&q)));HR(d->CreateCommandAllocator(qd.Type,IID_PPV_ARGS(&al)));HR(d->CreateCommandList(1,qd.Type,al.Get(),ps.Get(),IID_PPV_ARGS(&l)));
            auto transition=[&](R &rr,D3D12_RESOURCE_STATES old,D3D12_RESOURCE_STATES now){D3D12_RESOURCE_BARRIER br={};br.Transition={rr.Get(),0,old,now};l->ResourceBarrier(1,&br);};
            l->CopyResource(out.Get(),seed1.Get());l->CopyResource(accum.Get(),seed2.Get());
            for(auto rr:{std::addressof(out),std::addressof(accum)})transition(*rr,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            UINT pc[16]={part,0,UINT(centroid),UINT(centroid>>32),UINT(base),UINT(base>>32)};
            UINT window[]={UINT(base),UINT(base>>32),readBytes,UINT(base),UINT(base>>32),atomBytes};
            auto addr=[&](R &rr){return rr->GetGPUVirtualAddress()+4*pad;};
            CK(addr(in)!=base&&addr(read)!=base&&addr(accum)!=base);
            l->SetComputeRootSignature(root.Get());l->SetComputeRootShaderResourceView(t,addr(in));l->SetComputeRootUnorderedAccessView(u,addr(out));
            l->SetComputeRoot32BitConstants(b,16,pc,0);l->SetComputeRoot32BitConstants(r,6,window,0);
            l->SetComputeRootShaderResourceView(s,addr(read));l->SetComputeRootUnorderedAccessView(a,addr(accum));
            l->Dispatch(2,1,1);for(auto rr:{std::addressof(out),std::addressof(accum)})transition(*rr,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);
            l->CopyResource(rb1.Get(),out.Get());l->CopyResource(rb2.Get(),accum.Get());HR(l->Close());ID3D12CommandList *lists[]={l.Get()};q->ExecuteCommandLists(1,lists);
            ComPtr<ID3D12Fence>fence;HR(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);CK(event);HR(q->Signal(fence.Get(),1));HR(fence->SetEventOnCompletion(1,event));CK(WaitForSingleObject(event,15000)==WAIT_OBJECT_0&&fence->GetCompletedValue()==1);CloseHandle(event);
            auto check=[&](const char*which,R &rr,const Buf &buf){UINT *m=nullptr;if(FAILED(rr->Map(0,nullptr,(void**)&m)))return false;bool ok=true;
                for(UINT i=0;i<W;i++){if(m[i]!=buf[i]){printf("FAIL %s variant=%u at=%u actual=%08x expected=%08x\n",which,variant,i,m[i],buf[i]);ok=false;break;}}
                rr->Unmap(0,nullptr);return ok;};
            CK(check("AABB",rb1,expectDst));CK(check("atomic",rb2,expectAtom));CK(check("tlas",in,tlas));CK(check("source",read,src));
            printf("PASS layout=%u variant=%u count=%u fenced 4 x %u words\n",layout,variant,count,W);
        }
    }
}
