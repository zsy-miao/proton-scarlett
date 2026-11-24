/* Included by test-allocation-info.cpp: reuse its actual factory, heaps and two hooked slices. */
#include "xmem/display_backing.h"
#ifdef TEST_TAIL_TIMESTAMP
#include "xmem/marked_section.h"
extern "C" HRESULT test_stamp_try(IUnknown *,UINT64,UINT64);
extern "C" void test_copy_ticks(IUnknown *,ID3D12Resource *,UINT64);
#ifdef TEST_TAGGED_BATCH
extern "C" HRESULT test_tagged(IUnknown *,UINT,IUnknown *const *,UINT64,UINT);
extern "C" HRESULT test_incomplete_batch(IUnknown *,IUnknown *,UINT64,BOOL);
extern "C" HRESULT xs_frame_submission_query(IUnknown *,UINT64,ID3D12CommandQueue *,BOOL);
extern "C" const IID xs_sync_list_id;
extern "C" ID3D12CommandQueue *xs_display_queue(IUnknown *);
#endif
#endif
extern "C" HRESULT test_primary_try(IUnknown *,UINT,const D3D12_BARRIER_GROUP *);
#ifdef TEST_PRIMARY_CLEAR
extern "C" HRESULT test_clear_try(IUnknown *,SIZE_T,const FLOAT *,UINT,const RECT *);
extern "C" ID3D12GraphicsCommandList7 *test_clear_recorder(IUnknown *);
#endif
struct PrimaryWitness : IUnknown {
    LONG refs=1;
    HANDLE gone=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    HRESULT WINAPI QueryInterface(REFIID id,void **out) override {
        *out=nullptr;if(id!=IID_IUnknown)return E_NOINTERFACE;*out=this;AddRef();return S_OK;
    }
    ULONG WINAPI AddRef() override {return InterlockedIncrement(&refs);}
    ULONG WINAPI Release() override {LONG n=InterlockedDecrement(&refs);if(n==1)SetEvent(gone);return n;}
};
#ifdef TEST_PRESENT_BRIDGE
#include "test-present-bridge.hpp"
#endif
static int primary_barrier(ID3D12Device *d,IUnknown *dev,IUnknown *surface,IUnknown *other,
        ComPtr<IUnknown> &heap,ComPtr<IUnknown> &views,SIZE_T slot,SIZE_T dest,HMODULE xm)
{
    using Alloc=HRESULT(WINAPI*)(IUnknown *,UINT,REFIID,void **);
    using List=HRESULT(WINAPI*)(IUnknown *,const void *,IUnknown *,IUnknown *,REFIID,void **);
    using Close=HRESULT(WINAPI*)(IUnknown *);
    using Reset=HRESULT(WINAPI*)(IUnknown *,IUnknown *,IUnknown *,UINT);
    using Barrier=void(WINAPI*)(IUnknown *,UINT,const D3D12_BARRIER_GROUP *);
    using Submit=void(WINAPI*)(IUnknown *,UINT,IUnknown *const *,UINT64,UINT);
    using Signal=HRESULT(WINAPI*)(IUnknown *,IUnknown *,UINT64);
    auto addAllocator=SLOT(dev,0x48,Alloc);ComPtr<IUnknown> a,b,list;
    HR(addAllocator(dev,0,IID_ID3D12CommandAllocator,(void**)a.GetAddressOf()));
    HR(addAllocator(dev,0,IID_ID3D12CommandAllocator,(void**)b.GetAddressOf()));
    UINT ld[]={0,6,1,0}; HR(SLOT(dev,0x248,List)(dev,ld,a.Get(),nullptr,xs_pool_list_id,(void**)list.GetAddressOf()));
#ifdef TEST_TAIL_TIMESTAMP
    constexpr UINT span=0x49400000,window=0x4190000,post=0x29a5ee15;
    constexpr UINT64 empty=0xadadadadadadadadull;
    UINT shape[]={1,128,1,1};ComPtr<IUnknown> sampler;
    using Heap=HRESULT(WINAPI*)(IUnknown *,const void *,REFIID,void **);
    HR(SLOT(dev,0x70,Heap)(dev,shape,IID_ID3D12DescriptorHeap,(void**)sampler.GetAddressOf()));
    IUnknown *pair[]={heap.Get(),sampler.Get()};HR(xs_bind_pool_heaps(list.Get(),2,pair));
    auto reserve=(void *(WINAPI*)(void*,SIZE_T,DWORD,UINT64,DWORD))(void*)GetProcAddress(xm,"XMemVirtualAlloc");CK(reserve);
    auto pool=(BYTE*)reserve(nullptr,span,0x20003000,0x183,0x22004);CK(pool);
#ifdef TEST_BATCH_HEAD
    constexpr UINT headOff=TEST_BATCH_HEAD;
#else
    constexpr UINT headOff=0x6218;
#endif
    auto head=pool+window+headOff;scarlett_section_info id;
#ifdef TEST_ORDERED_PRESENT
    ComPtr<IDXGIFactory4> factory;ComPtr<IDXGIAdapter1> adapter;DXGI_ADAPTER_DESC1 ad={};
    const LUID luid=d->GetAdapterLuid();
    HR(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));HR(factory->EnumAdapterByLuid(luid,IID_PPV_ARGS(&adapter)));
    HR(adapter->GetDesc1(&ad));
    CK(ad.AdapterLuid.LowPart==luid.LowPart && ad.AdapterLuid.HighPart==luid.HighPart && !(ad.Flags&DXGI_ADAPTER_FLAG_SOFTWARE));
    const bool wb=ad.VendorId==0x1002 && ad.DeviceId==0x164e;
    CK(wb || (ad.VendorId==0x10de && ad.DeviceId==0x2484));
#elif defined(TEST_WB_TAIL)
    constexpr bool wb=true;
#else
    constexpr bool wb=false;
#endif
    volatile UINT *ctrl=nullptr;BYTE *original;scarlett_section_info copy={};
    if(wb) {
        ctrl=(volatile UINT*)reserve(nullptr,65536,0x20401000,0x283,0x42404);CK(ctrl);
        HR(scarlett_query_section(0x5343000c,head,8,&id));
        HR(scarlett_query_section_handle(0x5343000b,nullptr,span,&copy,id.section));CK(copy.base);
        original=(BYTE*)copy.base+window;
    } else {
        auto make=(HRESULT(WINAPI*)(ID3D12Device*,SIZE_T,void**))(void*)GetProcAddress(GetModuleHandleW(L"d3d12core.dll"),"ScarlettCpuControlViewV1");
        CK(make);HR(make(d,65536,(void**)&ctrl));CK(ctrl);HR(scarlett_acquire_section(head,8,&id));
        original=(BYTE*)MapViewOfFile(id.section,FILE_MAP_READ|FILE_MAP_WRITE,0,window,65536);CK(original);
    }
#ifdef TEST_ORDERED_PRESENT
    printf("TAIL adapter=%04x:%04x path=%s\n",ad.VendorId,ad.DeviceId,wb?"wb-section":"cpu-control");
#endif
    CK(CloseHandle(id.section));memset(original,0xad,65536);
    auto ticks=(volatile UINT64*)(original+headOff-8);
    InterlockedExchange64((volatile LONG64*)ctrl,0);ctrl[2]=ctrl[3]=~0u;
    using Mark=void(WINAPI*)(IUnknown*,const volatile void*,UINT,UINT64,UINT);
    using Stamp=void(WINAPI*)(IUnknown*,const void*,UINT64,UINT64,UINT);
    SLOT(list.Get(),0x238,Mark)(list.Get(),ctrl+2,0,0,0);
#ifdef TEST_TAGGED_BATCH
    HR(xs_bind_pool_heaps(list.Get(),2,pair));
#endif
    SLOT(list.Get(),0x240,Stamp)(list.Get(),head,0,0,1);
    SLOT(list.Get(),0x238,Mark)(list.Get(),ctrl+3,post,0,0);
    SLOT(list.Get(),0x250,Mark)(list.Get(),ctrl,7,1,1);
#endif
#ifdef TEST_PRIMARY_CLEAR
    auto l=test_clear_recorder(list.Get()); // fixture records the producer; no escaped host recorder in the title
    FLOAT clear[4]={};
    CK(test_clear_try(list.Get(),dest,clear,0,nullptr)==E_NOTIMPL); // before +490
#else
    auto l=xs_list_host(list.Get()); CK(l);
#endif
    D3D12_CPU_DESCRIPTOR_HANDLE ignored; ComPtr<ID3D12Resource> texture;
    HR(xs_resolve_color_rtv(views.Get(),dest,&ignored,texture.GetAddressOf()));
    ID3D12DescriptorHeap *bank; D3D12_GPU_DESCRIPTOR_HANDLE uav;
    HR(xs_resolve_view(heap.Get(),slot,0x98,&bank,&uav)); ComPtr<ID3D12DescriptorHeap> keepBank=bank;
#ifndef TEST_PRIMARY_CLEAR
    D3D12_DESCRIPTOR_HEAP_DESC hd={D3D12_DESCRIPTOR_HEAP_TYPE_RTV,1,D3D12_DESCRIPTOR_HEAP_FLAG_NONE,1};
    ComPtr<ID3D12DescriptorHeap> rtvs; HR(d->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&rtvs)));
    auto rtv=rtvs->GetCPUDescriptorHandleForHeapStart(); d->CreateRenderTargetView(texture.Get(),nullptr,rtv);
#endif
    D3D12_DESCRIPTOR_RANGE range={D3D12_DESCRIPTOR_RANGE_TYPE_UAV,1,0,0,0};
    D3D12_ROOT_PARAMETER rp={};rp.ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;rp.DescriptorTable={1,&range};
    D3D12_ROOT_SIGNATURE_DESC rd={1,&rp,0,nullptr,D3D12_ROOT_SIGNATURE_FLAG_NONE}; ComPtr<ID3DBlob> blob;
    HR(D3D12SerializeRootSignature(&rd,D3D_ROOT_SIGNATURE_VERSION_1,&blob,nullptr));ComPtr<ID3D12RootSignature> root;
    HR(d->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&root)));
    BYTE code[8192];FILE *file=fopen("test-primary-barrier.dxil","rb");CK(file);
    size_t len=fread(code,1,sizeof code,file);CK(len && feof(file));fclose(file);
    D3D12_COMPUTE_PIPELINE_STATE_DESC pd={};pd.pRootSignature=root.Get();pd.CS={code,len};ComPtr<ID3D12PipelineState> ps;
    HR(d->CreateComputePipelineState(&pd,IID_PPV_ARGS(&ps)));
    l->SetPipelineState(ps.Get());l->SetComputeRootSignature(root.Get());l->SetDescriptorHeaps(1,&bank);
    l->SetComputeRootDescriptorTable(0,uav);l->Dispatch(240,135,1);
    D3D12_TEXTURE_BARRIER t={D3D12_BARRIER_SYNC_ALL_SHADING,D3D12_BARRIER_SYNC_RENDER_TARGET,
        D3D12_BARRIER_ACCESS_UNORDERED_ACCESS,D3D12_BARRIER_ACCESS_RENDER_TARGET,
        D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS,D3D12_BARRIER_LAYOUT_RENDER_TARGET,nullptr,{UINT_MAX,0,0,0,0,0},D3D12_TEXTURE_BARRIER_FLAG_NONE};
    D3D12_BARRIER_GROUP group={};group.Type=D3D12_BARRIER_TYPE_TEXTURE;group.NumBarriers=1;group.pTextureBarriers=&t;
    CK(test_primary_try(list.Get(),1,&group)==E_INVALIDARG); // no anonymous/global fallback
    t.pResource=texture.Get();CK(test_primary_try(list.Get(),1,&group)==E_INVALIDARG); // native pointer is not the guest key
    t.pResource=(ID3D12Resource*)surface;t.Subresources.IndexOrFirstMipLevel=0;
    CK(test_primary_try(list.Get(),1,&group)==E_NOTIMPL);t.Subresources.IndexOrFirstMipLevel=UINT_MAX;
    CK(test_primary_try(list.Get(),1,nullptr)==E_POINTER && test_primary_try(list.Get(),2,&group)==E_NOTIMPL);
    SLOT(list.Get(),0x490,Barrier)(list.Get(),1,&group);
    CK(test_primary_try(list.Get(),1,&group)==E_NOTIMPL);memset(&t,0,sizeof t);memset(&group,0,sizeof group);
#ifdef TEST_PRIMARY_CLEAR
    using Clear=void(WINAPI*)(IUnknown *,SIZE_T,const FLOAT *,UINT,const RECT *);
    RECT rect={}; CK(test_clear_try(list.Get(),dest,clear,1,&rect)==E_NOTIMPL && test_clear_try(list.Get(),dest,clear,0,&rect)==E_NOTIMPL);
    clear[3]=1;CK(test_clear_try(list.Get(),dest,clear,0,nullptr)==E_NOTIMPL);clear[3]=0;
    CK(test_clear_try(list.Get(),dest,nullptr,0,nullptr)==E_POINTER && FAILED(test_clear_try(list.Get(),dest+1,clear,0,nullptr)));
    CK(test_clear_try(list.Get(),dest+4095*32,clear,0,nullptr)==E_INVALIDARG); // valid RTV of the other primary
    *(UINT64*)dest^=1;CK(test_clear_try(list.Get(),dest,clear,0,nullptr)==E_INVALIDARG);*(UINT64*)dest^=1;
    SLOT(list.Get(),0x1a0,Clear)(list.Get(),dest,clear,0,nullptr);
    CK(test_clear_try(list.Get(),dest,clear,0,nullptr)==E_NOTIMPL);
#ifdef TEST_PRIMARY_RELEASE
    // A release that discards contents could pass a black-only readback.
    const FLOAT red[]={1,0,0,1};D3D12_RECT stripe={0,0,1,2160};
    l->ClearRenderTargetView(ignored,red,1,&stripe);
#ifdef TEST_PRESENT_BRIDGE
    D3D12_HEAP_PROPERTIES up={};up.Type=D3D12_HEAP_TYPE_UPLOAD;up.CreationNodeMask=up.VisibleNodeMask=1;
    D3D12_RESOURCE_DESC uploadDesc={};uploadDesc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;
    uploadDesc.Width=3840*2160*4;uploadDesc.Height=uploadDesc.DepthOrArraySize=uploadDesc.MipLevels=uploadDesc.SampleDesc.Count=1;
    uploadDesc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;ComPtr<ID3D12Resource> upload;UINT *pattern=nullptr;
    HR(d->CreateCommittedResource(&up,D3D12_HEAP_FLAG_NONE,&uploadDesc,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&upload)));
    HR(upload->Map(0,nullptr,(void**)&pattern));D3D12_RESOURCE_BARRIER toCopy={};
    toCopy.Transition={texture.Get(),0,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_DEST};l->ResourceBarrier(1,&toCopy);
    D3D12_TEXTURE_COPY_LOCATION from={},to={};from.pResource=upload.Get();from.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    from.PlacedFootprint={0,{DXGI_FORMAT_R8G8B8A8_UNORM,3840,2160,1,3840*4}};to.pResource=texture.Get();l->CopyTextureRegion(&to,0,0,0,&from,nullptr);
    toCopy.Transition.StateBefore=D3D12_RESOURCE_STATE_COPY_DEST;toCopy.Transition.StateAfter=D3D12_RESOURCE_STATE_RENDER_TARGET;l->ResourceBarrier(1,&toCopy);
#endif
#endif
    clear[0]=1; // caller storage is reusable immediately
    DWORD view[]={28,4,0,0,0,0};
#ifdef TEST_PRESENT_BRIDGE
    SIZE_T secondSlot=dest+4095*32;
#else
    SIZE_T secondSlot=dest;
#endif
    HR(xs_write_view(dev,0xa0,other,view,secondSlot)); // same slot now names the other image
    ComPtr<ID3D12Resource> sibling,read;HR(xs_resolve_color_rtv(views.Get(),secondSlot,&ignored,sibling.GetAddressOf()));CK(sibling.Get()!=texture.Get());
    const UINT width=3840,height=2160,imageBytes=width*height*4;
    D3D12_RESOURCE_DESC bd={};bd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;bd.Width=2*imageBytes;
    bd.Height=bd.DepthOrArraySize=bd.MipLevels=bd.SampleDesc.Count=1;bd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    D3D12_HEAP_PROPERTIES hp={};hp.Type=D3D12_HEAP_TYPE_READBACK;hp.CreationNodeMask=hp.VisibleNodeMask=1;
#ifdef TEST_TAIL_TIMESTAMP
    const UINT64 tickOffset=bd.Width;bd.Width+=16;
    CK(test_stamp_try(list.Get(),(UINT_PTR)head-8,0)==E_NOTIMPL); // clear alone is insufficient
#endif
    HR(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&bd,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&read)));
    auto copyList=l;
#ifdef TEST_PRIMARY_RELEASE
    t={D3D12_BARRIER_SYNC_RENDER_TARGET,D3D12_BARRIER_SYNC_NONE,D3D12_BARRIER_ACCESS_RENDER_TARGET,D3D12_BARRIER_ACCESS_NO_ACCESS,
       D3D12_BARRIER_LAYOUT_RENDER_TARGET,(D3D12_BARRIER_LAYOUT)0x1f,(ID3D12Resource*)other,{UINT_MAX,0,0,0,0,0},D3D12_TEXTURE_BARRIER_FLAG_NONE};
    group.Type=D3D12_BARRIER_TYPE_TEXTURE;group.NumBarriers=1;group.pTextureBarriers=&t;
    CK(test_primary_try(list.Get(),1,&group)==E_INVALIDARG);
    t.pResource=(ID3D12Resource*)surface;SLOT(list.Get(),0x490,Barrier)(list.Get(),1,&group);
    CK(test_primary_try(list.Get(),1,&group)==E_NOTIMPL && !xs_list_host(list.Get()));
#ifdef TEST_TAIL_TIMESTAMP
    CK(FAILED(test_stamp_try(list.Get(),(UINT_PTR)head,0)) && FAILED(test_stamp_try(list.Get(),(UINT_PTR)head-8,1)));
    SLOT(list.Get(),0x240,Stamp)(list.Get(),head-8,0,0,1);
    CK(FAILED(test_stamp_try(list.Get(),(UINT_PTR)head-8,0)));
    test_copy_ticks(list.Get(),read.Get(),tickOffset);
#endif
    ComPtr<ID3D12CommandQueue> reader;D3D12_COMMAND_QUEUE_DESC qd={};
    HR(d->CreateCommandQueue(&qd,IID_PPV_ARGS(&reader)));
    ComPtr<ID3D12CommandAllocator> allocator;HR(d->CreateCommandAllocator(qd.Type,IID_PPV_ARGS(&allocator)));
    ComPtr<ID3D12GraphicsCommandList7> acquire;HR(d->CreateCommandList(0,qd.Type,allocator.Get(),nullptr,IID_PPV_ARGS(&acquire)));
    copyList=acquire.Get();
#endif
    for (UINT j=0;j<2;j++) {
        auto source=j?sibling.Get():texture.Get();D3D12_RESOURCE_BARRIER br={};
        br.Transition={source,0,j?D3D12_RESOURCE_STATE_UNORDERED_ACCESS:D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE};
#ifdef TEST_PRIMARY_RELEASE
        if (!j) {
            D3D12_TEXTURE_BARRIER ab={D3D12_BARRIER_SYNC_NONE,D3D12_BARRIER_SYNC_COPY,D3D12_BARRIER_ACCESS_NO_ACCESS,D3D12_BARRIER_ACCESS_COPY_SOURCE,
                D3D12_BARRIER_LAYOUT_COMMON,D3D12_BARRIER_LAYOUT_COPY_SOURCE,source,{UINT_MAX,0,0,0,0,0},D3D12_TEXTURE_BARRIER_FLAG_NONE};
            D3D12_BARRIER_GROUP ag={};ag.Type=D3D12_BARRIER_TYPE_TEXTURE;ag.NumBarriers=1;ag.pTextureBarriers=&ab;copyList->Barrier(1,&ag);
        } else
#endif
        copyList->ResourceBarrier(1,&br);
        D3D12_TEXTURE_COPY_LOCATION src={},dst={};src.pResource=source;dst.pResource=read.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dst.PlacedFootprint.Offset=(UINT64)j*imageBytes;dst.PlacedFootprint.Footprint={DXGI_FORMAT_R8G8B8A8_UNORM,width,height,1,width*4};
        copyList->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
    }
#else
    const FLOAT red[]={1,0,0,1};D3D12_RECT right={1920,0,3840,2160}; l->ClearRenderTargetView(rtv,red,1,&right);
#endif
    HR(SLOT(list.Get(),0x48,Close)(list.Get()));
#ifdef TEST_PRESENT_BRIDGE
    ComPtr<IUnknown> second;
#ifdef TEST_ORDERED_PRESENT
    auto record_recycled=[&]() -> int {
    IUnknown *key=surface;auto image=texture.Get();SIZE_T targetSlot=dest;
    HR(xs_write_view(dev,0xa0,other,view,targetSlot));
    HR(xs_write_view(dev,0xa0,key,view,targetSlot));
#else
    IUnknown *key=other;auto image=sibling.Get();SIZE_T targetSlot=secondSlot;
#endif
    HR(SLOT(dev,0x248,List)(dev,ld,b.Get(),nullptr,xs_pool_list_id,(void**)second.GetAddressOf()));
    IUnknown *s2=second.Get();HR(xs_bind_pool_heaps(s2,2,pair));SLOT(s2,0x238,Mark)(s2,ctrl+2,0,0,0);
    HR(xs_bind_pool_heaps(s2,2,pair));SLOT(s2,0x240,Stamp)(s2,head,0,0,1);SLOT(s2,0x238,Mark)(s2,ctrl+3,post,0,0);SLOT(s2,0x250,Mark)(s2,ctrl,7,1,1);
    t={D3D12_BARRIER_SYNC_ALL_SHADING,D3D12_BARRIER_SYNC_RENDER_TARGET,D3D12_BARRIER_ACCESS_UNORDERED_ACCESS,D3D12_BARRIER_ACCESS_RENDER_TARGET,
        D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS,D3D12_BARRIER_LAYOUT_RENDER_TARGET,(ID3D12Resource*)key,{UINT_MAX,0,0,0,0,0},D3D12_TEXTURE_BARRIER_FLAG_NONE};
    group.pTextureBarriers=&t;
    auto tail=test_clear_recorder(s2);
#ifdef TEST_ORDERED_PRESENT
    // The private WSI copy restored A to COMMON. Reacquire on the original producer.
    D3D12_TEXTURE_BARRIER restore={D3D12_BARRIER_SYNC_NONE,D3D12_BARRIER_SYNC_ALL_SHADING,
        D3D12_BARRIER_ACCESS_NO_ACCESS,D3D12_BARRIER_ACCESS_UNORDERED_ACCESS,D3D12_BARRIER_LAYOUT_COMMON,
        D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS,image,{UINT_MAX,0,0,0,0,0},D3D12_TEXTURE_BARRIER_FLAG_NONE};
    auto restoreGroup=group;restoreGroup.pTextureBarriers=&restore;tail->Barrier(1,&restoreGroup);
#endif
    SLOT(s2,0x490,Barrier)(s2,1,&group);FLOAT zero[4]={};SLOT(s2,0x1a0,Clear)(s2,targetSlot,zero,0,nullptr);
    toCopy.Transition={image,0,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_DEST};
    tail->ResourceBarrier(1,&toCopy);to.pResource=image;tail->CopyTextureRegion(&to,0,0,0,&from,nullptr);
    toCopy.Transition.StateBefore=D3D12_RESOURCE_STATE_COPY_DEST;toCopy.Transition.StateAfter=D3D12_RESOURCE_STATE_RENDER_TARGET;tail->ResourceBarrier(1,&toCopy);
    t={D3D12_BARRIER_SYNC_RENDER_TARGET,D3D12_BARRIER_SYNC_NONE,D3D12_BARRIER_ACCESS_RENDER_TARGET,D3D12_BARRIER_ACCESS_NO_ACCESS,
       D3D12_BARRIER_LAYOUT_RENDER_TARGET,(D3D12_BARRIER_LAYOUT)0x1f,(ID3D12Resource*)key,{UINT_MAX,0,0,0,0,0},D3D12_TEXTURE_BARRIER_FLAG_NONE};
    SLOT(s2,0x490,Barrier)(s2,1,&group);SLOT(s2,0x240,Stamp)(s2,head-8,0,0,1);HR(SLOT(s2,0x48,Close)(s2));
#ifdef TEST_ORDERED_PRESENT
    return 0; };
#endif
    IUnknown *keys[]={surface,other},*batches[]={list.Get(),second.Get()};ID3D12Resource *sources[]={texture.Get(),sibling.Get()};
    int checked=bridge_fixture(d,dev,keys,batches,
#ifdef TEST_PAIR_SPLIT
            pair,head,
#endif
#ifdef TEST_ORDERED_PRESENT
            upload.Get(),views.Get(),record_recycled,second,
#endif
            sources,dest,pattern,ctrl,ticks);
    upload->Unmap(0,nullptr);other->Release();surface->Release();return checked;
#endif
    PrimaryWitness witness;CK(witness.gone);const GUID tag={0x490,0,0,{}};
    HR(texture->SetPrivateDataInterface(tag,&witness));CK(witness.refs==2);
    auto pin=(decltype(&ScarlettRetainDisplayBackingV1))(void*)GetProcAddress(xm,"ScarlettRetainDisplayBackingV1");
    auto unpin=(decltype(&ScarlettReleaseDisplayBackingV1))(void*)GetProcAddress(xm,"ScarlettReleaseDisplayBackingV1");
    XsDisplayBacking backing;CK(pin && unpin); HR(pin(arena,slice,&backing));CK(backing.base==arena);
    ComPtr<IUnknown> q,gate,done;
#ifdef TEST_TAGGED_BATCH
    using Queue=HRESULT(WINAPI*)(IUnknown*,const D3D12_COMMAND_QUEUE_DESC*,REFIID,void**);
    auto createQueue=SLOT(dev,0x40,Queue);qd.NodeMask=1;ComPtr<IUnknown> wrongQueue;
    HR(createQueue(dev,&qd,IID_ID3D12CommandQueue,(void**)q.GetAddressOf()));
    HR(createQueue(dev,&qd,IID_ID3D12CommandQueue,(void**)wrongQueue.GetAddressOf()));
#else
    HR(xs_direct_queue(d,q.GetAddressOf()));
#endif
    HR(xs_shared_fence(d,gate.GetAddressOf()));HR(xs_shared_fence(d,done.GetAddressOf()));
#ifdef TEST_PRIMARY_RELEASE
    HR(acquire->Close());
    ComPtr<ID3D12Fence> published,consumed;HR(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&published)));
    HR(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&consumed)));
    ComPtr<ID3D12Resource> readOwner=texture;
#endif
#ifdef TEST_TAIL_TIMESTAMP
    // Replay requires restoring the primary layout, but not resetting the guest list.
    auto queue=test_host_queue(q.Get());
    ComPtr<ID3D12GraphicsCommandList7> restore;HR(d->CreateCommandList(1,qd.Type,allocator.Get(),nullptr,IID_PPV_ARGS(&restore)));
    t={D3D12_BARRIER_SYNC_NONE,D3D12_BARRIER_SYNC_COMPUTE_SHADING,D3D12_BARRIER_ACCESS_NO_ACCESS,D3D12_BARRIER_ACCESS_UNORDERED_ACCESS,
        D3D12_BARRIER_LAYOUT_COMMON,D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS,texture.Get(),{UINT_MAX,0,0,0,0,0},D3D12_TEXTURE_BARRIER_FLAG_NONE};
    group.pTextureBarriers=&t;restore->Barrier(1,&group);HR(restore->Close());
    IUnknown *first[]={list.Get()};SLOT(q.Get(),0x118,Submit)(q.Get(),1,first,0,0);
    ID3D12CommandList *back[]={restore.Get()};queue->ExecuteCommandLists(1,back);
    ComPtr<ID3D12Fence> firstDone;HR(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&firstDone)));HR(queue->Signal(firstDone.Get(),1));
    auto headOnly=[&] {
        ULONGLONG until=GetTickCount64()+5000;
        while(ctrl[3]!=post && GetTickCount64()<until) Sleep(1);
        return ctrl[2]==0 && ctrl[3]==post && ticks[1]!=empty && ticks[0]==empty;
    };
    CK(headOnly() && !firstDone->GetCompletedValue());InterlockedExchange64((volatile LONG64*)ctrl,1);
    HANDLE firstEvent=CreateEventW(nullptr,FALSE,FALSE,nullptr);CK(firstEvent);HR(firstDone->SetEventOnCompletion(1,firstEvent));
    CK(WaitForSingleObject(firstEvent,10000)==WAIT_OBJECT_0);CloseHandle(firstEvent);
    UINT64 firstTail=ticks[0];CK(ticks[1]<firstTail && firstTail!=empty);
    ticks[0]=ticks[1]=empty;ctrl[2]=ctrl[3]=~0u;InterlockedExchange64((volatile LONG64*)ctrl,0);
    const BYTE before0=arena[0],beforeLast=arena[3839*4];
#else
    const BYTE before0=0xa5,beforeLast=0xa5;
#endif
#ifdef TEST_PRIMARY_RELEASE
    HR(reader->Wait(published.Get(),1));ID3D12CommandList *reads[]={acquire.Get()};reader->ExecuteCommandLists(1,reads);HR(reader->Signal(consumed.Get(),1));
#endif
    HR(xs_queue_wait(q.Get(),gate.Get(),1));IUnknown *batch[]={list.Get()};
#ifdef TEST_TAGGED_BATCH
    using Interval=HRESULT(WINAPI*)(IUnknown*,IDXGIOutput*,UINT,UINT,UINT);
    using Schedule=HRESULT(WINAPI*)(IUnknown*,UINT,UINT,const void*,UINT);
    using FrameWait=HRESULT(WINAPI*)(IUnknown*,UINT,DWORD,const void*,UINT,UINT64*);
    auto configure=[&](IUnknown *owner) {
        return SLOT(owner,0x270,Interval)(owner,nullptr,16667,1,0)==S_OK &&
            SLOT(owner,0x278,Schedule)(owner,0,0,nullptr,0)==S_OK && SLOT(owner,0x278,Schedule)(owner,2,0,nullptr,0)==S_OK;
    };
    CK(configure(dev));UINT64 token=0,stale=0;
    auto frameWait=SLOT(dev,0x280,FrameWait);HR(frameWait(dev,0,1000,nullptr,0,&stale));HR(frameWait(dev,0,1000,nullptr,0,&token));
    ComPtr<IUnknown> sa,sync;HR(addAllocator(dev,0,IID_ID3D12CommandAllocator,(void**)sa.GetAddressOf()));
    UINT sd[]={0,2,1,0};HR(SLOT(dev,0x248,List)(dev,sd,sa.Get(),nullptr,xs_sync_list_id,(void**)sync.GetAddressOf()));
    HR(SLOT(sync.Get(),0x48,Close)(sync.Get()));IUnknown *syncs[]={sync.Get()};
    auto status=[&](BOOL batchEnd){return xs_frame_submission_query(dev,token,queue,batchEnd);};
    CK(test_tagged(q.Get(),1,batch,token,3)==DXGI_ERROR_INVALID_CALL);
    DWORD params[20]={};params[0]=0x20004;params[2]=0x100000;params[10]=1;params[15]=0x20;
    const IID boot={0x5405c344,0xd457,0x444e,{0xb4,0xdd,0x23,0x66,0xe4,0x5a,0xee,0x39}};
    ComPtr<IUnknown> foreign,fq;
    HR(D3D12XboxCreateDevice(nullptr,params,boot,(void**)foreign.GetAddressOf()));
    HR(createQueue(foreign.Get(),&qd,IID_ID3D12CommandQueue,(void**)fq.GetAddressOf()));
    CK(configure(foreign.Get())); UINT64 otherToken=0;HR(frameWait(foreign.Get(),0,1000,nullptr,0,&otherToken));
    CK(test_tagged(q.Get(),1,syncs,otherToken,0)==DXGI_ERROR_INVALID_CALL &&
        test_tagged(fq.Get(),1,syncs,otherToken,0)==E_INVALIDARG && test_tagged(fq.Get(),1,batch,otherToken,3)==E_INVALIDARG);
    CK(test_tagged(q.Get(),1,syncs,stale,0)==DXGI_ERROR_INVALID_CALL);
    SLOT(q.Get(),0x118,Submit)(q.Get(),1,syncs,token,0);
    CK(status(FALSE)==E_PENDING && status(TRUE)==S_FALSE && test_tagged(q.Get(),1,syncs,token,0)==DXGI_ERROR_INVALID_CALL);
    CK(test_tagged(wrongQueue.Get(),1,batch,token,3)==DXGI_ERROR_INVALID_CALL &&
        test_tagged(q.Get(),1,syncs,token,3)==E_INVALIDARG && test_tagged(q.Get(),2,batch,token,3)==E_NOTIMPL &&
        test_tagged(q.Get(),1,batch,token,1)==E_NOTIMPL && test_tagged(q.Get(),1,batch,stale,3)==DXGI_ERROR_INVALID_CALL &&
        test_incomplete_batch(q.Get(),list.Get(),token,TRUE)==E_INVALIDARG && test_incomplete_batch(q.Get(),list.Get(),token,FALSE)==E_INVALIDARG);
    CK(xs_display_queue(wrongQueue.Get()) && test_tagged(wrongQueue.Get(),1,syncs,token,0)==E_ACCESSDENIED);
    SLOT(q.Get(),0x118,Submit)(q.Get(),1,batch,token,3);
    using AllocReset=HRESULT(WINAPI*)(IUnknown*);
    CK(status(TRUE)==E_PENDING && test_tagged(q.Get(),1,batch,token,3)==DXGI_ERROR_INVALID_CALL &&
        SLOT(a.Get(),0x40,AllocReset)(a.Get())==DXGI_ERROR_WAS_STILL_DRAWING &&
        SLOT(sa.Get(),0x40,AllocReset)(sa.Get())==DXGI_ERROR_WAS_STILL_DRAWING);
    sync.Reset();sa.Reset();
#else
    SLOT(q.Get(),0x118,Submit)(q.Get(),1,batch,0,0);
#endif
    HR(SLOT(q.Get(),0x80,Signal)(q.Get(),done.Get(),1));HANDLE retired=CreateEventW(nullptr,FALSE,FALSE,nullptr);CK(retired);
    HR(xs_fence_event(done.Get(),1,retired));HR(SLOT(list.Get(),0x458,Reset)(list.Get(),b.Get(),nullptr,0));list.Reset();a.Reset();
    CK(other->Release()==0 && surface->Release()==0 && given==2);views.Reset();heap.Reset();texture.Reset();
    CK(WaitForSingleObject(retired,50)==WAIT_TIMEOUT && WaitForSingleObject(witness.gone,0)==WAIT_TIMEOUT);
#ifdef TEST_PRIMARY_RELEASE
    HR(test_host_queue(q.Get())->Signal(published.Get(),1));CK(published->GetCompletedValue()==0 && consumed->GetCompletedValue()==0);
#endif
#ifdef TEST_TAIL_TIMESTAMP
    CK(VirtualFree(pool,0,MEM_RELEASE));
    auto reuse=pool+window;CK(VirtualAlloc(reuse,65536,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE)==reuse);
    memset(reuse,0x49,65536);CK(ticks[0]==empty && ticks[1]==empty);
    UINT64 low,mid,high,cpu;HR(queue->GetClockCalibration(&low,&cpu));
#endif
    CK(arena[0]==before0 && arena[3839*4]==beforeLast);HR(xs_cpu_signal(gate.Get(),1));
#ifdef TEST_TAIL_TIMESTAMP
    CK(headOnly() && WaitForSingleObject(retired,100)==WAIT_TIMEOUT && ticks[0]==empty);
#ifdef TEST_TAGGED_BATCH
    CK(status(FALSE)==S_OK && status(TRUE)==E_PENDING);
#endif
    HR(queue->GetClockCalibration(&mid,&cpu));InterlockedExchange64((volatile LONG64*)ctrl,1);
#endif
#ifdef TEST_PRIMARY_RELEASE
    HANDLE ready=CreateEventW(nullptr,FALSE,FALSE,nullptr);CK(ready);HR(consumed->SetEventOnCompletion(1,ready));
    CK(WaitForSingleObject(ready,10000)==WAIT_OBJECT_0 && consumed->GetCompletedValue()==1 && published->GetCompletedValue()==1);
    CloseHandle(ready);readOwner.Reset();
#endif
    CK(WaitForSingleObject(retired,10000)==WAIT_OBJECT_0 && WaitForSingleObject(witness.gone,10000)==WAIT_OBJECT_0);
#ifdef TEST_PRIMARY_CLEAR
    UINT *gpuPixels;HR(read->Map(0,nullptr,(void**)&gpuPixels));
#ifdef TEST_TAIL_TIMESTAMP
    HR(queue->GetClockCalibration(&high,&cpu));auto gpuTicks=(UINT64*)((BYTE*)gpuPixels+tickOffset);
    CK(firstTail<ticks[1] && low<=ticks[1] && ticks[1]<=mid && mid<=ticks[0] && ticks[0]<=high);
    CK(gpuTicks[0]==ticks[0] && gpuTicks[1]==ticks[1]);
    for(UINT k=0;k<65536;k++){CK(reuse[k]==0x49);if(k<headOff-8 || k>=headOff+8)CK(original[k]==0xad);}
#ifdef TEST_TAGGED_BATCH
    CK(status(FALSE)==S_OK && status(TRUE)==S_OK && frameWait(dev,2,0,nullptr,0,&token)==DXGI_ERROR_WAIT_TIMEOUT);
    CK(configure(dev) && status(FALSE)==DXGI_ERROR_INVALID_CALL && status(TRUE)==DXGI_ERROR_INVALID_CALL);
    printf("PASS tagged singleton: sync fence retired while batch pending, separate tail completion, stale/duplicate rejection, head offset %#x\n",window+headOff);
#endif
    if(wb) {CK(VirtualFree(copy.base,0,MEM_RELEASE));}
    else {CK(UnmapViewOfFile(original));}
    CK(VirtualFree(reuse,0,MEM_RELEASE) && VirtualFree((void*)ctrl,65536,MEM_RELEASE));
    puts("PASS +240 pair: gated replay, original GPU timestamps and exact-VA replacement guards");
#endif
    for(UINT i=0;i<width*height;i++) {
        UINT expected=0;
#ifdef TEST_PRIMARY_RELEASE
        if (i%width==0) expected=0xff0000ff;
#endif
        CK(gpuPixels[i]==expected && ((UINT*)arena)[i]==expected && gpuPixels[width*height+i]==0xa5a5a5a5);
    }
    read->Unmap(0,nullptr);
#else
    const UINT *pixels=(UINT*)arena;for(UINT y=0;y<2160;y++)for(UINT x=0;x<3840;x++) CK(pixels[3840*y+x]==(x<1920?0xff00ff00u:0xff0000ffu));
#endif
    for(UINT i=3840*2160*4;i<(64u<<20);i++) CK(arena[i]==0xa5); // includes second image and padding
    unpin(backing.base);CloseHandle(retired);CloseHandle(witness.gone);HR(d->GetDeviceRemovedReason());
#if defined(TEST_PRIMARY_RELEASE)
    puts("PASS +490 release: independent Direct queue acquired original primary; verified stripe, black remainder, untouched sibling and padding");return 0;
#elif defined(TEST_PRIMARY_CLEAR)
    puts("PASS +1a0: all 8294400 RGBA pixels zero; second image unchanged on GPU readback, both slices/padding verified; RTV rewritten and guest heap released behind gate");return 0;
#else
    puts("PASS +490: UAV shader green / partial RTV red, second primary and padding intact; reset and release behind gate; host image destroyed after retirement"); return 0;
#endif
}
