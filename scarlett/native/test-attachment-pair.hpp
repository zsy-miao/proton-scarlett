// Fixture-only native recorder for checking the two barriers captured from the guest list.
#ifdef TEST_INLINE_B27
extern "C" {
__declspec(dllimport) void *WINAPI XMemAllocDefault(SIZE_T,UINT64);
__declspec(dllimport) void WINAPI XMemFreeDefault(void*,UINT64);
static BYTE *inline_arena;
static void *WINAPI take_inline(SIZE_T n,UINT64 attr){
    if(n==0x130000ull*32 && attr==0x800004 && inline_arena)return inline_arena;
    return XMemAllocDefault(n,attr);
}
static void WINAPI drop_inline(void *v,UINT64 a){if(v!=inline_arena)XMemFreeDefault(v,a);}
__declspec(dllexport) struct {decltype(&take_inline) a;decltype(&drop_inline) f;} __XMemAllocationHooks__={take_inline,drop_inline};
IUnknown *inline_pair[2];
}
#endif
extern "C" BOOL test_pair_record(ID3D12Device*,IUnknown*,IUnknown*,IUnknown*,IUnknown**,SIZE_T*,ID3D12GraphicsCommandList7*,ID3D12Resource**,IUnknown**,UINT64,IUnknown*);
#ifdef TEST_PAIR_FIXTURE
extern "C" BOOL test_sealed_pair(ID3D12Device*,IUnknown*,IUnknown*,IUnknown*,IUnknown**,SIZE_T*,UINT64);
#else /* Preserve the original fixture's embedded diagnostic IDs. */
#line 16
#endif
extern "C" BOOL test_source_lifetime(ID3D12Device*,IUnknown*,IUnknown*,UINT64,IUnknown*,void(*)(void*),void*);
extern "C" IUnknown *test_texture(IUnknown *,ID3D12Resource *,BOOL);
extern "C" HRESULT xs_resolve_color_rtv(IUnknown*,SIZE_T,D3D12_CPU_DESCRIPTOR_HANDLE*,ID3D12Resource**);
extern "C" HRESULT xs_resolve_depth_dsv(IUnknown*,SIZE_T,D3D12_CPU_DESCRIPTOR_HANDLE*,ID3D12Resource**);
static BYTE *pair_pool(){
    auto xm=LoadLibraryW(L"xmem.dll"); CK(xm);
    auto alloc=(void*(WINAPI*)(void*,SIZE_T,DWORD,UINT64,DWORD))(void*)GetProcAddress(xm,"XMemVirtualAlloc");
    CK(alloc);BYTE *b=(BYTE*)alloc(nullptr,0x89700000,0x20003000,0x283,0x2404);CK(b);return b;
}
static void test_pair(BYTE *base,ID3D12Device *d,IUnknown *owner,IUnknown *pso,IUnknown *root,IUnknown *other,ClearSourceControl &control){
    auto xm=GetModuleHandleW(L"xmem.dll");auto take=(HRESULT(WINAPI*)(ID3D12Device*,const void*,SIZE_T,ID3D12Heap**,ID3D12Resource**,UINT*))(void*)GetProcAddress(xm,"ScarlettAcquireNativePoolV1");CK(take);

    ComPtr<ID3D12Resource> backing;UINT off;HRESULT hr=take(d,base,1,nullptr,&backing,&off);CK(hr==S_OK || hr==S_FALSE);
    ComPtr<IUnknown> obj[2],heaps[2];ComPtr<ID3D12Resource> host[2];D3D12_CPU_DESCRIPTOR_HANDLE cpu[2];SIZE_T slots[2];
    auto vt=*(void***)owner;
    using Place=HRESULT(WINAPI*)(IUnknown*,UINT64,const void*,UINT,const void*,REFIID,void**);
    using Make=HRESULT(WINAPI*)(IUnknown*,const void*,REFIID,void**);using Start=SIZE_T(WINAPI*)(IUnknown*);
    using Writer=void(WINAPI*)(IUnknown*,IUnknown*,const void*,SIZE_T);
    for(UINT k=0;k<2;k++){
        DWORD wire[]={3,0,0,0,3840,0,2160,0x10001,k?19u:28u,1,0,0,k?2u:0x300001u};D3D12_CLEAR_VALUE cv={};cv.Format=k?DXGI_FORMAT_D32_FLOAT_S8X24_UINT:DXGI_FORMAT_R8G8B8A8_UNORM;
        UINT pos=backing?(k?0x17480000:0x15470000):(k?0x17fe0000:0x16000000);
        HR(((Place)vt[0x428/8])(owner,(SIZE_T)base+pos,wire,6,&cv,IID_ID3D12Resource,(void**)obj[k].GetAddressOf()));
        UINT hd[]={k?3u:2u,k?2048u:4096u,0,1};HR(((Make)vt[0x70/8])(owner,hd,IID_ID3D12DescriptorHeap,(void**)heaps[k].GetAddressOf()));
        slots[k]=((Start)(*(void***)heaps[k].Get())[0x48/8])(heaps[k].Get())+32;
        DWORD desc[]={k?20u:28u,k?3u:4u,0,0,0,0};((Writer)vt[(k?0xa8:0xa0)/8])(owner,obj[k].Get(),desc,slots[k]);
        HR((k?xs_resolve_depth_dsv:xs_resolve_color_rtv)(heaps[k].Get(),slots[k],&cpu[k],host[k].GetAddressOf()));
    }
    ComPtr<ID3D12CommandQueue> q;D3D12_COMMAND_QUEUE_DESC qd={};HR(d->CreateCommandQueue(&qd,IID_PPV_ARGS(&q)));
    ComPtr<ID3D12CommandAllocator> a;ComPtr<ID3D12GraphicsCommandList7> l;HR(d->CreateCommandAllocator(qd.Type,IID_PPV_ARGS(&a)));HR(d->CreateCommandList(0,qd.Type,a.Get(),nullptr,IID_PPV_ARGS(&l)));
    D3D12_TEXTURE_BARRIER b[2]={};for(UINT k=0;k<2;k++){
        b[k].pResource=host[k].Get();b[k].Subresources.IndexOrFirstMipLevel=UINT_MAX;
        b[k].SyncBefore=k?D3D12_BARRIER_SYNC_DEPTH_STENCIL:D3D12_BARRIER_SYNC_RENDER_TARGET;b[k].SyncAfter=D3D12_BARRIER_SYNC_ALL_SHADING;
        b[k].AccessBefore=k?D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE:D3D12_BARRIER_ACCESS_RENDER_TARGET;b[k].AccessAfter=D3D12_BARRIER_ACCESS_SHADER_RESOURCE;
        b[k].LayoutBefore=k?D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE:D3D12_BARRIER_LAYOUT_RENDER_TARGET;b[k].LayoutAfter=D3D12_BARRIER_LAYOUT_SHADER_RESOURCE;
    }
    D3D12_BARRIER_GROUP g={};g.Type=D3D12_BARRIER_TYPE_TEXTURE;g.NumBarriers=2;g.pTextureBarriers=b;l->Barrier(1,&g);
    IUnknown *ids[]={obj[0].Get(),obj[1].Get()};ID3D12Resource *images[]={host[0].Get(),host[1].Get()};
    ComPtr<IUnknown> siblings[2];IUnknown *sid[2];
    for(UINT k=0;k<2;k++){auto rd=images[k]->GetDesc();D3D12_HEAP_PROPERTIES hp={};hp.Type=D3D12_HEAP_TYPE_DEFAULT;hp.CreationNodeMask=hp.VisibleNodeMask=1;
        ComPtr<ID3D12Resource> image;HR(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,k?D3D12_RESOURCE_STATE_DEPTH_WRITE:D3D12_RESOURCE_STATE_RENDER_TARGET,nullptr,IID_PPV_ARGS(&image)));
        siblings[k].Attach(test_texture(owner,image.Get(),k));CK(siblings[k]);sid[k]=siblings[k].Get();}
    CK(XMemMapPhysicalPages(control.at[3],752,control.ids)==control.at[3] && XMemMapPhysicalPages(control.at[4],1600,control.ids+752)==control.at[4]);
    D3D12_RESOURCE_DESC arena={};arena.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;arena.Width=174ull<<20;arena.Height=arena.DepthOrArraySize=arena.MipLevels=arena.SampleDesc.Count=1;arena.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;arena.Flags=(D3D12_RESOURCE_FLAGS)0x20004;
    UINT64 va=(UINT64)control.at[2]+0x3ff80;ComPtr<IUnknown> placed,duplicate;
    auto make_arena=[&](ComPtr<IUnknown>& out){HR(((Place)vt[0x428/8])(owner,(UINT64)control.at[2],&arena,UINT_MAX,nullptr,IID_ID3D12Resource,(void**)out.GetAddressOf()));};
    make_arena(placed);make_arena(duplicate);
#ifdef TEST_INLINE_B27
    inline_arena=control.at[0];
    ComPtr<IUnknown> banks[2];
    UINT shapes[][4]={{0,0x130000,1,1},{1,128,1,1}};
    for(UINT j=0;j<2;j++)HR(((Make)vt[0x70/8])(owner,shapes[j],IID_ID3D12DescriptorHeap,(void**)banks[j].GetAddressOf()));
    inline_pair[0]=banks[0].Get();inline_pair[1]=banks[1].Get();
#endif
    control.write({.125f,.375f,.625f,.875f});memcpy((void*)va,control.record(),100);MemoryBarrier();
#ifdef TEST_PAIR_FIXTURE
    (void)images;(void)sid;
    HR(l->Close());ID3D12CommandList *prep[]={l.Get()};q->ExecuteCommandLists(1,prep);
    ComPtr<ID3D12Fence> prepared;HR(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&prepared)));
    HR(q->Signal(prepared.Get(),1));HR(prepared->SetEventOnCompletion(1,nullptr));
    duplicate.Reset();CK(test_sealed_pair(d,owner,pso,root,ids,slots,va));
    placed.Reset();control.replace();
    return;
#else
#line 70
#endif
    CK(test_pair_record(d,owner,pso,root,ids,slots,l.Get(),images,sid,va,duplicate.Detach()));
    memset((void*)va,0x11,100);
    auto replace=[](void *arg){auto c=(ClearSourceControl*)arg;BYTE *src=c->at[2]+0x3ff80;
        for(UINT k=0;k<100;k++){src[k]=(BYTE)(7+3*k);}MemoryBarrier();c->replace();memset(src,0xcd,100);MemoryBarrier();};
    CK(test_source_lifetime(d,owner,other,va,placed.Detach(),replace,&control));
    for(UINT k=0;k<2;k++){b[k].SyncAfter=D3D12_BARRIER_SYNC_COPY;b[k].LayoutAfter=D3D12_BARRIER_LAYOUT_COPY_SOURCE;b[k].AccessAfter=D3D12_BARRIER_ACCESS_COPY_SOURCE;}l->Barrier(1,&g);
    D3D12_HEAP_PROPERTIES hp={};hp.Type=D3D12_HEAP_TYPE_READBACK;hp.CreationNodeMask=hp.VisibleNodeMask=1;
    D3D12_RESOURCE_DESC rd={};rd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;rd.Width=1536;rd.Height=rd.MipLevels=rd.DepthOrArraySize=rd.SampleDesc.Count=1;rd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> read;HR(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&read)));
    // Depth/stencil plane copies must cover the entire subresource.
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp[2];UINT64 size;auto ds=host[1]->GetDesc();d->GetCopyableFootprints(&ds,0,2,0,fp,nullptr,nullptr,&size);
    rd.Width=size;ComPtr<ID3D12Resource> planes;HR(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&planes)));
    D3D12_TEXTURE_COPY_LOCATION from={},to={};from.pResource=host[0].Get();to.pResource=read.Get();to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;to.PlacedFootprint.Footprint={DXGI_FORMAT_R8G8B8A8_UNORM,1,1,1,256};D3D12_BOX box={0,0,0,1,1,1};l->CopyTextureRegion(&to,0,0,0,&from,&box);
    from.pResource=host[1].Get();to.pResource=planes.Get();for(UINT k=0;k<2;k++){from.SubresourceIndex=k;to.PlacedFootprint=fp[k];l->CopyTextureRegion(&to,0,0,0,&from,nullptr);}
    HR(l->Close());ID3D12CommandList *batch[]={l.Get()};q->ExecuteCommandLists(1,batch);ComPtr<ID3D12Fence> f;HR(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&f)));HR(q->Signal(f.Get(),1));HR(f->SetEventOnCompletion(1,nullptr));HR(d->GetDeviceRemovedReason());
    BYTE *map;HR(read->Map(0,nullptr,(void**)&map));CK(*(DWORD*)map==0xff00ff00);read->Unmap(0,nullptr);HR(planes->Map(0,nullptr,(void**)&map));
    CK(fp[0].Footprint.Format==DXGI_FORMAT_R32_TYPELESS && fp[1].Footprint.Format==DXGI_FORMAT_R8_TYPELESS);
    for(UINT y=0;y<ds.Height;y++)for(UINT x=0;x<ds.Width;x++){
        float z;memcpy(&z,map+fp[0].Offset+y*fp[0].Footprint.RowPitch+4*x,4);
        CK(z==.375f && map[fp[1].Offset+y*fp[1].Footprint.RowPitch+x]==0x69);
    }
    planes->Unmap(0,nullptr);puts("PASS original #4 recorder: both DSV planes unchanged over all 3840x2160 pixels");
    CK(VirtualFree(base,0,MEM_RELEASE));puts("PASS +190/+490: private split descriptors survived both producer rewrites; original color/depth/stencil readback, pending submit blocked");
}
