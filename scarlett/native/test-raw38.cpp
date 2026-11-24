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
constexpr SIZE_T span=0x89700000u, offset=0x1b80;
int main(){
    auto alloc=(Alloc)(void*)GetProcAddress(LoadLibraryW(L"xmem.dll"),"XMemVirtualAlloc"); CK(alloc);
    BYTE *b=(BYTE*)alloc(nullptr,span,0x20003000,0x283,0x2404); CK(b);
    DWORD desc[]={1,0,0,0,160,0,1,0x10001,0,1,0,1,0};
    ComPtr<ID3D12Device3>d;HR(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&d)));
    DWORD params[20]={}; params[0]=0x20004;params[2]=0x100000;params[10]=1;params[15]=0x20;
    const IID id={0x5405c344,0xd457,0x444e,{0xb4,0xdd,0x23,0x66,0xe4,0x5a,0xee,0x39}};
    ComPtr<IUnknown> guest,src,dst;
    HR(D3D12XboxCreateDevice(nullptr,params,id,(void**)guest.GetAddressOf()));
    auto make=SLOT(guest.Get(),0x70,Make);
    UINT hd[4]={0,0xc38,0,1}; HR(make(guest.Get(),hd,IID_ID3D12DescriptorHeap,(void**)src.GetAddressOf()));
    hd[1]=4;hd[2]=1;HR(make(guest.Get(),hd,IID_ID3D12DescriptorHeap,(void**)dst.GetAddressOf()));
    auto from=SLOT(src.Get(),0x48,Start)(src.Get()), to=SLOT(dst.Get(),0x48,Start)(dst.Get())+32;
    DWORD w[16]={39,1,0x1688,0,0,0,38,0,1};
    auto address=[&](BYTE *p){memcpy(w+10,&p,sizeof p);};
    address(b+span-160); HR(xs_write_address_srv(guest.Get(),desc,w,from));
    SLOT(guest.Get(),0xc0,DescriptorCopy)(guest.Get(),1,to,from,0);
    UINT64 serial=*(UINT64*)from;
    for (UINT j:{0u,2u,3u,4u,5u,6u,7u,8u,9u,10u,11u,12u}) {
        desc[j]^=1; CK(FAILED(xs_write_address_srv(guest.Get(),desc,w,from))); desc[j]^=1;
    }
    for (UINT j:{0u,1u,2u,3u,4u,5u,6u,7u,8u,9u,12u,13u,14u,15u}) {
        w[j]^=1; CK(FAILED(xs_write_address_srv(guest.Get(),desc,w,from))); w[j]^=1;
    }
    for (auto p:{b+span-144,b+offset+1,(BYTE*)&serial}) {
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
    HR(xs_resolve_view(dst.Get(),to,0x90,&bank,&gh));
    D3D12_DESCRIPTOR_RANGE range={D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,0,0,0};
    D3D12_ROOT_PARAMETER p[2]={};p[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;p[0].DescriptorTable={1,&range};
    p[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV;
    D3D12_ROOT_SIGNATURE_DESC rs={2,p,0,nullptr,D3D12_ROOT_SIGNATURE_FLAG_NONE};
    ComPtr<ID3DBlob> blob;HR(D3D12SerializeRootSignature(&rs,D3D_ROOT_SIGNATURE_VERSION_1,&blob,nullptr));
    ComPtr<ID3D12RootSignature> root;HR(d->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&root)));
    FILE *f=fopen("raw38.dxil","rb");CK(f);BYTE dxil[8192];size_t n=fread(dxil,1,sizeof dxil,f);CK(n && feof(f));fclose(f);
    D3D12_COMPUTE_PIPELINE_STATE_DESC pd={};pd.pRootSignature=root.Get();pd.CS={dxil,n};
    ComPtr<ID3D12PipelineState> ps;HR(d->CreateComputePipelineState(&pd,IID_PPV_ARGS(&ps)));
    D3D12_HEAP_PROPERTIES hp={};hp.Type=D3D12_HEAP_TYPE_DEFAULT;hp.CreationNodeMask=hp.VisibleNodeMask=1;
    D3D12_RESOURCE_DESC rd={};rd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;rd.Width=65*4;rd.Height=rd.DepthOrArraySize=rd.MipLevels=rd.SampleDesc.Count=1;
    rd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;rd.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    ComPtr<ID3D12Resource> out,read;HR(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&out)));
    hp.Type=D3D12_HEAP_TYPE_READBACK;rd.Width=65*4;rd.Flags=D3D12_RESOURCE_FLAG_NONE;HR(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&read)));
    D3D12_COMMAND_QUEUE_DESC qd={};
    qd.Type=D3D12_COMMAND_LIST_TYPE_COMPUTE;
    ComPtr<ID3D12CommandQueue> q;HR(d->CreateCommandQueue(&qd,IID_PPV_ARGS(&q)));ComPtr<ID3D12Fence> done,gate;
    HR(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&done)));HR(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&gate)));
    ComPtr<ID3D12CommandAllocator> a;HR(d->CreateCommandAllocator(qd.Type,IID_PPV_ARGS(&a)));
    ComPtr<ID3D12GraphicsCommandList> l;HR(d->CreateCommandList(0,qd.Type,a.Get(),ps.Get(),IID_PPV_ARGS(&l)));
    HANDLE ev=CreateEventW(nullptr,FALSE,FALSE,nullptr);CK(ev);
    l->SetComputeRootSignature(root.Get());l->SetDescriptorHeaps(1,&bank);l->SetComputeRootDescriptorTable(0,gh);
    l->SetComputeRootUnorderedAccessView(1,out->GetGPUVirtualAddress());l->Dispatch(1,1,1);
    D3D12_RESOURCE_BARRIER br={};br.Transition={out.Get(),0,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE};
    l->ResourceBarrier(1,&br); l->CopyBufferRegion(read.Get(),0,out.Get(),0,65*4);HR(l->Close());
    HR(q->Wait(gate.Get(),1));ID3D12CommandList *batch[]={l.Get()};q->ExecuteCommandLists(1,batch);HR(q->Signal(done.Get(),1));
    HR(done->SetEventOnCompletion(1,ev));CK(WaitForSingleObject(ev,50)==WAIT_TIMEOUT);
    DWORD *data=(DWORD*)(b+offset);for(UINT i=0;i<64;i++) data[i]=0x11002347+i*73;
    MemoryBarrier(); CK(VirtualFree(b,0,MEM_RELEASE));
    CK(VirtualAlloc(b,65536,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE)==b);memset(b,0xad,65536);
    // A fresh lookup of the same VA must fail, leaving the retained destination intact.
    CK(FAILED(xs_write_address_srv(guest.Get(),desc,w,to)));
    // Rotate the device's one-entry WC cache. Only the copied descriptor now owns the old import.
    BYTE *second=(BYTE*)alloc(nullptr,span,0x20003000,0x283,0x2404); CK(second && second!=b);
    memset(second+offset,0x6b,160); address(second+offset);
    HR(xs_write_address_srv(guest.Get(),desc,w,to+32));
    HR(gate->Signal(1));CK(WaitForSingleObject(ev,10000)==WAIT_OBJECT_0);
    DWORD *map;HR(read->Map(0,nullptr,(void**)&map));for(UINT i=0;i<65;i++){
        UINT expected=i==64 ? 152 : i<38 ? 0x11002347+i*73 : 0;
        if (map[i]!=expected) {fprintf(stderr,"FAIL index=%u got=%#lx expected=%#x\n",i,map[i],expected);ExitProcess(1);}
    }read->Unmap(0,nullptr);
    for(UINT i=0;i<65536;i++) CK(b[i]==0xad);
    CK(VirtualFree(b,0,MEM_RELEASE) && VirtualFree(second,0,MEM_RELEASE));CloseHandle(ev); HR(d->GetDeviceRemovedReason());
    puts("PASS 38 original DWORDs, 26 OOB zeros including both padding DWORDs; size=152, source overwrite, tag, VA reuse and guard checks");
}
