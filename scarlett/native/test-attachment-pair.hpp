// Fixture-only native recorder for checking the two barriers captured from the guest list.
extern "C" BOOL test_pair_record(ID3D12Device*,IUnknown*,IUnknown*,IUnknown*,IUnknown**,SIZE_T*,ID3D12GraphicsCommandList7*,ID3D12Resource**,IUnknown**);
extern "C" IUnknown *test_texture(IUnknown *,ID3D12Resource *,BOOL);
extern "C" HRESULT xs_resolve_color_rtv(IUnknown*,SIZE_T,D3D12_CPU_DESCRIPTOR_HANDLE*,ID3D12Resource**);
extern "C" HRESULT xs_resolve_depth_dsv(IUnknown*,SIZE_T,D3D12_CPU_DESCRIPTOR_HANDLE*,ID3D12Resource**);
static BYTE *pair_pool(){
    auto xm=LoadLibraryW(L"xmem.dll"); CK(xm);
    auto alloc=(void*(WINAPI*)(void*,SIZE_T,DWORD,UINT64,DWORD))(void*)GetProcAddress(xm,"XMemVirtualAlloc");
    CK(alloc);BYTE *b=(BYTE*)alloc(nullptr,0x89700000,0x20003000,0x283,0x2404);CK(b);return b;
}
static void test_pair(BYTE *base,ID3D12Device *d,IUnknown *owner,IUnknown *pso,IUnknown *root){
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
    CK(test_pair_record(d,owner,pso,root,ids,slots,l.Get(),images,sid));
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
    BYTE *map;HR(read->Map(0,nullptr,(void**)&map));CK(*(DWORD*)map==0xff00ff00);read->Unmap(0,nullptr);HR(planes->Map(0,nullptr,(void**)&map));CK(*(float*)(map+fp[0].Offset)==.375f && map[fp[1].Offset]==0x69);planes->Unmap(0,nullptr);
    CK(VirtualFree(base,0,MEM_RELEASE));puts("PASS +190/+490: private split descriptors survived both producer rewrites; original color/depth/stencil readback, pending submit blocked");
}
