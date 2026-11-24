#include <d3d12.h>
#include <cstdio>
#ifdef TEST_RADV_MARKER
#include "xmem/gpu_buffer.hpp"
#endif
#include <cstdlib>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
extern "C" HRESULT WINAPI D3D12XboxCreateDevice(void *,const void *,REFIID,void **);
extern "C" HRESULT xs_color_allocation_info(IUnknown *,UINT,UINT,const void *,D3D12_RESOURCE_ALLOCATION_INFO *);
extern "C" HRESULT xs_resolve_color_rtv(IUnknown *,SIZE_T,D3D12_CPU_DESCRIPTOR_HANDLE *,ID3D12Resource **);
extern "C" HRESULT xs_write_view(IUnknown *,UINT,IUnknown *,const void *,SIZE_T);
extern "C" HRESULT xs_resolve_view(IUnknown *,SIZE_T,UINT,ID3D12DescriptorHeap **,D3D12_GPU_DESCRIPTOR_HANDLE *);
extern "C" HRESULT xs_resolve_sampler(IUnknown *,SIZE_T,ID3D12DescriptorHeap **,D3D12_GPU_DESCRIPTOR_HANDLE *);
extern "C" HRESULT xs_write_sampler(IUnknown *,const void *,SIZE_T);
extern "C" const IID xs_pool_list_id;
extern "C" ID3D12GraphicsCommandList7 *xs_list_host(IUnknown *);
extern "C" HRESULT xs_bind_pool_heaps(IUnknown *,UINT,IUnknown *const *);
extern "C" HRESULT xs_descriptor_range(IUnknown *,SIZE_T,UINT,ID3D12DescriptorHeap **,D3D12_CPU_DESCRIPTOR_HANDLE *,D3D12_GPU_DESCRIPTOR_HANDLE *);
extern "C" HRESULT xs_direct_queue(ID3D12Device *,IUnknown **);
extern "C" HRESULT xs_shared_fence(ID3D12Device *,IUnknown **);
extern "C" HRESULT xs_queue_wait(IUnknown *,IUnknown *,UINT64);
extern "C" HRESULT xs_cpu_signal(IUnknown *,UINT64);
extern "C" HRESULT xs_fence_event(IUnknown *,UINT64,HANDLE);
#define SLOT(o,n,T) ((T)((*reinterpret_cast<void***>(o))[n/8]))
#define CK(x) do { if (!(x)) { std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); return 1; } } while(0)
#define HR(x) CK(SUCCEEDED(x))

static void *(WINAPI *allocateDefault)(SIZE_T,UINT64);
static void (WINAPI *freeDefault)(void *,UINT64);
static BYTE *arena;
static SIZE_T slice,used;
static UINT taken,given;
static BYTE foreign;
enum Fault { Normal, Foreign, Duplicate };
static Fault fault;
static void *WINAPI take(SIZE_T n,UINT64 attr)
{
    if (attr!=0x30808000) return allocateDefault(n,attr);
    if (n!=slice) std::abort();
    if (fault==Foreign) return &foreign;
    if (fault==Duplicate) return arena;
    if (!arena) {
        arena=(BYTE *)allocateDefault(64u<<20,attr);
        if (!arena) std::abort();
        memset(arena,0xa5,64u<<20);
    }
    SIZE_T old=used; used+=(n+65535)&~(SIZE_T)65535;
    if (used>(64u<<20) || ++taken>2) std::abort();
    return arena+old;
}
static void WINAPI give(void *ptr,UINT64 attr)
{
    if (attr!=0x30808000) { freeDefault(ptr,attr); return; }
    if (fault!=Normal) {
        if (ptr!=(fault==Foreign?&foreign:arena)) std::abort();
        fault=Normal; return;
    }
    if (++given>2 || ptr!=arena+(2-given)*slice) std::abort();
    if (ptr==arena) freeDefault(ptr,attr);
}
extern "C" { __declspec(dllexport) struct {decltype(&take) a;decltype(&give) f;} __XMemAllocationHooks__={take,give}; }

int main()
{
    HMODULE xm=LoadLibraryW(L"xmem.dll"); CK(xm);
    allocateDefault=(decltype(allocateDefault))(void *)GetProcAddress(xm,"XMemAllocDefault");
    freeDefault=(decltype(freeDefault))(void *)GetProcAddress(xm,"XMemFreeDefault");
    CK(allocateDefault && freeDefault);
    DWORD params[20]={}; params[0]=0x20004;params[2]=0x100000;params[10]=1;params[15]=0x20;
    const IID boot={0x5405c344,0xd457,0x444e,{0xb4,0xdd,0x23,0x66,0xe4,0x5a,0xee,0x39}};
    ComPtr<IUnknown> dev; HR(D3D12XboxCreateDevice(nullptr,params,boot,(void **)dev.GetAddressOf()));
    using Query=D3D12_RESOURCE_ALLOCATION_INFO *(WINAPI *)(D3D12_RESOURCE_ALLOCATION_INFO *,IUnknown *,UINT,UINT,const void *);
    auto query=(Query)((*reinterpret_cast<void***>(dev.Get()))[0xc8/8]);
    DWORD wire[]={3,0,0,0,0xf00,0,0x870,0x10001,0x1c,1,0,0,0x300005};
    // Only the 52-byte caller-written prefix is readable.
    SYSTEM_INFO sys; GetSystemInfo(&sys);
    auto mem=(BYTE *)VirtualAlloc(nullptr,2*sys.dwPageSize,MEM_RESERVE,PAGE_NOACCESS);
    CK(mem && VirtualAlloc(mem,sys.dwPageSize,MEM_COMMIT,PAGE_READWRITE)==mem);
    void *prefix=mem+sys.dwPageSize-sizeof wire;
    memcpy(prefix,wire,sizeof wire);
    struct { UINT64 before; D3D12_RESOURCE_ALLOCATION_INFO info; UINT64 after; } got={7,{},8};
    CK(query(&got.info,dev.Get(),1,1,prefix)==&got.info && got.before==7 && got.after==8);
    CK(VirtualFree(mem,0,MEM_RELEASE));
    auto denied=[&](UINT mask,UINT count,const void *ptr) {
        D3D12_RESOURCE_ALLOCATION_INFO bad={1,2};
        return FAILED(xs_color_allocation_info(dev.Get(),mask,count,ptr,&bad)) && !bad.SizeInBytes && !bad.Alignment;
    };
    CK(denied(0,1,wire) && denied(1,2,wire) && denied(1,1,nullptr));
    wire[12]^=0x100000; CK(denied(1,1,wire)); wire[12]^=0x100000;
    wire[2]=4096; CK(denied(1,1,wire)); wire[2]=0;

    ComPtr<ID3D12Device> d; HR(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&d)));
    D3D12_RESOURCE_DESC tex={}; tex.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    tex.Width=3840; tex.Height=2160; tex.DepthOrArraySize=tex.MipLevels=tex.SampleDesc.Count=1;
    tex.Format=DXGI_FORMAT_R8G8B8A8_UNORM; tex.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET|D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    slice=got.info.SizeInBytes;
    CK(slice==0x1fb0000 && got.info.Alignment==65536);
    D3D12_HEAP_PROPERTIES props={}; props.Type=D3D12_HEAP_TYPE_DEFAULT; props.CreationNodeMask=props.VisibleNodeMask=1;
    ComPtr<ID3D12Resource> rt,rb;
    using Make=HRESULT(WINAPI *)(IUnknown *,const void *,UINT,const void *,UINT,const void *,const void *,UINT,const void *,REFIID,void **);
    auto make=SLOT(dev.Get(),0x3f8,Make);
    D3D12_CLEAR_VALUE cv={}; cv.Format=tex.Format;
    auto create=[&](IUnknown **o,UINT flags,UINT state,UINT extension,REFIID iid){
        return make(dev.Get(),&props,flags,wire,state,&cv,nullptr,extension,nullptr,iid,(void **)o);
    };
    IUnknown *surface=dev.Get(),*qi;
    CK(FAILED(create(&surface,8,6,0,IID_ID3D12Resource)) && !surface);
    CK(FAILED(create(&surface,0,3,0,IID_ID3D12Resource)) && !surface);
    CK(FAILED(create(&surface,8,3,1,IID_ID3D12Resource)) && !surface);
    CK(create(&surface,8,3,0,IID_IUnknown)==E_NOINTERFACE && !surface);
    props.VisibleNodeMask=2; CK(FAILED(create(&surface,8,3,0,IID_ID3D12Resource)) && !surface); props.VisibleNodeMask=1;
    cv.Color[3]=1; CK(FAILED(create(&surface,8,3,0,IID_ID3D12Resource)) && !surface); cv.Color[3]=0;
    fault=Foreign; CK(FAILED(create(&surface,8,3,0,IID_ID3D12Resource)) && !surface && fault==Normal && !taken);
    HR(create(&surface,8,3,0,IID_ID3D12Resource));
    fault=Duplicate; CK(FAILED(create(&qi,8,3,0,IID_ID3D12Resource)) && !qi && fault==Normal && taken==1 && !given);
    HR(surface->QueryInterface(IID_ID3D12Resource,(void **)&qi)); CK(qi==surface && qi->Release()==1);
    HR(surface->QueryInterface(IID_IUnknown,(void **)&qi)); CK(qi==surface && qi->Release()==1);
    CK(surface->QueryInterface(IID_ID3D12Resource1,(void **)&qi)==E_NOINTERFACE && !qi);
    using Heap=HRESULT(WINAPI *)(IUnknown *,const void *,REFIID,void **);
    using Start=SIZE_T(WINAPI *)(IUnknown *);
    using UAV=void(WINAPI *)(IUnknown *,IUnknown *,IUnknown *,const void *,SIZE_T);
    DWORD heapDesc[]={0,0x130000,1,1},view[]={28,4,0,0}; ComPtr<IUnknown> heap;
    HR(SLOT(dev.Get(),0x70,Heap)(dev.Get(),heapDesc,IID_ID3D12DescriptorHeap,(void **)heap.GetAddressOf()));
    SIZE_T slot=SLOT(heap.Get(),0x48,Start)(heap.Get())+32;
    CK(FAILED(xs_write_view(dev.Get(),0x98,(IUnknown *)1,view,slot)));
    view[3]=1; CK(FAILED(xs_write_view(dev.Get(),0x98,surface,view,slot))); view[3]=0;
    SLOT(dev.Get(),0x98,UAV)(dev.Get(),surface,nullptr,view,slot);
    using SRV=void(WINAPI *)(IUnknown *,IUnknown *,const void *,SIZE_T);
    const SIZE_T sampleSlot=slot-32+999999ull*32;
    DWORD sw[]={28,4,0x1688,0,0,1,0,0};
    // The head of the device's live color list is deliberately a different texture.
    IUnknown *other; HR(create(&other,8,3,0,IID_ID3D12Resource));
    sw[5]=2; CK(FAILED(xs_write_view(dev.Get(),0x90,surface,sw,sampleSlot))); sw[5]=1;
    CK(FAILED(xs_write_view(dev.Get(),0x90,(IUnknown *)1,sw,sampleSlot)));
    SLOT(dev.Get(),0x90,SRV)(dev.Get(),surface,sw,sampleSlot);
    DWORD rtvHeapDesc[]={2,4096,0,1},rw[]={28,4,0,0,0,0};
    ComPtr<IUnknown> views; HR(SLOT(dev.Get(),0x70,Heap)(dev.Get(),rtvHeapDesc,IID_ID3D12DescriptorHeap,(void **)views.GetAddressOf()));
    SIZE_T dest=SLOT(views.Get(),0x48,Start)(views.Get());
    CK(dest && !SLOT(views.Get(),0x50,Start)(views.Get()));
    CK(FAILED(xs_write_view(dev.Get(),0xa0,surface,rw,slot))); // shader heap
    CK(FAILED(xs_write_view(dev.Get(),0x90,surface,sw,dest))); // RTV heap
    CK(FAILED(xs_write_view(dev.Get(),0xa0,surface,rw,dest+1)));
    CK(FAILED(xs_write_view(dev.Get(),0xa0,surface,rw,dest+4096*32)));
    CK(FAILED(xs_write_view(dev.Get(),0xa0,(IUnknown *)1,rw,dest)));
    rw[1]=6; CK(FAILED(xs_write_view(dev.Get(),0xa0,surface,rw,dest))); rw[1]=4;
    rw[2]=1; CK(FAILED(xs_write_view(dev.Get(),0xa0,surface,rw,dest))); rw[2]=0;
    rw[3]=1; CK(FAILED(xs_write_view(dev.Get(),0xa0,surface,rw,dest))); rw[3]=0;
    *(UINT64 *)(dest+16)=0xa0cafe;
    auto writer=SLOT(dev.Get(),0xa0,SRV);
    writer(dev.Get(),other,rw,dest); writer(dev.Get(),other,rw,dest+4095*32);
    writer(dev.Get(),surface,rw,dest); // overwrite; the last slot still refers to other
    CK(*(UINT64 *)(dest+16)==0xa0cafe);
    CK(taken==2 && !given && (64u<<20)-used==0xa0000);
    CK(other->Release()==0 && given==1 && surface->Release()==0 && given==2);
    D3D12_CPU_DESCRIPTOR_HANDLE rtv={},lastRTV={};
    *(UINT64 *)dest^=1; CK(FAILED(xs_resolve_color_rtv(views.Get(),dest,&rtv,rt.GetAddressOf())) && !rt && !rtv.ptr); *(UINT64 *)dest^=1;
    HR(xs_resolve_color_rtv(views.Get(),dest,&rtv,rt.GetAddressOf()));
    ComPtr<ID3D12Resource> otherRT; HR(xs_resolve_color_rtv(views.Get(),dest+4095*32,&lastRTV,otherRT.GetAddressOf()));
    CK(rt.Get()!=otherRT.Get() && rtv.ptr!=dest && lastRTV.ptr-rtv.ptr==4095ull*d->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV));
    otherRT.Reset();
    auto actual=rt->GetDesc(); CK(actual.Width==3840 && actual.Height==2160 && actual.Format==tex.Format && actual.Flags==tex.Flags);
    ID3D12DescriptorHeap *bank; D3D12_GPU_DESCRIPTOR_HANDLE gpu;
    HR(xs_resolve_view(heap.Get(),slot,0x98,&bank,&gpu));
    D3D12_DESCRIPTOR_HEAP_DESC ud={D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,1,D3D12_DESCRIPTOR_HEAP_FLAG_NONE,1};
    ComPtr<ID3D12DescriptorHeap> cpuHeap; HR(d->CreateDescriptorHeap(&ud,IID_PPV_ARGS(&cpuHeap)));
    auto cpu=cpuHeap->GetCPUDescriptorHandleForHeapStart();
    d->CreateUnorderedAccessView(rt.Get(),nullptr,nullptr,cpu);
    D3D12_RESOURCE_DESC buf={}; buf.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER; buf.Width=512;
    buf.Height=buf.DepthOrArraySize=buf.MipLevels=buf.SampleDesc.Count=1; buf.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    auto hp=props; hp.Type=D3D12_HEAP_TYPE_READBACK;
    HR(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&buf,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&rb)));
    D3D12_COMMAND_QUEUE_DESC qd={}; ComPtr<ID3D12CommandQueue> q; ComPtr<ID3D12CommandAllocator> a;
    ComPtr<ID3D12GraphicsCommandList> l; ComPtr<ID3D12Fence> f;
    HR(d->CreateCommandQueue(&qd,IID_PPV_ARGS(&q))); HR(d->CreateCommandAllocator(qd.Type,IID_PPV_ARGS(&a)));
    HR(d->CreateCommandList(0,qd.Type,a.Get(),nullptr,IID_PPV_ARGS(&l)));
    const FLOAT green[]={0,1,0,1},red[]={1,0,0,1};
    l->SetDescriptorHeaps(1,&bank); l->ClearUnorderedAccessViewFloat(gpu,cpu,rt.Get(),green,0,nullptr);
    D3D12_RESOURCE_BARRIER b={}; b.Transition={rt.Get(),0,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_RENDER_TARGET};
    l->ResourceBarrier(1,&b); D3D12_RECT corner={3839,2159,3840,2160};
    l->ClearRenderTargetView(rtv,red,1,&corner);
    b.Transition={rt.Get(),0,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE};
    l->ResourceBarrier(1,&b);
    D3D12_TEXTURE_COPY_LOCATION src={},dst={}; src.pResource=rt.Get(); src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.pResource=rb.Get(); dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint.Footprint={tex.Format,2,1,1,256};
    D3D12_BOX last={3838,2159,0,3840,2160,1}; l->CopyTextureRegion(&dst,0,0,0,&src,&last);
    HR(l->Close()); ID3D12CommandList *batch[]={l.Get()}; q->ExecuteCommandLists(1,batch);
    HR(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&f))); HR(q->Signal(f.Get(),1));
    HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr); CK(event);
    HR(f->SetEventOnCompletion(1,event)); CK(WaitForSingleObject(event,10000)==WAIT_OBJECT_0); CloseHandle(event);
    DWORD *pixel; HR(rb->Map(0,nullptr,(void **)&pixel)); CK(pixel[0]==0xff00ff00 && pixel[1]==0xff0000ff); rb->Unmap(0,nullptr);
    auto words=(const volatile UINT *)arena;
    CK(words[3840*2160-2]==0xff00ff00 && words[3840*2160-1]==0xff0000ff);
    CK(arena[used]==0xa5 && arena[(64u<<20)-1]==0xa5);
    // Transition before dropping the last direct reference. Only the SRV record
    // will retain this texture when the sampling dispatch is submitted.
    using Alloc=HRESULT(WINAPI*)(IUnknown*,UINT,REFIID,void**);
    using List=HRESULT(WINAPI*)(IUnknown*,const void*,IUnknown*,IUnknown*,REFIID,void**);
    using Bind=void(WINAPI*)(IUnknown*,UINT,IUnknown*const*);
    using Close=HRESULT(WINAPI*)(IUnknown*);
    using Reset=HRESULT(WINAPI*)(IUnknown*,IUnknown*,IUnknown*,UINT);
    ComPtr<IUnknown> pooled,alloc0,alloc1;UINT ld[]={0,6,1,0};auto alloc=SLOT(dev.Get(),0x48,Alloc);
    HR(alloc(dev.Get(),0,IID_ID3D12CommandAllocator,(void**)alloc0.GetAddressOf()));
    HR(alloc(dev.Get(),0,IID_ID3D12CommandAllocator,(void**)alloc1.GetAddressOf()));
    HR(SLOT(dev.Get(),0x248,List)(dev.Get(),ld,alloc0.Get(),nullptr,xs_pool_list_id,(void**)pooled.GetAddressOf()));
    DWORD nullUav[10]={42,1};
    SLOT(dev.Get(),0x98,UAV)(dev.Get(),nullptr,nullptr,nullUav,slot);

    *(UINT64 *)sampleSlot^=1;
    CK(FAILED(xs_resolve_view(heap.Get(),sampleSlot,0x90,&bank,&gpu)) && !bank && !gpu.ptr);
    *(UINT64 *)sampleSlot^=1;
    HR(xs_resolve_view(heap.Get(),sampleSlot,0x90,&bank,&gpu));
    D3D12_DESCRIPTOR_RANGE range={D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,0,0,0};
    D3D12_ROOT_PARAMETER rp[3]={}; rp[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rp[0].DescriptorTable={1,&range}; rp[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV;
    D3D12_DESCRIPTOR_RANGE samRange={D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER,2,0,0,0};
    rp[2].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; rp[2].DescriptorTable={1,&samRange};
    D3D12_ROOT_SIGNATURE_DESC rs={3,rp,0,nullptr,D3D12_ROOT_SIGNATURE_FLAG_NONE};
    ComPtr<IUnknown> cpuSam,visibleSam;
    UINT shape[4]={1,128,0,1};
    auto mkHeap=SLOT(dev.Get(),0x70,Heap);
    HR(mkHeap(dev.Get(),shape,IID_ID3D12DescriptorHeap,(void **)cpuSam.GetAddressOf()));
    shape[2]=1; HR(mkHeap(dev.Get(),shape,IID_ID3D12DescriptorHeap,(void **)visibleSam.GetAddressOf()));
    SIZE_T cpuSlot=SLOT(cpuSam.Get(),0x48,Start)(cpuSam.Get())+16*126;
    SIZE_T visibleSlot=SLOT(visibleSam.Get(),0x48,Start)(visibleSam.Get())+16*126;
    DWORD record[23]={0,1,1,2,2,0,1,1,1,0,0x41400000,0,0,1,0,0,1};
    using Writer=void(WINAPI *)(IUnknown *,const void *,SIZE_T);
    using Copy=void(WINAPI *)(IUnknown *,UINT,SIZE_T,SIZE_T,UINT);
    SLOT(dev.Get(),0x1e8,Writer)(dev.Get(),record,cpuSlot);
    SLOT(dev.Get(),0xc0,Copy)(dev.Get(),1,visibleSlot,cpuSlot,1);
    CK(*(UINT64 *)cpuSlot && !memcmp((void *)cpuSlot,(void *)visibleSlot,16));
    DWORD point[23]={0,2,2,1,1,0,1,1,1,0,0x41400000,0,0,1,0,0,1};
    SLOT(dev.Get(),0x1e8,Writer)(dev.Get(),point,cpuSlot+16);
    UINT64 pointTag[2]; memcpy(pointTag,(void *)(cpuSlot+16),16);
    for (auto &word:point) {
        word^=1;
        CK(FAILED(xs_write_sampler(dev.Get(),point,cpuSlot+16)));
        word^=1;
    }
    CK(FAILED(xs_write_sampler(dev.Get(),nullptr,cpuSlot+16)));
    CK(FAILED(xs_write_sampler(dev.Get(),point,cpuSlot+1)));
    CK(FAILED(xs_write_sampler(dev.Get(),point,cpuSlot+32)));
    CK(FAILED(xs_write_sampler(dev.Get(),point,visibleSlot)));
    CK(pointTag[0]!=*(UINT64 *)cpuSlot && !memcmp(pointTag,(void *)(cpuSlot+16),16));
    SLOT(dev.Get(),0xc0,Copy)(dev.Get(),1,visibleSlot+16,cpuSlot+16,1);
    CK(!memcmp(pointTag,(void *)(visibleSlot+16),16));
    ID3D12DescriptorHeap *samBank; D3D12_GPU_DESCRIPTOR_HANDLE samGPU;
    CK(FAILED(xs_resolve_sampler(cpuSam.Get(),cpuSlot,&samBank,&samGPU)));
    CK(FAILED(xs_resolve_sampler(visibleSam.Get(),visibleSlot-16,&samBank,&samGPU)));
    *(UINT64 *)visibleSlot^=1;
    CK(FAILED(xs_resolve_sampler(visibleSam.Get(),visibleSlot,&samBank,&samGPU)) && !samBank && !samGPU.ptr);
    *(UINT64 *)visibleSlot^=1;
    HR(xs_resolve_sampler(visibleSam.Get(),visibleSlot+16,&samBank,&samGPU));
    HR(xs_resolve_sampler(visibleSam.Get(),visibleSlot,&samBank,&samGPU));
    ComPtr<ID3DBlob> blob; ComPtr<ID3D12RootSignature> root;
    HR(D3D12SerializeRootSignature(&rs,D3D_ROOT_SIGNATURE_VERSION_1,&blob,nullptr));
    HR(d->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&root)));
    BYTE code[8192]; FILE *file=fopen("test-color-sample.dxil","rb"); CK(file);
    size_t size=fread(code,1,sizeof code,file); CK(size && size<sizeof code && feof(file)); fclose(file);
    D3D12_COMPUTE_PIPELINE_STATE_DESC pd={}; pd.pRootSignature=root.Get(); pd.CS={code,size};
    ComPtr<ID3D12PipelineState> ps; HR(d->CreateComputePipelineState(&pd,IID_PPV_ARGS(&ps)));
    hp.Type=D3D12_HEAP_TYPE_DEFAULT; buf.Width=24; buf.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    ComPtr<ID3D12Resource> samples;
    HR(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&buf,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&samples)));
    SIZE_T base=SLOT(heap.Get(),0x48,Start)(heap.Get());ID3D12DescriptorHeap *physical;D3D12_GPU_DESCRIPTOR_HANDLE address;
    HR(xs_descriptor_range(heap.Get(),base,1,&physical,nullptr,&address));CK(physical==bank);
    HR(xs_descriptor_range(heap.Get(),base+1000000ull*32,1,&physical,nullptr,&address));CK(bank!=physical);
    CK(FAILED(xs_descriptor_range(heap.Get(),base+999999ull*32,2,&physical,nullptr,&address)));
    ComPtr<IUnknown> foreign,foreignSam,small;HR(D3D12XboxCreateDevice(nullptr,params,boot,(void**)foreign.GetAddressOf()));
    HR(mkHeap(foreign.Get(),shape,IID_ID3D12DescriptorHeap,(void**)foreignSam.GetAddressOf()));UINT tiny[]={0,4,1,1};
    HR(mkHeap(dev.Get(),tiny,IID_ID3D12DescriptorHeap,(void**)small.GetAddressOf()));
    IUnknown *pair[]={heap.Get(),visibleSam.Get()};auto reject=[&](IUnknown *r,IUnknown *sam){IUnknown *bad[]={r,sam};return FAILED(xs_bind_pool_heaps(pooled.Get(),2,bad));};
    CK(reject(small.Get(),visibleSam.Get()) && reject(visibleSam.Get(),heap.Get()) && reject(heap.Get(),cpuSam.Get()) && reject(heap.Get(),foreignSam.Get()));
    CK(reject(heap.Get(),heap.Get()) && reject(heap.Get(),(IUnknown*)1) && FAILED(xs_bind_pool_heaps(pooled.Get(),1,pair)));
    cpuSam.Reset();SLOT(pooled.Get(),0x100,Bind)(pooled.Get(),2,pair);
    CK(reject(heap.Get(),foreignSam.Get()));
#ifdef TEST_RADV_MARKER
    auto reserve=(void *(WINAPI*)(void*,SIZE_T,DWORD,UINT64,DWORD))(void*)GetProcAddress(xm,"XMemVirtualAlloc"); CK(reserve);
    auto *ctrl=(UINT*)reserve(nullptr,65536,0x20401000,0x283,0x42404); CK(ctrl);
    ComPtr<ID3D12Device3> raw; HR(d.As(&raw)); ImportedBuffer proof; SIZE_T off=0;
    HR(proof.acquire(raw.Get(),ctrl+2,4,&off)); CK(off==8);
    for(UINT j=0;j<16384;j++)ctrl[j]=0x76100000+j;
    MemoryBarrier();auto original=(volatile UINT*)proof.cpu();CK(original[2]==0x76100002);
    using Marker=void(WINAPI*)(IUnknown*,void*,UINT,UINT64,UINT);
    scarlett_section_info lease; HRESULT alias=scarlett_query_section(0x53430120,ctrl+2,4,&lease);
    CK(FAILED(alias));
    printf("AMD guest prefix: original=%p offset=%zu originalWord=%08x cpuAliasHr=%08lx\n",ctrl,off,(UINT)original[2],(DWORD)alias);fflush(stdout);
    SLOT(pooled.Get(),0x238,Marker)(pooled.Get(),ctrl+2,0,0,0);
    CK(original[2]==0x76100002); // A synchronous CPU store is not a recorded command.
#endif

    l=xs_list_host(pooled.Get());
    b.Transition={rt.Get(),0,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE};
    l->ResourceBarrier(1,&b);
    rt.Reset(); views.Reset(); cpuHeap.Reset();
#ifdef TEST_RADV_MARKER
    // The GPU suffix must see the prefix value, including through an independent import.
    D3D12_RESOURCE_BARRIER aliasBarrier={};aliasBarrier.Type=D3D12_RESOURCE_BARRIER_TYPE_ALIASING;
    aliasBarrier.Aliasing.pResourceAfter=proof.resource();l->ResourceBarrier(1,&aliasBarrier);
    l->CopyBufferRegion(rb.Get(),48,proof.resource(),4,12);
#endif
    l->SetComputeRootSignature(root.Get()); l->SetPipelineState(ps.Get());
    l->SetComputeRootDescriptorTable(2,samGPU);
    l->SetComputeRootDescriptorTable(0,gpu); l->SetComputeRootUnorderedAccessView(1,samples->GetGPUVirtualAddress());
    l->Dispatch(1,1,1);
    b.Transition={samples.Get(),0,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE};
    l->ResourceBarrier(1,&b); l->CopyBufferRegion(rb.Get(),16,samples.Get(),0,24);
    HR(SLOT(pooled.Get(),0x48,Close)(pooled.Get()));CK(FAILED(xs_bind_pool_heaps(pooled.Get(),2,pair)));
    ComPtr<IUnknown> gq,gate,done;HR(xs_direct_queue(d.Get(),gq.GetAddressOf()));HR(xs_shared_fence(d.Get(),gate.GetAddressOf()));HR(xs_shared_fence(d.Get(),done.GetAddressOf()));
    using Submit=void(WINAPI*)(IUnknown*,UINT,IUnknown*const*,UINT64,UINT);using Signal=HRESULT(WINAPI*)(IUnknown*,IUnknown*,UINT64);
    HR(xs_queue_wait(gq.Get(),gate.Get(),1));IUnknown *lists[]={pooled.Get()};SLOT(gq.Get(),0x118,Submit)(gq.Get(),1,lists,0,0);
    HR(SLOT(gq.Get(),0x80,Signal)(gq.Get(),done.Get(),1));HANDLE sampled=CreateEventW(nullptr,FALSE,FALSE,nullptr); CK(sampled);
    HR(xs_fence_event(done.Get(),1,sampled));CK(WaitForSingleObject(sampled,50)==WAIT_TIMEOUT);
    // Reset on a different allocator before opening the gate. A new recording may bind a different sampler.
    HR(SLOT(pooled.Get(),0x458,Reset)(pooled.Get(),alloc1.Get(),nullptr,0));ComPtr<IUnknown> otherSam;
    HR(mkHeap(dev.Get(),shape,IID_ID3D12DescriptorHeap,(void**)otherSam.GetAddressOf()));pair[1]=otherSam.Get();
    SLOT(pooled.Get(),0x100,Bind)(pooled.Get(),2,pair);HR(SLOT(pooled.Get(),0x48,Close)(pooled.Get()));
    pooled.Reset();l.Reset();heap.Reset();visibleSam.Reset();
    MEMORY_BASIC_INFORMATION m;CK(VirtualQuery(arena,&m,sizeof m) && m.State==MEM_COMMIT);
#ifdef TEST_RADV_MARKER
    CK(original[2]==0x76100002 && VirtualFree(ctrl,65536,MEM_RELEASE));
    CK(VirtualAlloc(ctrl,65536,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE)==ctrl);
    for(UINT j=0;j<16384;j++)ctrl[j]=0x89500000+j;
    MemoryBarrier();CK(original[2]==0x76100002);
#endif
    HR(xs_cpu_signal(gate.Get(),1)); CK(WaitForSingleObject(sampled,10000)==WAIT_OBJECT_0); CloseHandle(sampled);
    HR(rb->Map(0,nullptr,(void **)&pixel));
#ifdef TEST_RADV_MARKER
    for(UINT j=0;j<16384;j++)CK(original[j]==(j==2?0:0x76100000+j) && ctrl[j]==0x89500000+j);
    CK(VirtualFree(ctrl,0,MEM_RELEASE));puts("PASS AMD public +238: prefilled original marker cleared only after gate, neighboring words and exact VA replacement intact after guest list reset/release");
    CK(pixel[12]==0x76100001 && pixel[13]==0 && pixel[14]==0x76100003);puts("PASS suffix GPU copy saw guarded marker zero in recording order");
#endif
    CK(pixel[4]==0xff00ff00 && pixel[5]==0xff0000ff && pixel[6]==0xff008080);
    CK(pixel[7]==0xff00ff00 && pixel[8]==0xff0000ff && pixel[9]==0xff00ff00); rb->Unmap(0,nullptr);
    l.Reset(); a.Reset(); heap.Reset();
    ULONGLONG limit=GetTickCount64()+5000;
    do { CK(VirtualQuery(arena,&m,sizeof m)); if(m.State==MEM_FREE)break;Sleep(1); } while(GetTickCount64()<limit);
    CK(m.State==MEM_FREE);
    printf("PASS edge SRV=%llu samplers=126,127 six checked native samples\n",(unsigned long long)(sampleSlot-base)/32);
    puts("PASS pooled +100 bank 0 and sampler: real dispatch through guest bind, rejected foreign/CPU/reversed/small heaps, tail distinct, in-flight reset/rebind/release retired original pair");
    FreeLibrary(xm);
    puts("PASS +1e8/+c0: green/red centers, linear seam ff008080 vs point seam ff00ff00 after CPU heap release; rejected record and destination controls");
    puts("PASS public +a0: type-2 heap, overwritten slot and independent final slot retained two distinct +3f8 textures; real RTV clear/readback after title Release");
    std::printf("PASS +c8 size=%llu alignment=%llu; +3f8 committed identity -> +98 UAV -> checked RTV; fenced last pixels ff00ff00 ff0000ff after wrapper release\n",
            (unsigned long long)got.info.SizeInBytes,(unsigned long long)got.info.Alignment);
    puts("PASS real hook arena: two reverse public frees at wrapper destruction; imported image writes visible in original bytes; final descriptor/host release unmapped the arena");
}
