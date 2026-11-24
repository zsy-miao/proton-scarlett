#include <d3d12.h>
#include <cstdint>
#include <wrl/client.h>
#include <cstdio>
#include <cstdlib>

#include <initializer_list>
#include <utility>
using Microsoft::WRL::ComPtr;
#define CK(x) do{if(!(x)){fprintf(stderr,"FAIL %d %s err=%lu\n",__LINE__,#x,GetLastError());ExitProcess(1);}}while(0)
#define HR(x) do{HRESULT h_=(x);if(FAILED(h_)){fprintf(stderr,"FAIL %d %s hr=%08lx\n",__LINE__,#x,(unsigned long)h_);ExitProcess(1);}}while(0)
extern "C" HRESULT WINAPI D3D12XboxCreateDevice(void *,const void *,REFIID,void **);
extern "C" HRESULT xs_write_address_srv(IUnknown *,const void *,const void *,SIZE_T);
extern "C" HRESULT xs_copy_wc_view(IUnknown *,SIZE_T,SIZE_T);
extern "C" HRESULT xs_write_view(IUnknown *,UINT,IUnknown *,const void *,SIZE_T);
extern "C" HRESULT xs_resolve_view(IUnknown *,SIZE_T,UINT,ID3D12DescriptorHeap **,D3D12_GPU_DESCRIPTOR_HANDLE *);
#define SLOT(o,n,T) ((T)((*reinterpret_cast<void***>(o))[n/8]))
using Make=HRESULT(WINAPI*)(IUnknown *,const void *,REFIID,void **);
using Start=SIZE_T(WINAPI*)(IUnknown *);
using Writer=void(WINAPI*)(IUnknown *,const void *,const void *,SIZE_T);
using DescriptorCopy=void(WINAPI*)(IUnknown *,UINT,SIZE_T,SIZE_T,UINT);
using Alloc=void*(WINAPI*)(void*,SIZE_T,DWORD,UINT64,DWORD);
constexpr SIZE_T span=0x89700000u;
#ifdef STRIDE
struct xs_texture_use;
extern "C" HRESULT xs_refresh_wc_texture(IUnknown *,SIZE_T,ID3D12GraphicsCommandList *,ID3D12DescriptorHeap **,xs_texture_use **);
extern "C" void xs_drop_texture_use(xs_texture_use *);
#ifndef ELEMENTS
#define ELEMENTS 1
#endif
constexpr UINT width=STRIDE*ELEMENTS,results=(width+STRIDE)/4+2;
#if defined(TEST_NATIVE_POOL) && !defined(TEST_WC_OFFSET) && STRIDE==2048
#define TEST_WC_OFFSET 0xe5c6200
#endif
#ifndef TEST_WC_OFFSET
#define TEST_WC_OFFSET (STRIDE==180?0xe5c6a00:STRIDE==320?0x1c80:0x6200)
#endif
constexpr SIZE_T offset=TEST_WC_OFFSET;
#else
constexpr UINT width=160,results=65;
constexpr SIZE_T offset=0x1b80;
#endif
#ifdef TEST_RAW32
#include "test-raw32-setup.hpp"
#endif
int main(){
#ifdef TEST_RAW32
    Raw32Setup raw;
    auto &d=raw.d;
    ID3D12DescriptorHeap *bank=raw.visible.Get();
    auto gh=bank->GetGPUDescriptorHandleForHeapStart();
#else
    auto alloc=(Alloc)(void*)GetProcAddress(LoadLibraryW(L"xmem.dll"),"XMemVirtualAlloc"); CK(alloc);
    BYTE *b=(BYTE*)alloc(nullptr,span,0x20003000,0x283,0x2404); CK(b);
    DWORD desc[]={1,0,0,0,width,0,1,0x10001,0,1,0,1,0};
    ComPtr<ID3D12Device3>d;HR(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&d)));
#ifdef TEST_NATIVE_POOL
    using Take=HRESULT(WINAPI*)(ID3D12Device*,const void*,SIZE_T,ID3D12Heap**,ID3D12Resource**,UINT*);
    auto take=(Take)(void*)GetProcAddress(GetModuleHandleW(L"xmem.dll"),"ScarlettAcquireNativePoolV1");UINT off=0;ComPtr<ID3D12Resource> proof;
    CK(take && take(d.Get(),b+offset,width,nullptr,&proof,&off)==S_OK && off==offset && off%STRIDE==offset%STRIDE && proof->GetDesc().Width==span);
    printf("retained buffer offset=%x width=%u stride=%u remainder=%u\n",off,width,STRIDE,off%STRIDE);
#endif
    DWORD params[20]={}; params[0]=0x20004;params[2]=0x100000;params[10]=1;params[15]=0x20;
    const IID id={0x5405c344,0xd457,0x444e,{0xb4,0xdd,0x23,0x66,0xe4,0x5a,0xee,0x39}};
    ComPtr<IUnknown> guest,src,dst;
    HR(D3D12XboxCreateDevice(nullptr,params,id,(void**)guest.GetAddressOf()));
    auto make=SLOT(guest.Get(),0x70,Make);
    UINT hd[4]={0,0xc38,0,1}; HR(make(guest.Get(),hd,IID_ID3D12DescriptorHeap,(void**)src.GetAddressOf()));
    hd[1]=4;hd[2]=1;HR(make(guest.Get(),hd,IID_ID3D12DescriptorHeap,(void**)dst.GetAddressOf()));
    auto from=SLOT(src.Get(),0x48,Start)(src.Get()), to=SLOT(dst.Get(),0x48,Start)(dst.Get())+32;
#ifdef STRIDE
    DWORD w[16]={0,1,0x1688,0,0,0,ELEMENTS,STRIDE};
#ifdef TEST_STRUCT192
    static_assert(STRIDE==32 && ELEMENTS==6 && !(offset%256));
    w[9]=0x800100;
#endif
#else
    DWORD w[16]={39,1,0x1688,0,0,0,38,0,1};
#endif
    auto address=[&](BYTE *p){memcpy(w+10,&p,sizeof p);};
#ifdef TEST_STRUCT192
    address(b+span-256); HR(xs_write_address_srv(guest.Get(),desc,w,from));
#else
    address(b+span-((width+15)&~15u)); HR(xs_write_address_srv(guest.Get(),desc,w,from));
#endif
    SLOT(guest.Get(),0xc0,DescriptorCopy)(guest.Get(),1,to,from,0);
    UINT64 serial=*(UINT64*)from;
#ifdef TEST_STRUCT192
    address(b+offset+16);CK(FAILED(xs_write_address_srv(guest.Get(),desc,w,from)));
    address(b+offset+128);CK(FAILED(xs_write_address_srv(guest.Get(),desc,w,from)));
    address(b+offset);w[6]=3;w[7]=64;CK(FAILED(xs_write_address_srv(guest.Get(),desc,w,from)));
    w[6]=ELEMENTS;w[7]=STRIDE;w[9]=0x800000;CK(FAILED(xs_write_address_srv(guest.Get(),desc,w,from)));
    w[9]=0x800100;CK(*(UINT64*)from==serial);
    // The retail write goes straight into a visible heap. The untagged ABI still permits 16-byte alignment.
    HR(xs_write_address_srv(guest.Get(),desc,w,to));
    w[9]=0;address(b+offset+16);HR(xs_write_address_srv(guest.Get(),desc,w,to+32));w[9]=0x800100;
    address(b+offset);
#endif
#ifdef STRIDE
    for (DWORD invalid:{0u,3u,2049u,2052u,0xfffffffcU}) {
        desc[4]=w[7]=invalid;CK(FAILED(xs_write_address_srv(guest.Get(),desc,w,from)));
    }
    desc[4]=width;w[7]=STRIDE;
    for(DWORD badCount:{0u,0x40000001u,~0u}) {w[6]=badCount;CK(FAILED(xs_write_address_srv(guest.Get(),desc,w,from)));}
    w[6]=ELEMENTS;
    // A wrapping DWORD product would pass this truncated width check.
    w[6]=0x40000001;w[7]=4;desc[4]=4;CK(FAILED(xs_write_address_srv(guest.Get(),desc,w,from)));
    w[6]=0x400001;desc[4]=w[6]*4;CK(FAILED(xs_write_address_srv(guest.Get(),desc,w,from)));
    w[7]=STRIDE;w[6]=ELEMENTS;desc[4]=width;
    for(UINT badWidth:{width-1,width+1}){desc[4]=badWidth;CK(FAILED(xs_write_address_srv(guest.Get(),desc,w,from)));}desc[4]=width;
    BYTE *guard=(BYTE*)VirtualAlloc(nullptr,8192,MEM_RESERVE,PAGE_NOACCESS);
    CK(guard && VirtualAlloc(guard,4096,MEM_COMMIT,PAGE_READWRITE));
    memcpy(guard+4096-63,w,63);CK(xs_write_address_srv(guest.Get(),desc,guard+4096-63,from)==E_POINTER);
    CK(VirtualFree(guard,0,MEM_RELEASE));
#endif
    for (UINT j:{0u,2u,3u,4u,5u,6u,7u,8u,9u,10u,11u,12u}) {
        desc[j]^=1; CK(FAILED(xs_write_address_srv(guest.Get(),desc,w,from))); desc[j]^=1;
    }
    for (UINT j:{0u,1u,2u,3u,4u,5u,6u,7u,8u,9u,12u,13u,14u,15u}) {
        w[j]^=1; CK(FAILED(xs_write_address_srv(guest.Get(),desc,w,from))); w[j]^=1;
    }
    for (auto p:{b+span-((width+15)&~15u)+16,b+offset+1,(BYTE*)&serial}) {
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
    ID3D12DescriptorHeap *bank; D3D12_GPU_DESCRIPTOR_HANDLE gh;
    *(UINT64*)to^=1; CK(FAILED(xs_resolve_view(dst.Get(),to,0x90,&bank,&gh)) && !bank && !gh.ptr); *(UINT64*)to^=1;
#ifdef STRIDE
    CK(xs_resolve_view(dst.Get(),to,0x90,&bank,&gh)==E_NOTIMPL && !bank && !gh.ptr);
#else
    HR(xs_resolve_view(dst.Get(),to,0x90,&bank,&gh));
#endif
#endif
    D3D12_DESCRIPTOR_RANGE range={D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,0,0,0};
    D3D12_ROOT_PARAMETER p[2]={};p[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;p[0].DescriptorTable={1,&range};
    p[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV;
    D3D12_ROOT_SIGNATURE_DESC rs={2,p,0,nullptr,D3D12_ROOT_SIGNATURE_FLAG_NONE};
    ComPtr<ID3DBlob> blob;HR(D3D12SerializeRootSignature(&rs,D3D_ROOT_SIGNATURE_VERSION_1,&blob,nullptr));
    ComPtr<ID3D12RootSignature> root;HR(d->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&root)));
#ifdef STRIDE
    FILE *f=fopen("struct1.dxil","rb");
#else
    FILE *f=fopen("raw38.dxil","rb");
#endif
    CK(f);BYTE dxil[8192];size_t n=fread(dxil,1,sizeof dxil,f);CK(n && feof(f));fclose(f);
    D3D12_COMPUTE_PIPELINE_STATE_DESC pd={};pd.pRootSignature=root.Get();pd.CS={dxil,n};
    ComPtr<ID3D12PipelineState> ps;HR(d->CreateComputePipelineState(&pd,IID_PPV_ARGS(&ps)));
    D3D12_HEAP_PROPERTIES hp={};hp.Type=D3D12_HEAP_TYPE_DEFAULT;hp.CreationNodeMask=hp.VisibleNodeMask=1;
    D3D12_RESOURCE_DESC rd={};rd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;rd.Width=results*4;rd.Height=rd.DepthOrArraySize=rd.MipLevels=rd.SampleDesc.Count=1;
    rd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;rd.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    ComPtr<ID3D12Resource> out,read;HR(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&out)));
    hp.Type=D3D12_HEAP_TYPE_READBACK;rd.Width=results*4;
#ifdef TEST_NATIVE_POOL
    rd.Width+=width+STRIDE+16; // Inspect all neighboring data for the first OOB element.
#endif
    rd.Flags=D3D12_RESOURCE_FLAG_NONE;HR(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&read)));
    D3D12_COMMAND_QUEUE_DESC qd={};
    qd.Type=D3D12_COMMAND_LIST_TYPE_COMPUTE;
    ComPtr<ID3D12CommandQueue> q;HR(d->CreateCommandQueue(&qd,IID_PPV_ARGS(&q)));ComPtr<ID3D12Fence> done,gate;
    HR(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&done)));HR(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&gate)));
    ComPtr<ID3D12CommandAllocator> a;HR(d->CreateCommandAllocator(qd.Type,IID_PPV_ARGS(&a)));
    ComPtr<ID3D12GraphicsCommandList> l;HR(d->CreateCommandList(0,qd.Type,a.Get(),ps.Get(),IID_PPV_ARGS(&l)));
    HANDLE ev=CreateEventW(nullptr,FALSE,FALSE,nullptr);CK(ev);
#ifdef STRIDE
    xs_texture_use *hold=nullptr;
    *(UINT64*)to^=1; CK(FAILED(xs_refresh_wc_texture(dst.Get(),to,l.Get(),&bank,&hold)) && !bank && !hold); *(UINT64*)to^=1;
    HR(xs_refresh_wc_texture(dst.Get(),to,l.Get(),&bank,&hold));gh=bank->GetGPUDescriptorHandleForHeapStart();
#endif
#ifdef TEST_NATIVE_POOL
    // Poison the entire output so an unexecuted or missing OOB store cannot pass as zero.
    memset(b,0xef,results*4);MemoryBarrier();D3D12_RESOURCE_BARRIER init={};init.Transition={out.Get(),0,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_DEST};
    l->ResourceBarrier(1,&init);l->CopyBufferRegion(out.Get(),0,proof.Get(),0,results*4);std::swap(init.Transition.StateBefore,init.Transition.StateAfter);l->ResourceBarrier(1,&init);
#endif
    l->SetComputeRootSignature(root.Get());l->SetDescriptorHeaps(1,&bank);l->SetComputeRootDescriptorTable(0,gh);
    l->SetComputeRootUnorderedAccessView(1,out->GetGPUVirtualAddress());l->Dispatch(((results-2)+63)/64,1,1);
    D3D12_RESOURCE_BARRIER br={};br.Transition={out.Get(),0,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE};
    l->ResourceBarrier(1,&br); l->CopyBufferRegion(read.Get(),0,out.Get(),0,results*4);
#ifdef TEST_NATIVE_POOL
    l->CopyBufferRegion(read.Get(),results*4,proof.Get(),offset-16,width+STRIDE+16);proof.Reset();
#endif
    HR(l->Close());
    HR(q->Wait(gate.Get(),1));ID3D12CommandList *batch[]={l.Get()};q->ExecuteCommandLists(1,batch);HR(q->Signal(done.Get(),1));
    HR(done->SetEventOnCompletion(1,ev));CK(WaitForSingleObject(ev,50)==WAIT_TIMEOUT);
#ifdef TEST_RAW32
    raw.replace();
#else
    DWORD *data=(DWORD*)(b+offset);for(UINT i=0;i<results;i++) data[i]=0x11002347+i*73;
#ifdef STRIDE
    // Poison both sides of the range; OOB reads must not see the adjacent element.
    data[-1]=0xdeadca11;MemoryBarrier();
#ifdef TEST_NATIVE_POOL
    for(UINT j=1;j<=4;j++)data[-(int)j]=0xdeadca10+j;
    MemoryBarrier();
#endif
#endif
    MemoryBarrier(); CK(VirtualFree(b,0,MEM_RELEASE));
    BYTE *reuse=b+(offset&~SIZE_T(65535)); CK(VirtualAlloc(reuse,65536,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE)==reuse);memset(reuse,0xad,65536);
    // A fresh lookup of the same VA must fail, leaving the retained destination intact.
    CK(FAILED(xs_write_address_srv(guest.Get(),desc,w,to)));
    // Rotate the device's one-entry WC cache. Only the copied descriptor now owns the old import.
    BYTE *second=(BYTE*)alloc(nullptr,span,0x20003000,0x283,0x2404); CK(second && second+offset!=reuse+(offset&65535));
    memset(second+offset,0x6b,width); address(second+offset);
    HR(xs_write_address_srv(guest.Get(),desc,w,to+32));
#endif
#ifdef STRIDE
    HR(xs_write_view(guest.Get(),0x90,nullptr,nullview,to)); dst.Reset();guest.Reset();
#ifdef TEST_NATIVE_POOL
    CK(WaitForSingleObject(ev,0)==WAIT_TIMEOUT && take(d.Get(),reuse+(offset&65535),width,nullptr,&proof,&off)==S_FALSE && !proof);
#endif
#endif
    HR(gate->Signal(1));CK(WaitForSingleObject(ev,10000)==WAIT_OBJECT_0);
    DWORD *map;HR(read->Map(0,nullptr,(void**)&map));for(UINT i=0;i<results;i++){
#ifdef STRIDE
        UINT expected=i<width/4?0x11002347+i*73:i<results-2?0:i==results-2?ELEMENTS:STRIDE;
#elif defined(TEST_RAW32)
        UINT expected=i==64 ? 128 : i<32 ? 0x11002347+i*73 : 0;
#else
        UINT expected=i==64 ? 152 : i<38 ? 0x11002347+i*73 : 0;
#endif
        if (map[i]!=expected) {fprintf(stderr,"FAIL index=%u got=%#lx expected=%#x\n",i,map[i],expected);ExitProcess(1);}
    }
#ifdef TEST_NATIVE_POOL
    for(UINT j=0;j<(width+STRIDE+16)/4;j++)CK(map[results+j]==(j<4?0xdeadca14-j:0x11002347+(j-4)*73));
    printf("PASS original 16-byte prefix, %u-byte payload and %u-byte poisoned suffix after exact VA reuse; cached descriptors released before signal\n",width,STRIDE);
#endif
    read->Unmap(0,nullptr);
#ifdef TEST_RAW32
    HR(d->GetDeviceRemovedReason());CloseHandle(ev);
    puts("PASS placed RAW32: 32 late-written words, 32 out-of-view zeros, size 128, original backing after VA reuse");
#else
    for(UINT i=0;i<65536;i++) CK(reuse[i]==0xad);
    CK(VirtualFree(reuse,0,MEM_RELEASE) && VirtualFree(second,0,MEM_RELEASE));CloseHandle(ev); HR(d->GetDeviceRemovedReason());
#ifdef STRIDE
    xs_drop_texture_use(hold);
    printf("PASS WC offset=%zx: %u original words, %u OOB zeros, count=%u stride=%u; late write, slot overwrite, cache rotation, exact VA reuse, heap release before fence\n",offset,width/4,STRIDE/4,ELEMENTS,STRIDE);
#else
    puts("PASS 38 original DWORDs, 26 OOB zeros including both padding DWORDs; size=152, source overwrite, tag, VA reuse and guard checks");
#endif
#endif
}
