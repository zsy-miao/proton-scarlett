#include <d3d12.h>
#include <cstdint>
#include <wrl/client.h>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <initializer_list>
using Microsoft::WRL::ComPtr;
#define CK(x) do{if(!(x)){fprintf(stderr,"FAIL %d %s err=%lu\n",__LINE__,#x,GetLastError());ExitProcess(1);}}while(0)
#define HR(x) do{HRESULT h_=(x);if(FAILED(h_)){fprintf(stderr,"FAIL %d %s hr=%08lx\n",__LINE__,#x,(unsigned long)h_);ExitProcess(1);}}while(0)
extern "C" __declspec(dllimport) HRESULT WINAPI XGCreateTextureComputer(const void *,void **);
extern "C" __declspec(dllimport) HRESULT WINAPI XGSuggestSwizzleMode(const void *,uintptr_t,DWORD *);
extern "C" HRESULT WINAPI D3D12XboxCreateDevice(void *,const void *,REFIID,void **);
extern "C" HRESULT xs_write_address_srv(IUnknown *,const void *,const void *,SIZE_T);
extern "C" HRESULT xs_copy_wc_view(IUnknown *,SIZE_T,SIZE_T);
extern "C" HRESULT xs_write_view(IUnknown *,UINT,IUnknown *,const void *,SIZE_T);
extern "C" HRESULT xs_resolve_view(IUnknown *,SIZE_T,UINT,ID3D12DescriptorHeap **,D3D12_GPU_DESCRIPTOR_HANDLE *);
extern "C" HRESULT xs_descriptor_range(IUnknown *,SIZE_T,UINT,ID3D12DescriptorHeap **,D3D12_CPU_DESCRIPTOR_HANDLE *,D3D12_GPU_DESCRIPTOR_HANDLE *);
struct xs_r8_use;
extern "C" HRESULT xs_refresh_r8(IUnknown *,SIZE_T,ID3D12GraphicsCommandList *,ID3D12DescriptorHeap **,xs_r8_use **);
extern "C" void xs_drop_r8_use(xs_r8_use *);
#define SLOT(o,n,T) ((T)((*reinterpret_cast<void***>(o))[n/8]))
using Make=HRESULT(WINAPI*)(IUnknown *,const void *,REFIID,void **);
using Start=SIZE_T(WINAPI*)(IUnknown *);
using Writer=void(WINAPI*)(IUnknown *,const void *,const void *,SIZE_T);
using DescriptorCopy=void(WINAPI*)(IUnknown *,UINT,SIZE_T,SIZE_T,UINT);
using Alloc=void*(WINAPI*)(void*,SIZE_T,DWORD,UINT64,DWORD);
using Copy=HRESULT(WINAPI*)(void*,void*,UINT,UINT,const void*,UINT,UINT);
static constexpr UINT N=32768,Bytes=4*N;
static constexpr SIZE_T span=0x89700000u, offset=8192;
int main(){
    auto alloc=(Alloc)(void*)GetProcAddress(LoadLibraryW(L"xmem.dll"),"XMemVirtualAlloc"); CK(alloc);
    BYTE *s=(BYTE*)alloc(nullptr,0x49400000,0x20003000,0x183,0x22004);
    BYTE *b=(BYTE*)alloc(nullptr,span,0x20003000,0x283,0x2404);CK(s&&b);
    DWORD desc[]={3,0,0,0,256,0,128,0x10001,61,1,0,0,0x300000};
    CK(XGSuggestSwizzleMode(desc,0,&desc[11])==S_OK && desc[11]==0x105);
    desc[2]=4096;
    void **c=nullptr;HR(XGCreateTextureComputer(desc,(void**)&c));auto v=*(void***)c;auto copy=(Copy)v[8];
    ComPtr<ID3D12Device3>d;HR(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&d)));
    DWORD params[20]={}; params[0]=0x20004;params[2]=0x100000;params[10]=1;params[15]=0x20;
    const IID id={0x5405c344,0xd457,0x444e,{0xb4,0xdd,0x23,0x66,0xe4,0x5a,0xee,0x39}};
    ComPtr<IUnknown> guest,src,dst;
    HR(D3D12XboxCreateDevice(nullptr,params,id,(void**)guest.GetAddressOf()));
    auto make=SLOT(guest.Get(),0x70,Make);
    UINT hd[4]={0,0xc38,0,1}; HR(make(guest.Get(),hd,IID_ID3D12DescriptorHeap,(void**)src.GetAddressOf()));
    hd[1]=4;hd[2]=1;HR(make(guest.Get(),hd,IID_ID3D12DescriptorHeap,(void**)dst.GetAddressOf()));
    auto from=SLOT(src.Get(),0x48,Start)(src.Get()), to=SLOT(dst.Get(),0x48,Start)(dst.Get())+32;
    desc[2]=0; DWORD w[16]={61,4,0x1688,0,0,1,0,0,0,0,0,0,0,0,0,3};
    auto address=[&](BYTE *p){memcpy(w+10,&p,sizeof p);};
    address(b+span-N); HR(xs_write_address_srv(guest.Get(),desc,w,from));
    SLOT(guest.Get(),0xc0,DescriptorCopy)(guest.Get(),1,to,from,0);
    UINT64 serial=*(UINT64*)from;
    for (UINT j:{0u,2u,3u,4u,5u,6u,7u,8u,9u,10u,11u,12u}) {
        desc[j]^=1; CK(FAILED(xs_write_address_srv(guest.Get(),desc,w,from))); desc[j]^=1;
    }
    for (UINT j:{0u,1u,2u,3u,4u,5u,6u,7u,8u,9u,12u,13u,14u,15u}) {
        w[j]^=1; CK(FAILED(xs_write_address_srv(guest.Get(),desc,w,from))); w[j]^=1;
    }
    for (auto p:{b+span-N+4096,b+offset+1,s}) {
        address(p);CK(FAILED(xs_write_address_srv(guest.Get(),desc,w,from)));
    }
    CK(*(UINT64*)from==serial);
    address(b+offset);CK(FAILED(xs_write_address_srv(guest.Get(),desc,w,from+1)));
    SLOT(guest.Get(),0x230,Writer)(guest.Get(),desc,w,from);
    CK(FAILED(xs_copy_wc_view(guest.Get(),to+1,from)));
    SLOT(guest.Get(),0xc0,DescriptorCopy)(guest.Get(),1,to,from,0);
    CK(*(UINT64*)from!=*(UINT64*)to);
    DWORD nullview[10]={42,4,0x1688};HR(xs_write_view(guest.Get(),0x90,nullptr,nullview,from));
    CK(FAILED(xs_copy_wc_view(guest.Get(),to,from))); src.Reset();
    ID3D12DescriptorHeap *raw; D3D12_GPU_DESCRIPTOR_HANDLE gh; D3D12_CPU_DESCRIPTOR_HANDLE ch;
    CK(xs_resolve_view(dst.Get(),to,0x90,&raw,&gh)==E_NOTIMPL && !raw && !gh.ptr);
    CK(xs_descriptor_range(dst.Get(),to-32,2,&raw,&ch,&gh)==E_NOTIMPL && !raw && !gh.ptr && !ch.ptr);
    D3D12_DESCRIPTOR_RANGE range={D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,0,0,0};
    D3D12_ROOT_PARAMETER p[2]={};p[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;p[0].DescriptorTable={1,&range};
    p[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV;
    D3D12_STATIC_SAMPLER_DESC sampler={}; sampler.Filter=D3D12_FILTER_MIN_MAG_MIP_POINT;
    sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.MaxLOD=12; sampler.MaxAnisotropy=1; sampler.ComparisonFunc=D3D12_COMPARISON_FUNC_NEVER;
    D3D12_ROOT_SIGNATURE_DESC rs={2,p,1,&sampler,D3D12_ROOT_SIGNATURE_FLAG_NONE};
    ComPtr<ID3DBlob> blob;HR(D3D12SerializeRootSignature(&rs,D3D_ROOT_SIGNATURE_VERSION_1,&blob,nullptr));
    ComPtr<ID3D12RootSignature> root;HR(d->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&root)));
    FILE *f=fopen("sample.dxil","rb");CK(f);BYTE dxil[8192];size_t n=fread(dxil,1,sizeof dxil,f);CK(n && feof(f));fclose(f);
    D3D12_COMPUTE_PIPELINE_STATE_DESC pd={};pd.pRootSignature=root.Get();pd.CS={dxil,n};
    ComPtr<ID3D12PipelineState> ps;HR(d->CreateComputePipelineState(&pd,IID_PPV_ARGS(&ps)));
    D3D12_HEAP_PROPERTIES hp={};hp.Type=D3D12_HEAP_TYPE_DEFAULT;hp.CreationNodeMask=hp.VisibleNodeMask=1;
    D3D12_RESOURCE_DESC rd={};rd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;rd.Width=Bytes;rd.Height=rd.DepthOrArraySize=rd.MipLevels=rd.SampleDesc.Count=1;
    rd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;rd.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    ComPtr<ID3D12Resource> out,read;HR(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&out)));
    hp.Type=D3D12_HEAP_TYPE_READBACK;rd.Width=2*Bytes;rd.Flags=D3D12_RESOURCE_FLAG_NONE;HR(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&read)));
    D3D12_COMMAND_QUEUE_DESC qd={};
#ifndef TEST_R8_DIRECT
    qd.Type=D3D12_COMMAND_LIST_TYPE_COMPUTE;
#endif
    ComPtr<ID3D12CommandQueue> q;HR(d->CreateCommandQueue(&qd,IID_PPV_ARGS(&q)));ComPtr<ID3D12Fence> done,gate;
    HR(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&done)));HR(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&gate)));
    ComPtr<ID3D12CommandAllocator> a;HR(d->CreateCommandAllocator(qd.Type,IID_PPV_ARGS(&a)));
    ComPtr<ID3D12GraphicsCommandList> l;HR(d->CreateCommandList(0,qd.Type,a.Get(),ps.Get(),IID_PPV_ARGS(&l)));
    HANDLE ev=CreateEventW(nullptr,FALSE,FALSE,nullptr);CK(ev);
    memset(b,0xd7,65536);
    for(unsigned pass=0;pass<2;pass++){
        if(pass){HR(a->Reset());HR(l->Reset(a.Get(),ps.Get()));}
        auto transition=[&](D3D12_RESOURCE_STATES from,D3D12_RESOURCE_STATES to){
            D3D12_RESOURCE_BARRIER br={};br.Transition={out.Get(),0,from,to};l->ResourceBarrier(1,&br);
        };
        if(pass)transition(D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        ID3D12DescriptorHeap *table=nullptr; xs_r8_use *hold=nullptr;
        *(UINT64*)to^=1; CK(FAILED(xs_refresh_r8(dst.Get(),to,l.Get(),&table,&hold)) && !table && !hold); *(UINT64*)to^=1;
        HR(xs_refresh_r8(dst.Get(),to,l.Get(),&table,&hold));CK(table && hold);
        l->SetComputeRootSignature(root.Get());ID3D12DescriptorHeap *heaps[]={table};
        l->SetDescriptorHeaps(1,heaps);l->SetComputeRootDescriptorTable(0,table->GetGPUDescriptorHandleForHeapStart());l->SetComputeRootUnorderedAccessView(1,out->GetGPUVirtualAddress());
        l->Dispatch(16,16,1);transition(D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);
        l->CopyBufferRegion(read.Get(),pass*Bytes,out.Get(),0,Bytes);HR(l->Close());
        HR(q->Wait(gate.Get(),pass+1));ID3D12CommandList *batch[]={l.Get()};q->ExecuteCommandLists(1,batch);HR(q->Signal(done.Get(),pass+1));
        HR(done->SetEventOnCompletion(pass+1,ev));CK(WaitForSingleObject(ev,50)==WAIT_TIMEOUT);
        // Change the original only after submission so a recording-time snapshot fails.
        for(UINT i=0;i<N;i++)s[i]=pass?i>>8:i&255;
        HR(copy(c,b+offset,0,0,s,256,N));MemoryBarrier();
        if(pass){
            for(UINT i=0;i<65536;i++)if(i<offset || i>=offset+N)CK(b[i]==0xd7);
            HR(xs_write_view(guest.Get(),0x90,nullptr,nullview,to)); HR(xs_resolve_view(dst.Get(),to,0x90,&raw,&gh));
            dst.Reset();guest.Reset(); // Only the recorded use retains the original buffer now.
            CK(VirtualFree(b,0,MEM_RELEASE));
            CK(VirtualAlloc(b,65536,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE)==b);memset(b,0xad,65536);MemoryBarrier();
        }
        HR(gate->Signal(pass+1));CK(WaitForSingleObject(ev,10000)==WAIT_OBJECT_0);
        float *map;HR(read->Map(0,nullptr,(void**)&map));for(unsigned i=0;i<N;i++){
            float value=map[pass*N+i];unsigned want=pass?i>>8:i&255;
            if(!std::isfinite(value)||fabs(value-want/255.f)>1e-7){fprintf(stderr,"pass %u index %u got %g expected %u/255\n",pass,i,value,want);ExitProcess(1);}
        }read->Unmap(0,nullptr);xs_drop_r8_use(hold);
        printf("PASS pass %u: all 32768 %c coordinates as R8_UNORM; checked gate held before CPU producer\n",pass,pass?'Y':'X');
    }
    for(UINT i=0;i<65536;i++)CK(b[i]==0xad);
    CK(VirtualFree(b,0,MEM_RELEASE)&&VirtualFree(s,0,MEM_RELEASE));CloseHandle(ev);
    CK(((ULONG(WINAPI*)(void*))v[1])(c)==0);HR(d->GetDeviceRemovedReason());
    puts("PASS +230 and +c0 copied view: refresh before native Texture2D sample; overwrites, bounds, heap release, VA reuse and guards");
}
