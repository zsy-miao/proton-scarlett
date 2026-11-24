#include <d3d12.h>
#ifdef TEST_NATIVE_POOL
#include "xmem/native_pool.h"
#endif
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
extern "C" HRESULT xs_color_allocation_info(IUnknown *,UINT,UINT,const void *,D3D12_RESOURCE_ALLOCATION_INFO *);
extern "C" HRESULT xs_copy_wc_view(IUnknown *,SIZE_T,SIZE_T);
extern "C" HRESULT xs_write_view(IUnknown *,UINT,IUnknown *,const void *,SIZE_T);
extern "C" HRESULT xs_resolve_view(IUnknown *,SIZE_T,UINT,ID3D12DescriptorHeap **,D3D12_GPU_DESCRIPTOR_HANDLE *);
extern "C" HRESULT xs_descriptor_range(IUnknown *,SIZE_T,UINT,ID3D12DescriptorHeap **,D3D12_CPU_DESCRIPTOR_HANDLE *,D3D12_GPU_DESCRIPTOR_HANDLE *);
struct xs_texture_use;
extern "C" HRESULT xs_refresh_wc_texture(IUnknown *,SIZE_T,ID3D12GraphicsCommandList *,ID3D12DescriptorHeap **,xs_texture_use **);
extern "C" void xs_drop_texture_use(xs_texture_use *);
#define SLOT(o,n,T) ((T)((*reinterpret_cast<void***>(o))[n/8]))
using Make=HRESULT(WINAPI*)(IUnknown *,const void *,REFIID,void **);
using Start=SIZE_T(WINAPI*)(IUnknown *);
using Writer=void(WINAPI*)(IUnknown *,const void *,const void *,SIZE_T);
using DescriptorCopy=void(WINAPI*)(IUnknown *,UINT,SIZE_T,SIZE_T,UINT);
using Alloc=void*(WINAPI*)(void*,SIZE_T,DWORD,UINT64,DWORD);
using Copy=HRESULT(WINAPI*)(void*,void*,UINT,UINT,const void*,UINT,UINT);
#if defined(TEST_SRGB_ROW)
static constexpr DWORD format=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
#elif defined(TEST_RGBA1_UINT)
static constexpr DWORD format=DXGI_FORMAT_R8G8B8A8_UINT;
#elif defined(TEST_R32_FLOAT)
static constexpr DWORD format=DXGI_FORMAT_R32_FLOAT;
#elif defined(TEST_RGBA1) || defined(TEST_VOLUME1) || defined(TEST_CUBE1) || defined(TEST_ARRAY6) || defined(TEST_CUBE_PAIR) || defined(TEST_ARRAY2)
static constexpr DWORD format=DXGI_FORMAT_R8G8B8A8_UNORM;
#else
static constexpr DWORD format=DXGI_FORMAT_R8_UNORM;
#endif
#ifdef TEST_VOLUME1
static constexpr bool volume=true;
#else
static constexpr bool volume=false;
#endif
#ifdef TEST_CUBE_PAIR
static constexpr UINT layers=12;
#elif defined(TEST_ARRAY2)
static constexpr UINT layers=2;
#elif defined(TEST_CUBE1) || defined(TEST_ARRAY6)
static constexpr UINT layers=6;
#else
static constexpr UINT layers=1;
#endif
#if defined(TEST_ARRAY2) || defined(TEST_ARRAY6)
static constexpr bool arrayView=true;
#else
static constexpr bool arrayView=false;
#endif
static constexpr bool rgba1=format!=DXGI_FORMAT_R8_UNORM;
static constexpr bool integer=format==DXGI_FORMAT_R8G8B8A8_UINT, scalar=format==DXGI_FORMAT_R32_FLOAT;
static DWORD pattern(unsigned pass,unsigned face=0) {
    return scalar ? (pass?0xc0300000u:0x3fb00000u) : ((pass?0x7de30b94u:0xc641a807u) ^ (face*0x0f182537u));
}
static constexpr bool srgb=format==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
static constexpr UINT explicitLayout=srgb?0x100u:0x105u;
static BYTE code(UINT pass,UINT i){return (BYTE)((i/4)*37+(i%4)*67+pass*127);}
static constexpr UINT N=srgb?1024:rgba1?layers*4:32768, Bytes=4*N, tile=rgba1?layers*4096:32768;
static constexpr SIZE_T span=0x89700000u, offset=8192;
int main(){
    auto alloc=(Alloc)(void*)GetProcAddress(LoadLibraryW(L"xmem.dll"),"XMemVirtualAlloc"); CK(alloc);
    BYTE *s=(BYTE*)alloc(nullptr,0x49400000,0x20003000,0x183,0x22004);
    BYTE *b=(BYTE*)alloc(nullptr,span,0x20003000,0x283,0x2404);CK(s&&b);
    DWORD desc[]={srgb?2u:volume?4u:3u,0,0,0,256,0,128,0x10001,61,1,0,0,0x300000};
    if (rgba1) {
        desc[4]=srgb?256:1;desc[6]=1; desc[8]=format; desc[7]=0x10000+layers;
        CK(XGSuggestSwizzleMode(desc,1,&desc[11])==E_NOTIMPL && !desc[11]);
        for (UINT j:{0u,2u,3u,4u,5u,6u,7u,8u,9u,10u,11u,12u}) {
            desc[j]^=1; DWORD before=desc[11]; CK(XGSuggestSwizzleMode(desc,0,&desc[11])==E_NOTIMPL && desc[11]==before); desc[j]^=1;
        }
    }
    CK(XGSuggestSwizzleMode(desc,0,&desc[11])==S_OK && desc[11]==explicitLayout);
    desc[2]=4096;
    void **c=nullptr;HR(XGCreateTextureComputer(desc,(void**)&c));auto v=*(void***)c;auto copy=(Copy)v[8];
    ComPtr<ID3D12Device3>d;HR(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&d)));
#ifdef TEST_NATIVE_POOL
    using Take=HRESULT(WINAPI*)(ID3D12Device*,const void*,SIZE_T,ID3D12Heap**,ID3D12Resource**,UINT*);
    auto take=(Take)(void*)GetProcAddress(GetModuleHandleW(L"xmem.dll"),"ScarlettAcquireNativePoolV1");CK(take);
    UINT start=0;ComPtr<ID3D12Resource> proof;UINT64 incarnation=0;
    CK(!native_pool_ctl(0x53430103,b,&incarnation,sizeof incarnation) && incarnation);
    CK(take(d.Get(),b+span-tile,tile,nullptr,&proof,&start)==S_OK && start==span-tile && proof && proof->GetDesc().Width==span);
    proof.Reset();CK(take(d.Get(),b+span-tile,tile+1,nullptr,&proof,&start)==E_INVALIDARG && !proof);
#endif
    DWORD params[20]={}; params[0]=0x20004;params[2]=0x100000;params[10]=1;params[15]=0x20;
    const IID id={0x5405c344,0xd457,0x444e,{0xb4,0xdd,0x23,0x66,0xe4,0x5a,0xee,0x39}};
    ComPtr<IUnknown> guest,src,dst;
    HR(D3D12XboxCreateDevice(nullptr,params,id,(void**)guest.GetAddressOf()));
    auto make=SLOT(guest.Get(),0x70,Make);
    UINT hd[4]={0,0xc38,0,1}; HR(make(guest.Get(),hd,IID_ID3D12DescriptorHeap,(void**)src.GetAddressOf()));
    hd[1]=4;hd[2]=1;HR(make(guest.Get(),hd,IID_ID3D12DescriptorHeap,(void**)dst.GetAddressOf()));
    auto from=SLOT(src.Get(),0x48,Start)(src.Get()), to=SLOT(dst.Get(),0x48,Start)(dst.Get())+32;
    if (rgba1) {
        using Query=D3D12_RESOURCE_ALLOCATION_INFO *(WINAPI *)(D3D12_RESOURCE_ALLOCATION_INFO *,IUnknown *,UINT,UINT,const void *);
        auto query=SLOT(guest.Get(),0xc8,Query);
#ifdef TEST_NATIVE_POOL
        D3D12_RESOURCE_DESC host={};memcpy(&host,desc,offsetof(D3D12_RESOURCE_DESC,Flags)+sizeof host.Flags);
        host.Layout=D3D12_TEXTURE_LAYOUT_UNKNOWN;host.Flags=D3D12_RESOURCE_FLAG_NONE;
        auto native=d->GetResourceAllocationInfo(1,1,&host);
        printf("native unknown layers=%u bytes=%llu alignment=%llu\n",layers,native.SizeInBytes,native.Alignment);
        CK(native.Alignment==4096 && native.SizeInBytes==4096);
        constexpr UINT small=4096;
#else
        constexpr UINT small=srgb || layers==12?8192:4096;
#endif
        for (UINT layout:{0u,explicitLayout}) {
            D3D12_RESOURCE_ALLOCATION_INFO result={}; desc[11]=layout;
            if (volume && !layout) {
                CK(xs_color_allocation_info(guest.Get(),1,1,desc,&result)==E_NOTIMPL && !result.SizeInBytes && !result.Alignment);
                continue;
            }
            CK(query(&result,guest.Get(),1,1,desc)==&result && result.SizeInBytes==(layout?tile:small) && result.Alignment==4096);
        }
        for (UINT j:{0u,2u,3u,4u,5u,6u,7u,8u,9u,10u,11u,12u}) {
            D3D12_RESOURCE_ALLOCATION_INFO bad={7,8}; void *unused=desc;
            desc[j]^=1;
            CK(FAILED(xs_color_allocation_info(guest.Get(),1,1,desc,&bad)) && !bad.SizeInBytes && !bad.Alignment);
            CK(XGCreateTextureComputer(desc,&unused)==E_NOTIMPL && !unused);
            desc[j]^=1;
        }
    }
    constexpr DWORD dimension=srgb?2u:layers==12?10u:arrayView?5u:layers==6?9u:volume?8u:4u;
    desc[2]=0; DWORD w[16]={format,dimension,0x1688,0,0,1,0,layers==12?2u:arrayView?layers:0u,0,0,0,0,0,0,0,3};
    auto address=[&](BYTE *p){memcpy(w+10,&p,sizeof p);};
    address(b+span-tile); HR(xs_write_address_srv(guest.Get(),desc,w,from));
    SLOT(guest.Get(),0xc0,DescriptorCopy)(guest.Get(),1,to,from,0);
    UINT64 serial=*(UINT64*)from;
    if (rgba1) {
        w[1]=volume?4:8; CK(FAILED(xs_write_address_srv(guest.Get(),desc,w,from)) && *(UINT64*)from==serial); w[1]=dimension;
        w[0]=integer?28:30; // supported formats still cannot reinterpret each other's views
        CK(FAILED(xs_write_address_srv(guest.Get(),desc,w,from)) && *(UINT64*)from==serial); w[0]=format;
    }
    for (UINT j:{0u,2u,3u,4u,5u,6u,7u,8u,9u,10u,11u,12u}) {
        desc[j]^=1; CK(FAILED(xs_write_address_srv(guest.Get(),desc,w,from))); desc[j]^=1;
    }
    for (UINT j:{0u,1u,2u,3u,4u,5u,6u,7u,8u,9u,12u,13u,14u,15u}) {
        w[j]^=1; CK(FAILED(xs_write_address_srv(guest.Get(),desc,w,from))); w[j]^=1;
    }
    for (auto p:{b+span-tile+4096,b+offset+1,s}) {
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
    FILE *f=fopen(srgb?"srgb-row.dxil":layers==12?"cube-pair.dxil":layers==2?"array2.dxil":arrayView?"array6.dxil":layers==6?"cube1.dxil":volume?"volume1.dxil":integer?"rgba1-uint.dxil":scalar?"r32-float.dxil":rgba1?"rgba1.dxil":"sample.dxil","rb");CK(f);BYTE dxil[8192];size_t n=fread(dxil,1,sizeof dxil,f);CK(n && feof(f));fclose(f);
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
        ID3D12DescriptorHeap *table=nullptr; xs_texture_use *hold=nullptr;
        *(UINT64*)to^=1; CK(FAILED(xs_refresh_wc_texture(dst.Get(),to,l.Get(),&table,&hold)) && !table && !hold); *(UINT64*)to^=1;
        HR(xs_refresh_wc_texture(dst.Get(),to,l.Get(),&table,&hold));CK(table && hold);
        l->SetComputeRootSignature(root.Get());ID3D12DescriptorHeap *heaps[]={table};
        l->SetDescriptorHeaps(1,heaps);l->SetComputeRootDescriptorTable(0,table->GetGPUDescriptorHandleForHeapStart());l->SetComputeRootUnorderedAccessView(1,out->GetGPUVirtualAddress());
        l->Dispatch(srgb?4:rgba1?1:16,rgba1?1:16,1);transition(D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);
        l->CopyBufferRegion(read.Get(),pass*Bytes,out.Get(),0,Bytes);HR(l->Close());
        HR(q->Wait(gate.Get(),pass+1));ID3D12CommandList *batch[]={l.Get()};q->ExecuteCommandLists(1,batch);HR(q->Signal(done.Get(),pass+1));
        HR(done->SetEventOnCompletion(pass+1,ev));CK(WaitForSingleObject(ev,50)==WAIT_TIMEOUT);
        // Change the original only after submission so a recording-time snapshot fails.
        if (srgb) {
            BYTE row[1024];for(UINT i=0;i<N;i++)row[i]=code(pass,i);
            CK(FAILED(copy(c,b+offset,0,1,row,N,N)) && FAILED(copy(c,b+offset,1,0,row,N,N)) &&
                FAILED(copy(c,b+offset,0,0,row,N-1,N)) && FAILED(copy(c,b+offset,0,0,row,N,N-1)) &&
                FAILED(copy(c,b+offset,0,0,s,N,N)) && FAILED(copy(c,b+offset+1,0,0,row,N,N)));
            HR(copy(c,b+offset,0,0,row,N,N));SecureZeroMemory(row,sizeof row);
            for(UINT i=0;i<N;i++)CK(b[offset+i]==code(pass,i));
        } else if (rgba1) {
            DWORD color=pattern(pass);
            CK(FAILED(copy(c,b+offset+1,0,0,&color,4,4)) && FAILED(copy(c,b+offset,0,0,&color,3,4)) &&
                FAILED(copy(c,b+offset,0,0,&color,4,5)) && FAILED(copy(c,b+offset,1,0,&color,4,4)) &&
                FAILED(copy(c,b+offset,0,layers,&color,4,4)) && FAILED(copy(c,b+offset,0,0,s,4,4)));
            // Last full tile fits; the four-byte texel does not license a shorter destination.
            if (!pass) {
                memset(b+span-tile,0xbd,tile); HR(copy(c,b+span-tile,0,0,&color,4,4));
                CK(*(DWORD*)(b+span-tile)==color); for(UINT j=4;j<tile;j++) CK(b[span-tile+j]==0xbd);
#ifdef TEST_NATIVE_POOL
                if(layers>1){
                    DWORD last=pattern(pass,layers-1);HR(copy(c,b+span-tile,0,layers-1,&last,4,4));
                    CK(FAILED(copy(c,b+span-tile+4096,0,0,&last,4,4)));
                    CK(*(DWORD*)(b+span-tile)==color && *(DWORD*)(b+span-4096)==last);
                    for(UINT j=4;j<tile;j++)if(j<tile-4096 || j>=tile-4092)CK(b[span-tile+j]==0xbd);
                }
#endif
            }
            if(layers>1) CK(FAILED(copy(c,b+span-tile+4096,0,0,&color,4,4)));
            for(UINT face=0;face<layers;face++) {
                color=pattern(pass,face);HR(copy(c,b+offset,0,face,&color,4,4)); CK(*(DWORD*)(b+offset+4096*face)==color);
            }
        } else {
            for(UINT i=0;i<N;i++)s[i]=pass?i>>8:i&255;
            HR(copy(c,b+offset,0,0,s,256,N));
        }
        MemoryBarrier();
        if(pass){
            for(UINT i=0;i<65536;i++)if(layers>1 ? i<offset || i>=offset+tile || (i-offset)%4096>=4 : i<offset || i>=offset+N)CK(b[i]==0xd7);
            HR(xs_write_view(guest.Get(),0x90,nullptr,nullview,to)); HR(xs_resolve_view(dst.Get(),to,0x90,&raw,&gh));
            dst.Reset();guest.Reset(); // Only the recorded use retains the original buffer now.
            CK(VirtualFree(b,0,MEM_RELEASE));
            CK(VirtualAlloc(b,65536,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE)==b);memset(b,0xad,65536);MemoryBarrier();
        }
#ifdef TEST_NATIVE_POOL
        if(pass){
            DWORD stale=pattern(0);CK(FAILED(copy(c,b+offset,0,layers-1,&stale,4,4)));
            CK(take(d.Get(),b+offset,tile,nullptr,&proof,&start)==S_FALSE && !proof);
            CK(WaitForSingleObject(ev,0)==WAIT_TIMEOUT);
        }
#endif
        HR(gate->Signal(pass+1));CK(WaitForSingleObject(ev,10000)==WAIT_OBJECT_0);
        DWORD *map;HR(read->Map(0,nullptr,(void**)&map));for(unsigned i=0;i<N;i++){
            DWORD bits=map[pass*N+i]; unsigned want=rgba1 ? (pattern(pass,i/4)>>((i%4)*8))&255 : pass?i>>8:i&255;
            if (srgb) {
                float sampled;memcpy(&sampled,&bits,4);CK(std::isfinite(sampled) && sampled>=0 && sampled<=1);
                double encoded=code(pass,i)/255.0;
                if(i%4==3) CK(fabs(sampled-encoded)<1e-7);
                else {
                    // Check the IEC sRGB transfer at every input code, within half an encoded step.
                    auto linear=[](double c){return c<=0.04045?c/12.92:pow((c+0.055)/1.055,2.4);};
                    CK(sampled>=linear(encoded-0.5/255) && sampled<=linear(encoded+0.5/255));
                }
            } else if (integer) CK(bits==want);
            else if (scalar) {
                const DWORD expected[]={pattern(pass),pass?0xc0b00000u:0x40300000u,0,0x3f800000u};
                CK(bits==expected[i]); // Load().r, r*2, missing green=0 and alpha=1
            } else {
                float value; memcpy(&value,&bits,4);
                if(!std::isfinite(value)||fabs(value-want/255.f)>1e-7){fprintf(stderr,"pass %u index %u got %g expected %u/255\n",pass,i,value,want);ExitProcess(1);}
            }
        }read->Unmap(0,nullptr);xs_drop_texture_use(hold);
        if(srgb)printf("PASS Texture1D sRGB pass %u: all 256 codes on each RGB channel, alpha linear, fenced point sampling\n",pass);
        else if (layers>1) printf("PASS %s pass %u: %u distinct faces and %u channels after gated writes\n",layers==12?"TextureCubeArray":arrayView?"Texture2DArray":"TextureCube",pass,layers,N);
        else if (rgba1) printf("PASS format %lu pass %u: all four shader results for original WC word %#010lx\n",format,pass,pattern(pass));
        else printf("PASS pass %u: all 32768 %c coordinates as R8_UNORM; checked gate held before CPU producer\n",pass,pass?'Y':'X');
    }
    for(UINT i=0;i<65536;i++)CK(b[i]==0xad);
    CK(VirtualFree(b,0,MEM_RELEASE)&&VirtualFree(s,0,MEM_RELEASE));CloseHandle(ev);
    CK(((ULONG(WINAPI*)(void*))v[1])(c)==0);HR(d->GetDeviceRemovedReason());
    if(srgb)puts("PASS linear row snapshot, slot copy, padding/adjacent guards, heap release and exact VA reuse");
    else if (layers>1) printf("PASS guest allocation %u/4096, face padding and adjacent guards intact; copied descriptor survived heap release and exact VA reuse\n",tile);
    else if (rgba1) puts("PASS RGBA1 allocation/copy: 4096/4096, all 4092 padding bytes and adjacent guards unchanged; copied view retained across VA reuse");
    else puts("PASS +230 and +c0 copied view: refresh before native Texture2D sample; overwrites, bounds, heap release, VA reuse and guards");
}
