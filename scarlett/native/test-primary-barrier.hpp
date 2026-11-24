/* Included by test-allocation-info.cpp: reuse its actual factory, heaps and two hooked slices. */
#include "xmem/display_backing.h"
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
#endif
    clear[0]=1; // caller storage is reusable immediately
    DWORD view[]={28,4,0,0,0,0};HR(xs_write_view(dev,0xa0,other,view,dest)); // same slot now names the other image
    ComPtr<ID3D12Resource> sibling,read;HR(xs_resolve_color_rtv(views.Get(),dest,&ignored,sibling.GetAddressOf()));CK(sibling.Get()!=texture.Get());
    const UINT width=3840,height=2160,imageBytes=width*height*4;
    D3D12_RESOURCE_DESC bd={};bd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;bd.Width=2*imageBytes;
    bd.Height=bd.DepthOrArraySize=bd.MipLevels=bd.SampleDesc.Count=1;bd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    D3D12_HEAP_PROPERTIES hp={};hp.Type=D3D12_HEAP_TYPE_READBACK;hp.CreationNodeMask=hp.VisibleNodeMask=1;
    HR(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&bd,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&read)));
    auto copyList=l;
#ifdef TEST_PRIMARY_RELEASE
    t={D3D12_BARRIER_SYNC_RENDER_TARGET,D3D12_BARRIER_SYNC_NONE,D3D12_BARRIER_ACCESS_RENDER_TARGET,D3D12_BARRIER_ACCESS_NO_ACCESS,
       D3D12_BARRIER_LAYOUT_RENDER_TARGET,(D3D12_BARRIER_LAYOUT)0x1f,(ID3D12Resource*)other,{UINT_MAX,0,0,0,0,0},D3D12_TEXTURE_BARRIER_FLAG_NONE};
    group.Type=D3D12_BARRIER_TYPE_TEXTURE;group.NumBarriers=1;group.pTextureBarriers=&t;
    CK(test_primary_try(list.Get(),1,&group)==E_INVALIDARG);
    t.pResource=(ID3D12Resource*)surface;SLOT(list.Get(),0x490,Barrier)(list.Get(),1,&group);
    CK(test_primary_try(list.Get(),1,&group)==E_NOTIMPL && !xs_list_host(list.Get()));
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
    PrimaryWitness witness;CK(witness.gone);const GUID tag={0x490,0,0,{}};
    HR(texture->SetPrivateDataInterface(tag,&witness));CK(witness.refs==2);
    auto pin=(decltype(&ScarlettRetainDisplayBackingV1))(void*)GetProcAddress(xm,"ScarlettRetainDisplayBackingV1");
    auto unpin=(decltype(&ScarlettReleaseDisplayBackingV1))(void*)GetProcAddress(xm,"ScarlettReleaseDisplayBackingV1");
    XsDisplayBacking backing;CK(pin && unpin); HR(pin(arena,slice,&backing));CK(backing.base==arena);
    ComPtr<IUnknown> q,gate,done;HR(xs_direct_queue(d,q.GetAddressOf()));HR(xs_shared_fence(d,gate.GetAddressOf()));HR(xs_shared_fence(d,done.GetAddressOf()));
#ifdef TEST_PRIMARY_RELEASE
    HR(acquire->Close());
    ComPtr<ID3D12Fence> published,consumed;HR(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&published)));
    HR(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&consumed)));
    HR(reader->Wait(published.Get(),1));ID3D12CommandList *reads[]={acquire.Get()};reader->ExecuteCommandLists(1,reads);HR(reader->Signal(consumed.Get(),1));
    ComPtr<ID3D12Resource> readOwner=texture;
#endif
    HR(xs_queue_wait(q.Get(),gate.Get(),1));IUnknown *batch[]={list.Get()};SLOT(q.Get(),0x118,Submit)(q.Get(),1,batch,0,0);
    HR(SLOT(q.Get(),0x80,Signal)(q.Get(),done.Get(),1));HANDLE retired=CreateEventW(nullptr,FALSE,FALSE,nullptr);CK(retired);
    HR(xs_fence_event(done.Get(),1,retired));HR(SLOT(list.Get(),0x458,Reset)(list.Get(),b.Get(),nullptr,0));list.Reset();a.Reset();
    CK(other->Release()==0 && surface->Release()==0 && given==2);views.Reset();heap.Reset();texture.Reset();
    CK(WaitForSingleObject(retired,50)==WAIT_TIMEOUT && WaitForSingleObject(witness.gone,0)==WAIT_TIMEOUT);
#ifdef TEST_PRIMARY_RELEASE
    HR(test_host_queue(q.Get())->Signal(published.Get(),1));CK(published->GetCompletedValue()==0 && consumed->GetCompletedValue()==0);
#endif
    CK(arena[0]==0xa5 && arena[3839*4]==0xa5);HR(xs_cpu_signal(gate.Get(),1));
#ifdef TEST_PRIMARY_RELEASE
    HANDLE ready=CreateEventW(nullptr,FALSE,FALSE,nullptr);CK(ready);HR(consumed->SetEventOnCompletion(1,ready));
    CK(WaitForSingleObject(ready,10000)==WAIT_OBJECT_0 && consumed->GetCompletedValue()==1 && published->GetCompletedValue()==1);
    CloseHandle(ready);readOwner.Reset();
#endif
    CK(WaitForSingleObject(retired,10000)==WAIT_OBJECT_0 && WaitForSingleObject(witness.gone,10000)==WAIT_OBJECT_0);
#ifdef TEST_PRIMARY_CLEAR
    UINT *gpuPixels;HR(read->Map(0,nullptr,(void**)&gpuPixels));
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
