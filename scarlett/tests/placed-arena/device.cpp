#include <d3d12.h>
struct xs_placed_dispatch;
extern "C" {
HRESULT WINAPI D3D12XboxCreateDevice(void *,const void *,REFIID,void **);
HRESULT xs_write_address_srv(IUnknown *,const void *,const void *,SIZE_T);
HRESULT xs_record_placed_dispatch(IUnknown *,IUnknown *,SIZE_T,SIZE_T,const void *,SIZE_T,ID3D12GraphicsCommandList *,ID3D12Resource *,xs_placed_dispatch **);
void xs_drop_placed_dispatch(xs_placed_dispatch *);
__declspec(dllimport) BOOL WINAPI XMemAllocatePhysicalPages(ULONGLONG,PULONG_PTR,PULONG_PTR);
__declspec(dllimport) BOOL WINAPI XMemFreePhysicalPages(ULONG_PTR,PULONG_PTR);
__declspec(dllimport) void *WINAPI XMemMapPhysicalPages(void *,ULONG_PTR,PULONG_PTR);
__declspec(dllimport) void *WINAPI XMemVirtualAlloc(void *,SIZE_T,DWORD,ULONGLONG,DWORD);
}
#include <cstdio>
#include <cstdlib>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
#define HR(x) do { HRESULT h_=(x); if (FAILED(h_)) { std::fprintf(stderr,"%d: %s: %08lx\n",__LINE__,#x,(unsigned long)h_); std::exit(1); } } while(0)
static constexpr UINT A=27u<<20, B=74u<<20, E=174u<<20, reportBytes=E+24;

static ID3D12Device *device;
static ComPtr<ID3D12Resource> buffer(D3D12_HEAP_TYPE type, UINT bytes, D3D12_RESOURCE_STATES state, bool writable=false) {
    D3D12_HEAP_PROPERTIES hp={}; hp.Type=type; hp.CreationNodeMask=hp.VisibleNodeMask=1;
    D3D12_RESOURCE_DESC d={}; d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER; d.Width=bytes;
    d.Height=d.DepthOrArraySize=d.MipLevels=d.SampleDesc.Count=1; d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if(writable) d.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    ComPtr<ID3D12Resource> r; HR(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&d,state,nullptr,IID_PPV_ARGS(&r))); return r;
}
static void check(UINT at, UINT actual, UINT expect, const char *what) {
    if (actual!=expect) { std::fprintf(stderr,"%s byte %08x: %08x != %08x\n",what,at,actual,expect); std::exit(1); }
}
#define SLOT(o,n,T) ((T)((*reinterpret_cast<void***>(o))[n/8]))
using Make=HRESULT(WINAPI *)(IUnknown *,UINT64,const void *,UINT,const void *,REFIID,void **);
using Heap=HRESULT(WINAPI *)(IUnknown *,const void *,REFIID,void **);
using Start=SIZE_T(WINAPI *)(IUnknown *);
using Srv=void(WINAPI *)(IUnknown *,IUnknown *,const void *,SIZE_T);
using AddressSrv=void(WINAPI *)(IUnknown *,const void *,const void *,SIZE_T);
using Uav=void(WINAPI *)(IUnknown *,IUnknown *,IUnknown *,const void *,SIZE_T);
static UINT seed(UINT p) { return 0x10000000u | (p/4); }
int main() {
    ComPtr<ID3D12Device3> d; HR(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&d))); device=d.Get();

    auto rb=buffer(D3D12_HEAP_TYPE_READBACK,reportBytes,D3D12_RESOURCE_STATE_COPY_DEST);
    auto result=buffer(D3D12_HEAP_TYPE_DEFAULT,reportBytes,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,true);
    constexpr UINT pg=65536,n=2960;
    const UINT count[]={160,16,432,752,1600}, id[]={2352,2512,2528,0,752}, prot[]={0x1004,0x1404,0x8404,0x2404,0x2004};
    BYTE *at[5]; auto va=(BYTE *)XMemVirtualAlloc(nullptr,n*pg,0x42000,7,1);
    if (!va) return 1;
    for(unsigned i=0;i<5;i++) { at[i]=va; if(i<4&&!VirtualFree(va,count[i]*pg,0x8002)) return 1; va+=count[i]*pg; }
    for(unsigned i=0;i<5;i++) if(XMemVirtualAlloc(at[i],count[i]*pg,0x20406000,0x27,prot[i])!=at[i]) return 1;
    ULONG_PTR ids[2][n]; for(auto &run:ids) { ULONG_PTR size=n; if(!XMemAllocatePhysicalPages(0x280,&size,run)||size!=n)return 1; }
    auto map=[&](int gen){ for(unsigned i=2;i<5;i++) if(XMemMapPhysicalPages(at[i],count[i],gen<0?nullptr:ids[gen]+id[i])!=at[i]) std::exit(1); };
    auto guest=[&](UINT p)->volatile UINT& { return *(volatile UINT *)(at[p<A?2:p<B?3:4]+(p<A?p:p<B?p-A:p-B)); };
    map(0); constexpr UINT width=E;
    DWORD params[20]={}; params[0]=0x20004;params[2]=0x100000;params[10]=1;params[15]=0x20;
    const IID boot={0x5405c344,0xd457,0x444e,{0xb4,0xdd,0x23,0x66,0xe4,0x5a,0xee,0x39}};
    IUnknown *dev; HR(D3D12XboxCreateDevice(nullptr,params,boot,(void **)&dev));
    Make create=SLOT(dev,0x428,Make);
    D3D12_RESOURCE_DESC desc={}; desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER; desc.Width=width;
    desc.Height=desc.DepthOrArraySize=desc.MipLevels=desc.SampleDesc.Count=1;
    desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR; desc.Flags=(D3D12_RESOURCE_FLAGS)0x20004;
    IUnknown *obj,*other; HR(create(dev,(UINT64)at[2],&desc,~0u,nullptr,IID_ID3D12Resource,(void **)&obj));
    D3D12_DESCRIPTOR_HEAP_DESC hd={D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,2,D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,1};
    IUnknown *heap,*wrong; HR(SLOT(dev,0x70,Heap)(dev,&hd,IID_ID3D12DescriptorHeap,(void **)&heap));
    HR(SLOT(dev,0x70,Heap)(dev,&hd,IID_ID3D12DescriptorHeap,(void **)&wrong));
    SIZE_T srv=SLOT(heap,0x48,Start)(heap),uav=srv+32;
    DWORD sw[16]={39,1,0x1688,0,0,0,E/4,0,1,0xffff},uw[10]={39,1,0,0,E/4,0,0,0,1};
    UINT64 base=(UINT64)at[2]; memcpy(sw+10,&base,8);
    // First resource word at +4 and the last qword copied by 4ecc8c4 contain padding.
    DWORD wire[13]={1,1,0,0,E,0,1,0x10001,0,1,0,1,0};
    auto attempt=[&](){ return xs_write_address_srv(dev,wire,sw,srv); };
    *(DWORD *)srv=0x12; *(UINT64 *)(srv+16)=0x12345678;
    sw[6]--; if (SUCCEEDED(attempt())) return 1; sw[6]++;
    sw[12]=1; if (SUCCEEDED(attempt())) return 1; sw[12]=0;
    wire[12]=0x20004; if (SUCCEEDED(attempt())) return 1; wire[12]=0;
    sw[10]+=32; if (SUCCEEDED(attempt())) return 1; sw[10]-=32;
    IUnknown *dup; HR(create(dev,base,&desc,~0u,nullptr,IID_ID3D12Resource,(void **)&dup));
    if (SUCCEEDED(attempt()) || dup->Release()!=0 || *(DWORD *)srv!=0x12) return 1;
    SLOT(dev,0x230,AddressSrv)(dev,wire,sw,srv);
    if (*(UINT64 *)(srv+16)!=0x12345678) return 1;
    SLOT(dev,0x98,Uav)(dev,obj,nullptr,uw,uav);
    for(UINT p=0;p<E;p+=4) guest(p)=seed(p);
    MemoryBarrier(); map(-1);
    if (SUCCEEDED(attempt())) return 1;
    map(1);
    if (SUCCEEDED(attempt())) return 1; // old wrapper is still live, current run is different
    for(UINT p=0;p<E;p+=4) guest(p)=~seed(p)^0x53000000;
    MemoryBarrier();
    HR(create(dev,(UINT64)at[2],&desc,~0u,nullptr,IID_ID3D12Resource,(void **)&other));
    BYTE code[16384]; FILE *f=std::fopen("logical.dxil","rb"); if(!f) return 1;
    size_t length=std::fread(code,1,sizeof code,f); if(!length || !std::feof(f)) return 1; std::fclose(f);
    D3D12_COMMAND_QUEUE_DESC qd={}; qd.Type=D3D12_COMMAND_LIST_TYPE_COMPUTE;
    ComPtr<ID3D12CommandQueue> q; ComPtr<ID3D12CommandAllocator> a; ComPtr<ID3D12GraphicsCommandList> l;
    HR(d->CreateCommandQueue(&qd,IID_PPV_ARGS(&q))); HR(d->CreateCommandAllocator(qd.Type,IID_PPV_ARGS(&a)));
    HR(d->CreateCommandList(0,qd.Type,a.Get(),nullptr,IID_PPV_ARGS(&l)));
    auto transit=[&](ID3D12Resource *r,D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after) {
        D3D12_RESOURCE_BARRIER b={}; b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition={r,0,before,after}; l->ResourceBarrier(1,&b);
    };
    xs_placed_dispatch *hold=nullptr;
    auto record=[&](IUnknown *dv,IUnknown *hp,SIZE_T s,SIZE_T u) { return xs_record_placed_dispatch(dv,hp,s,u,code,length,l.Get(),result.Get(),&hold); };
    auto deny=[&](IUnknown *dv,IUnknown *hp,SIZE_T s,SIZE_T u) { hold=(xs_placed_dispatch *)1; if(SUCCEEDED(record(dv,hp,s,u))||hold) std::exit(1); };
    deny(dev,wrong,srv,uav); deny(other,heap,srv,uav); deny(dev,heap,uav,srv);
    SLOT(dev,0x98,Uav)(dev,other,nullptr,uw,uav); deny(dev,heap,srv,uav); other->Release();
    UINT64 stale=*(UINT64 *)uav; SLOT(dev,0x98,Uav)(dev,obj,nullptr,uw,uav);
    auto tag=(UINT64 *)uav; UINT64 current=tag[0]; tag[0]=stale; deny(dev,heap,srv,uav); tag[0]=current;
    auto ts=(UINT64 *)srv; ts[1]^=1; deny(dev,heap,srv,uav); ts[1]^=1;
    code[length-1]^=1; deny(dev,heap,srv,uav); code[length-1]^=1;
    obj->Release(); HR(record(dev,heap,srv,uav));
    DWORD nullSrv[10]={42,1,0x1688},nullUav[10]={42,1};
    SLOT(dev,0x90,Srv)(dev,nullptr,nullSrv,srv); SLOT(dev,0x98,Uav)(dev,nullptr,nullptr,nullUav,uav);
    heap->Release(); wrong->Release(); dev->Release();
    transit(result.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);
    l->CopyBufferRegion(rb.Get(),0,result.Get(),0,reportBytes); HR(l->Close()); result.Reset();
    ID3D12CommandList *batch[]={l.Get()}; q->ExecuteCommandLists(1,batch);
    ComPtr<ID3D12Fence> fence; HR(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence))); HR(q->Signal(fence.Get(),1));
    HANDLE ev=CreateEventW(nullptr,FALSE,FALSE,nullptr); if(!ev)return 1;
    HR(fence->SetEventOnCompletion(1,ev)); if(WaitForSingleObject(ev,30000)!=WAIT_OBJECT_0)return 1; CloseHandle(ev);
    HR(d->GetDeviceRemovedReason()); xs_drop_placed_dispatch(hold); UINT *m; HR(rb->Map(0,nullptr,(void**)&m));
    for(UINT p=0;p<E;p+=4) check(p,guest(p),~seed(p)^0x53000000,"replacement");
    map(-1); map(0);
    for(UINT p=0;p<E;p+=4) {
        UINT v=seed(p);
        check(p,m[p/4],v^(p<A?0xb0000000u:0xa0000000u),"shader-read/write-status");
        check(p,guest(p),p==B-4?0xcafe0001:p==B?0xcafe0002:p<A?v:~v,"physical");
    }
    const UINT expected[]={seed(A-4),~seed(A),~seed(B-4),~seed(B),2,0};
    for(UINT i=0;i<6;++i) check(i*4,m[E/4+i],expected[i],"boundary report");
    D3D12_RANGE none={}; rb->Unmap(0,&none);
    map(-1); for (auto &run:ids) if(!XMemFreePhysicalPages(n,run))return 1;
    for(auto v:at) if(!VirtualFree(v,0,MEM_RELEASE))return 1;
    std::printf("PASS +428 -> +230 address SRV / +98 UAV -> trusted shader: %u dwords read, %u stores rejected, %u accepted; fenced original and replacement generations checked. Wrong wire, ambiguous, unmapped and stale addresses rejected\n",E/4,A/4,(E-A)/4);
}
