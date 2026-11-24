/* Included by test-allocation-info.cpp: reuse its actual factory, heaps and two hooked slices. */
#include "xmem/display_backing.h"
extern "C" HRESULT test_primary_try(IUnknown *,UINT,const D3D12_BARRIER_GROUP *);
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
    auto l=xs_list_host(list.Get()); CK(l);
    D3D12_CPU_DESCRIPTOR_HANDLE ignored; ComPtr<ID3D12Resource> texture;
    HR(xs_resolve_color_rtv(views.Get(),dest,&ignored,texture.GetAddressOf()));
    ID3D12DescriptorHeap *bank; D3D12_GPU_DESCRIPTOR_HANDLE uav;
    HR(xs_resolve_view(heap.Get(),slot,0x98,&bank,&uav)); ComPtr<ID3D12DescriptorHeap> keepBank=bank;
    D3D12_DESCRIPTOR_HEAP_DESC hd={D3D12_DESCRIPTOR_HEAP_TYPE_RTV,1,D3D12_DESCRIPTOR_HEAP_FLAG_NONE,1};
    ComPtr<ID3D12DescriptorHeap> rtvs; HR(d->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&rtvs)));
    auto rtv=rtvs->GetCPUDescriptorHandleForHeapStart(); d->CreateRenderTargetView(texture.Get(),nullptr,rtv);
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
    const FLOAT red[]={1,0,0,1};D3D12_RECT right={1920,0,3840,2160}; l->ClearRenderTargetView(rtv,red,1,&right);
    HR(SLOT(list.Get(),0x48,Close)(list.Get()));
    PrimaryWitness witness;CK(witness.gone);const GUID tag={0x490,0,0,{}};
    HR(texture->SetPrivateDataInterface(tag,&witness));CK(witness.refs==2);
    auto pin=(decltype(&ScarlettRetainDisplayBackingV1))(void*)GetProcAddress(xm,"ScarlettRetainDisplayBackingV1");
    auto unpin=(decltype(&ScarlettReleaseDisplayBackingV1))(void*)GetProcAddress(xm,"ScarlettReleaseDisplayBackingV1");
    XsDisplayBacking backing;CK(pin && unpin); HR(pin(arena,slice,&backing));CK(backing.base==arena);
    ComPtr<IUnknown> q,gate,done;HR(xs_direct_queue(d,q.GetAddressOf()));HR(xs_shared_fence(d,gate.GetAddressOf()));HR(xs_shared_fence(d,done.GetAddressOf()));
    HR(xs_queue_wait(q.Get(),gate.Get(),1));IUnknown *batch[]={list.Get()};SLOT(q.Get(),0x118,Submit)(q.Get(),1,batch,0,0);
    HR(SLOT(q.Get(),0x80,Signal)(q.Get(),done.Get(),1));HANDLE retired=CreateEventW(nullptr,FALSE,FALSE,nullptr);CK(retired);
    HR(xs_fence_event(done.Get(),1,retired));HR(SLOT(list.Get(),0x458,Reset)(list.Get(),b.Get(),nullptr,0));list.Reset();a.Reset();
    CK(other->Release()==0 && surface->Release()==0 && given==2);views.Reset();heap.Reset();texture.Reset();
    CK(WaitForSingleObject(retired,50)==WAIT_TIMEOUT && WaitForSingleObject(witness.gone,0)==WAIT_TIMEOUT);
    CK(arena[0]==0xa5 && arena[3839*4]==0xa5);HR(xs_cpu_signal(gate.Get(),1));
    CK(WaitForSingleObject(retired,10000)==WAIT_OBJECT_0 && WaitForSingleObject(witness.gone,10000)==WAIT_OBJECT_0);
    const UINT *pixels=(UINT*)arena;for(UINT y=0;y<2160;y++)for(UINT x=0;x<3840;x++) CK(pixels[3840*y+x]==(x<1920?0xff00ff00u:0xff0000ffu));
    for(UINT i=3840*2160*4;i<(64u<<20);i++) CK(arena[i]==0xa5); // includes second image and padding
    unpin(backing.base);CloseHandle(retired);CloseHandle(witness.gone);HR(d->GetDeviceRemovedReason());
    puts("PASS +490: UAV shader green / partial RTV red, second primary and padding intact; reset and release behind gate; host image destroyed after retirement"); return 0;
}
