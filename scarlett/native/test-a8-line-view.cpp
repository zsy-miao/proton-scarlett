// Compare the refreshed guest view with default and explicit host component mappings.
#include "xmem/gpu_buffer.hpp"
#include <wrl/client.h>
#include <cstdio>
#include <initializer_list>
#include <cmath>
using Microsoft::WRL::ComPtr;
#define CK(e) do{if(!(e)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#e);return 1;}}while(0)
#define HR(e) do{HRESULT h_=(e);if(FAILED(h_)){fprintf(stderr,"FAIL %d: %s hr=%08lx\n",__LINE__,#e,(unsigned long)h_);return 1;}}while(0)
extern "C" HRESULT WINAPI D3D12XboxCreateDevice(void *,const void *,REFIID,void **);
extern "C" HRESULT xs_write_address_srv(IUnknown *,const void *,const void *,SIZE_T);
struct xs_texture_use;
extern "C" HRESULT xs_refresh_wc_texture(IUnknown *,SIZE_T,ID3D12GraphicsCommandList *,ID3D12DescriptorHeap **,xs_texture_use **);
extern "C" void xs_drop_texture_use(xs_texture_use *);
#define SLOT(o,n,T) ((T)((*reinterpret_cast<void***>(o))[n/8]))
using Make=HRESULT(WINAPI*)(IUnknown *,const void *,REFIID,void **);
using Start=SIZE_T(WINAPI*)(IUnknown *);
using Copier=void(WINAPI*)(IUnknown *,UINT,SIZE_T,SIZE_T,UINT);
static BYTE alpha(UINT pass,UINT x){return BYTE(2*((53*x+11)%128)+pass);}
int main(){
    constexpr UINT W=128,span=256,guard=256,total=2*guard+span,off=0xe6cf800,pool=0x89700000;
    constexpr UINT plane=W*4*sizeof(float);
    auto xm=LoadLibraryW(L"xmem.dll"),xg=LoadLibraryW(L"xg_xs.dll"); CK(xm && xg);
    auto alloc=(void*(WINAPI*)(void*,SIZE_T,DWORD,UINT64,DWORD))(void*)GetProcAddress(xm,"XMemVirtualAlloc");
    auto take=(HRESULT(WINAPI*)(ID3D12Device*,const void*,SIZE_T,ID3D12Heap**,ID3D12Resource**,UINT*))(void*)GetProcAddress(xm,"ScarlettAcquireNativePoolV1");
    auto create=(HRESULT(WINAPI*)(const void*,void**))(void*)GetProcAddress(xg,"XGCreateTextureComputer");
    CK(alloc && take && create);
    ComPtr<ID3D12Device3>d;HR(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&d)));
    constexpr DXGI_FORMAT fmt=DXGI_FORMAT_A8_UNORM;
    D3D12_FEATURE_DATA_FORMAT_SUPPORT fs={};fs.Format=fmt;HR(d->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT,&fs,sizeof fs));
    printf("A8_UNORM Support1=%x Support2=%x\n",(unsigned)fs.Support1,(unsigned)fs.Support2);
    constexpr UINT need=D3D12_FORMAT_SUPPORT1_TEXTURE1D|D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE;
    CK((fs.Support1&need)==need);
    BYTE *b=(BYTE*)alloc(nullptr,pool,0x20003000,0x283,0x2404);CK(b);
    BYTE *at=b+off,*window=b+(off&~65535u);
    ComPtr<ID3D12Resource> native;ImportedBuffer section;UINT pos=0;
    HRESULT pin=take(d.Get(),at-guard,total,nullptr,native.GetAddressOf(),&pos);
    if(pin!=S_OK){
        scarlett_section_info info;CK(pin==S_FALSE);HR(scarlett_query_section(0x53430005,at-guard,total,&info));
        CK(info.offset==off-guard && info.view_size==pool);
        HRESULT h=section.wc(d.Get(),info.view_size,info.section,nullptr);CloseHandle(info.section);HR(h);
    }
    else CK(pos==off-guard);
    auto original=native?native.Get():section.resource();CK(original && original->GetDesc().Width==pool);
    DWORD xd[]={2,0,0,0,W,0,1,0x10001,fmt,1,0,0,0x300000};void *c=nullptr;HR(create(xd,&c));CK(c);
    auto vt=*(void***)c;auto copy=(HRESULT(WINAPI*)(void*,void*,UINT,UINT,const void*,UINT,UINT))(void*)vt[8];
    DWORD params[20]={};params[0]=0x20004;params[2]=0x100000;params[10]=1;params[15]=0x20;
    IID id={0x5405c344,0xd457,0x444e,{0xb4,0xdd,0x23,0x66,0xe4,0x5a,0xee,0x39}};
    ComPtr<IUnknown> guest,cpuheap,gpuheap;HR(D3D12XboxCreateDevice(nullptr,params,id,(void**)guest.GetAddressOf()));
    UINT hdw[]={0,0xc38,0,1};auto make=SLOT(guest.Get(),0x70,Make);
    HR(make(guest.Get(),hdw,IID_ID3D12DescriptorHeap,(void**)cpuheap.GetAddressOf()));hdw[1]=4;hdw[2]=1;
    HR(make(guest.Get(),hdw,IID_ID3D12DescriptorHeap,(void**)gpuheap.GetAddressOf()));
    SIZE_T srcslot=SLOT(cpuheap.Get(),0x48,Start)(cpuheap.Get()),slot=SLOT(gpuheap.Get(),0x48,Start)(gpuheap.Get())+32;
    DWORD w[16]={fmt,2,0x1688,0,0,1,0,0,0,0,0,0,0,0,0,3};memcpy(w+10,&at,8);
    HR(xs_write_address_srv(guest.Get(),xd,w,srcslot));
    for(UINT i: {0u,1u,2u,3u,4u,5u,6u,7u,8u,9u,12u,13u,14u,15u}){w[i]^=1;CK(FAILED(xs_write_address_srv(guest.Get(),xd,w,srcslot)));w[i]^=1;}
    SLOT(guest.Get(),0xc0,Copier)(guest.Get(),1,slot,srcslot,0);cpuheap.Reset();
    memset(at-guard,0xb7,total);

    D3D12_HEAP_PROPERTIES hp={};hp.Type=D3D12_HEAP_TYPE_DEFAULT;hp.CreationNodeMask=hp.VisibleNodeMask=1;
    D3D12_RESOURCE_DESC rd={};rd.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE1D;rd.Format=fmt;rd.Width=W;
    rd.Height=rd.DepthOrArraySize=rd.MipLevels=rd.SampleDesc.Count=1;
    ComPtr<ID3D12Resource> tex,out,read;
    HR(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&tex)));
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp={};UINT rows;UINT64 row;
    d->GetCopyableFootprints(&rd,0,1,off,&fp,&rows,&row,nullptr);
    CK(fp.Offset==off && rows==1 && row==W && fp.Footprint.RowPitch==span && fp.Footprint.Format==fmt);
    rd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;rd.Format=DXGI_FORMAT_UNKNOWN;rd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    rd.Width=3*plane;rd.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    HR(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&out)));
    rd.Width=3*plane+total;rd.Flags=D3D12_RESOURCE_FLAG_NONE;hp.Type=D3D12_HEAP_TYPE_READBACK;
    HR(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&read)));
    ComPtr<ID3D12DescriptorHeap> src,dst;D3D12_DESCRIPTOR_HEAP_DESC hd={};hd.NumDescriptors=2;
    HR(d->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&src)));hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    HR(d->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&dst)));
    const UINT inc=d->GetDescriptorHandleIncrementSize(hd.Type);
    D3D12_SHADER_RESOURCE_VIEW_DESC srv={};srv.Format=fmt;srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE1D;srv.Texture1D.MipLevels=1;
    auto cpu=src->GetCPUDescriptorHandleForHeapStart();
    srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    d->CreateShaderResourceView(tex.Get(),&srv,cpu);cpu.ptr+=inc;
    // Positive control: route alpha to R, force G=1 and B=A=0.
    srv.Shader4ComponentMapping=D3D12_ENCODE_SHADER_4_COMPONENT_MAPPING(3,5,4,4);
    d->CreateShaderResourceView(tex.Get(),&srv,cpu);
    d->CopyDescriptorsSimple(2,dst->GetCPUDescriptorHandleForHeapStart(),src->GetCPUDescriptorHandleForHeapStart(),hd.Type);
    d->CreateShaderResourceView(nullptr,&srv,cpu);cpu.ptr-=inc;d->CreateShaderResourceView(nullptr,&srv,cpu);src.Reset();

    D3D12_DESCRIPTOR_RANGE range={D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,0,0,0};
    D3D12_ROOT_PARAMETER rp[2]={};rp[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;rp[0].DescriptorTable={1,&range};
    rp[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV;
    D3D12_STATIC_SAMPLER_DESC ss={};ss.Filter=D3D12_FILTER_MIN_MAG_MIP_POINT;ss.AddressU=ss.AddressV=ss.AddressW=D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    ss.MaxAnisotropy=1;ss.ComparisonFunc=D3D12_COMPARISON_FUNC_NEVER;
    D3D12_ROOT_SIGNATURE_DESC rs={2,rp,1,&ss,D3D12_ROOT_SIGNATURE_FLAG_NONE};
    ComPtr<ID3DBlob> blob;HR(D3D12SerializeRootSignature(&rs,D3D_ROOT_SIGNATURE_VERSION_1,&blob,nullptr));
    ComPtr<ID3D12RootSignature> root;HR(d->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&root)));
    FILE *f=fopen("a8-line.dxil","rb");CK(f);BYTE shader[8192];size_t n=fread(shader,1,sizeof shader,f);CK(n && feof(f));fclose(f);
    D3D12_COMPUTE_PIPELINE_STATE_DESC pd={};pd.pRootSignature=root.Get();pd.CS={shader,n};
    ComPtr<ID3D12PipelineState> ps;HR(d->CreateComputePipelineState(&pd,IID_PPV_ARGS(&ps)));
    D3D12_COMMAND_QUEUE_DESC qd={};ComPtr<ID3D12CommandQueue> q;ComPtr<ID3D12CommandAllocator>a;ComPtr<ID3D12GraphicsCommandList>l;
    HR(d->CreateCommandQueue(&qd,IID_PPV_ARGS(&q)));HR(d->CreateCommandAllocator(qd.Type,IID_PPV_ARGS(&a)));
    HR(d->CreateCommandList(0,qd.Type,a.Get(),ps.Get(),IID_PPV_ARGS(&l)));
    ComPtr<ID3D12Fence> gate,done;HR(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&gate)));
    HR(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&done)));HANDLE ev=CreateEventW(nullptr,FALSE,FALSE,nullptr);CK(ev);
    for(UINT pass=0;pass<2;pass++){
        if(pass){HR(a->Reset());HR(l->Reset(a.Get(),ps.Get()));}
        auto barrier=[&](ID3D12Resource *r,D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after){
            D3D12_RESOURCE_BARRIER br={};br.Transition={r,0,before,after};l->ResourceBarrier(1,&br);
        };
        if(pass)barrier(out.Get(),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        D3D12_TEXTURE_COPY_LOCATION from={},to={};from.pResource=original;from.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;from.PlacedFootprint=fp;
        to.pResource=tex.Get();to.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;l->CopyTextureRegion(&to,0,0,0,&from,nullptr);
        barrier(tex.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        l->SetComputeRootSignature(root.Get());ID3D12DescriptorHeap *heaps[]={dst.Get()};l->SetDescriptorHeaps(1,heaps);
        auto gpu=dst->GetGPUDescriptorHandleForHeapStart();
        for(UINT j=0;j<2;j++,gpu.ptr+=inc){
            l->SetComputeRootDescriptorTable(0,gpu);l->SetComputeRootUnorderedAccessView(1,out->GetGPUVirtualAddress()+j*plane);l->Dispatch(W/64,1,1);
        }
        ID3D12DescriptorHeap *refreshed=NULL;xs_texture_use *hold=NULL;
        HR(xs_refresh_wc_texture(gpuheap.Get(),slot,l.Get(),&refreshed,&hold));
        l->SetDescriptorHeaps(1,&refreshed);l->SetComputeRootDescriptorTable(0,refreshed->GetGPUDescriptorHandleForHeapStart());
        l->SetComputeRootUnorderedAccessView(1,out->GetGPUVirtualAddress()+2*plane);l->Dispatch(W/64,1,1);
        barrier(tex.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_DEST);
        barrier(out.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);
        l->CopyBufferRegion(read.Get(),0,out.Get(),0,3*plane);l->CopyBufferRegion(read.Get(),3*plane,original,off-guard,total);
        HR(l->Close());HR(q->Wait(gate.Get(),pass+1));ID3D12CommandList *batch[]={l.Get()};q->ExecuteCommandLists(1,batch);
        HR(q->Signal(done.Get(),pass+1));HR(done->SetEventOnCompletion(pass+1,ev));CK(WaitForSingleObject(ev,0)==WAIT_TIMEOUT);
        BYTE stack[W];for(UINT x=0;x<W;x++)stack[x]=alpha(pass,x);
        CK(copy(c,at,0,0,stack,W,W)==S_OK);SecureZeroMemory(stack,sizeof stack);MemoryBarrier();
        if(pass){
            CK(VirtualFree(b,0,MEM_RELEASE) && VirtualAlloc(window,65536,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE)==window);
            memset(window,0xad,65536);CK(FAILED(copy(c,at,0,0,stack,W,W)));
        }
        HR(gate->Signal(pass+1));CK(WaitForSingleObject(ev,10000)==WAIT_OBJECT_0);
        BYTE *p;HR(read->Map(0,nullptr,(void**)&p));
        for(UINT x=0;x<W;x++){
            float v=alpha(pass,x)/255.f,want[]={0,0,0,v,v,1,0,0,0,0,0,v};
            for(UINT j=0;j<12;j++){float got;memcpy(&got,p+(j/4)*plane+16*x+4*(j%4),4);
                if(!(std::fabs(got-want[j])<=1e-7f)){fprintf(stderr,"pass=%u x=%u view=%u channel=%u got=%g want=%g\n",pass,x,j/4,j%4,got,want[j]);return 1;}}
        }
        for(UINT i=0;i<total;i++)CK(p[3*plane+i]==(i>=guard && i<guard+W?alpha(pass,i-guard):0xb7));
        read->Unmap(0,nullptr);xs_drop_texture_use(hold);
    }
    for(UINT i=0;i<65536;i++)CK(window[i]==0xad);
    CK(VirtualFree(window,0,MEM_RELEASE) && ((ULONG(WINAPI*)(void*))vt[1])(c)==0);CloseHandle(ev);HR(d->GetDeviceRemovedReason());
    puts("PASS copied guest and host A8 Texture1D: 256 alpha codes, both copied mappings, original backing/guards across VA reuse");
}
