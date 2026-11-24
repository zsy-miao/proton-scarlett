#include <d3d12.h>
#include <cstdint>
#include <wrl/client.h>
#include <cstdio>
#include <cstdlib>

#include <initializer_list>
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
constexpr UINT width=STRIDE,results=2*(width/4)+2;
#ifdef TEST_NATIVE_POOL
static_assert(STRIDE==2048);constexpr SIZE_T offset=0xe5c6200;
#else
constexpr SIZE_T offset=STRIDE==320?0x1c80:0x6200;
#endif
#else
constexpr UINT width=160,results=65;
constexpr SIZE_T offset=0x1b80;
#endif
int main(){
    auto alloc=(Alloc)(void*)GetProcAddress(LoadLibraryW(L"xmem.dll"),"XMemVirtualAlloc"); CK(alloc);
    BYTE *b=(BYTE*)alloc(nullptr,span,0x20003000,0x283,0x2404); CK(b);
    DWORD desc[]={1,0,0,0,width,0,1,0x10001,0,1,0,1,0};
    ComPtr<ID3D12Device3>d;HR(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&d)));
#ifdef TEST_NATIVE_POOL
    using Take=HRESULT(WINAPI*)(ID3D12Device*,const void*,SIZE_T,ID3D12Heap**,ID3D12Resource**,UINT*);
    auto take=(Take)(void*)GetProcAddress(GetModuleHandleW(L"xmem.dll"),"ScarlettAcquireNativePoolV1");UINT off=0;ComPtr<ID3D12Resource> proof;
    CK(take && take(d.Get(),b+offset,width,nullptr,&proof,&off)==S_OK && off==offset && off%width==512 && proof->GetDesc().Width==span);
    printf("retained GPU base=%llx + offset=%x, stride remainder=%u\n",proof->GetGPUVirtualAddress(),off,off%width);
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
    DWORD w[16]={0,1,0x1688,0,0,0,1,width};
#else
    DWORD w[16]={39,1,0x1688,0,0,0,38,0,1};
#endif
    auto address=[&](BYTE *p){memcpy(w+10,&p,sizeof p);};
    address(b+span-width); HR(xs_write_address_srv(guest.Get(),desc,w,from));
    SLOT(guest.Get(),0xc0,DescriptorCopy)(guest.Get(),1,to,from,0);
    UINT64 serial=*(UINT64*)from;
#ifdef STRIDE
    for (DWORD invalid:{0u,3u,2049u,2052u,0xfffffffcU}) {
        desc[4]=w[7]=invalid;CK(FAILED(xs_write_address_srv(guest.Get(),desc,w,from)));
    }
    desc[4]=w[7]=width;
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
    for (auto p:{b+span-width+16,b+offset+1,(BYTE*)&serial}) {
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
    rd.Width+=width+32;
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
    l->SetComputeRootSignature(root.Get());l->SetDescriptorHeaps(1,&bank);l->SetComputeRootDescriptorTable(0,gh);
    l->SetComputeRootUnorderedAccessView(1,out->GetGPUVirtualAddress());l->Dispatch((width/4+63)/64,1,1);
    D3D12_RESOURCE_BARRIER br={};br.Transition={out.Get(),0,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE};
    l->ResourceBarrier(1,&br); l->CopyBufferRegion(read.Get(),0,out.Get(),0,results*4);
#ifdef TEST_NATIVE_POOL
    // Inspect the original guarded window independently of the rebased SRV.
    l->CopyBufferRegion(read.Get(),results*4,proof.Get(),offset-16,width+32);proof.Reset();
#endif
    HR(l->Close());
    HR(q->Wait(gate.Get(),1));ID3D12CommandList *batch[]={l.Get()};q->ExecuteCommandLists(1,batch);HR(q->Signal(done.Get(),1));
    HR(done->SetEventOnCompletion(1,ev));CK(WaitForSingleObject(ev,50)==WAIT_TIMEOUT);
    DWORD *data=(DWORD*)(b+offset);for(UINT i=0;i<results;i++) data[i]=0x11002347+i*73;
#ifdef STRIDE
    // Poison both sides of the range; OOB reads must not see the adjacent element.
    data[-1]=0xdeadca11;MemoryBarrier();
#ifdef TEST_NATIVE_POOL
    for(UINT j=1;j<=4;j++)data[-(int)j]=0xdeadca10+j;
#endif
#endif
    MemoryBarrier(); CK(VirtualFree(b,0,MEM_RELEASE));
#ifdef TEST_NATIVE_POOL
    BYTE *reuse=b+(offset&~SIZE_T(65535));
#else
    BYTE *reuse=b;
#endif
    CK(VirtualAlloc(reuse,65536,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE)==reuse);memset(reuse,0xad,65536);
    // A fresh lookup of the same VA must fail, leaving the retained destination intact.
    CK(FAILED(xs_write_address_srv(guest.Get(),desc,w,to)));
    // Rotate the device's one-entry WC cache. Only the copied descriptor now owns the old import.
    BYTE *second=(BYTE*)alloc(nullptr,span,0x20003000,0x283,0x2404); CK(second && second!=b);
    memset(second+offset,0x6b,width); address(second+offset);
    HR(xs_write_address_srv(guest.Get(),desc,w,to+32));
#ifdef STRIDE
    HR(xs_write_view(guest.Get(),0x90,nullptr,nullview,to)); dst.Reset();guest.Reset();
#endif
    HR(gate->Signal(1));CK(WaitForSingleObject(ev,10000)==WAIT_OBJECT_0);
    DWORD *map;HR(read->Map(0,nullptr,(void**)&map));for(UINT i=0;i<results;i++){
#ifdef STRIDE
        UINT expected=i<width/4?0x11002347+i*73:i<width/2?0:i==width/2?1:width;
#else
        UINT expected=i==64 ? 152 : i<38 ? 0x11002347+i*73 : 0;
#endif
        if (map[i]!=expected) {fprintf(stderr,"FAIL index=%u got=%#lx expected=%#x\n",i,map[i],expected);ExitProcess(1);}
    }
#ifdef TEST_NATIVE_POOL
    for(UINT j=0;j<width/4+8;j++)CK(map[results+j]==(j<4?0xdeadca14-j:0x11002347+(j-4)*73));
    printf("PASS original 16-byte guards before/after offset %#zx; all 512 original words, late write at exact title address\n",offset);
#endif
    read->Unmap(0,nullptr);
    for(UINT i=0;i<65536;i++) CK(reuse[i]==0xad);
    CK(VirtualFree(reuse,0,MEM_RELEASE) && VirtualFree(second,0,MEM_RELEASE));CloseHandle(ev); HR(d->GetDeviceRemovedReason());
#ifdef STRIDE
    xs_drop_texture_use(hold);
    printf("PASS one structured element: %u original words, %u OOB zeros, count=1 stride=%u; late write, slot overwrite, cache rotation, exact VA reuse, heap release before fence\n",width/4,width/4,width);
#else
    puts("PASS 38 original DWORDs, 26 OOB zeros including both padding DWORDs; size=152, source overwrite, tag, VA reuse and guard checks");
#endif
}
