/* The prep list is separate. Both submitted generations contain only typed
 * guest calls. The fixture owns the three live GPU-copy spans through end. */
BOOL test_sealed_pair(ID3D12Device *d,IUnknown *owner,IUnknown *pso,IUnknown *root,IUnknown **ids,SIZE_T *slots,UINT64 va)
{
    extern IUnknown *test_primary_image(IUnknown*,ID3D12Device*);
    IUnknown *q,*a,*l,*next,*fresh,*key=test_primary_image(owner,d);
    REQUIRE(key && release_fixture_clock(d,owner,&q) && xs_allocator(d,0,&a)==S_OK && xs_device_list(d,0,a,TRUE,owner,&l)==S_OK);
    BOOL cpu=xs_is_rtx3070(d);BYTE *page=NULL,*exe=(BYTE*)GetModuleHandleW(NULL);
    BYTE ctx[0x12800]={0},nodes[0xb0]={0};UINT_PTR entries[2]={0},stack[32+514]={0},*array=stack+32;
    struct pub_wire records[2]={0};UINT_PTR base=va-0x3ff80,kn=(UINT_PTR)nodes+0x58;
    struct lease_node *node=(struct lease_node*)kn;
    *(struct lease_arena*)(exe+0x75df1b8)=(struct lease_arena){base,(UINT_PTR)nodes,2,0x20000,0x40000};
    node->data=base+0x20000;node->gen=1;node->stamp=~1ull;node->holds=2;
    REQUIRE(register_callback(a,exe+0x555df10,ctx));
    IUnknown *title,*foreign,*early;
    REQUIRE(xs_shared_fence(d,&title)==S_OK && xs_direct_queue(d,&foreign)==S_OK && xs_shared_fence(d,&early)==S_OK);
    UINT_PTR *book=(UINT_PTR*)(exe+0x75e0e98);
    *(UINT_PTR*)(exe+0x68929d0)=(UINT_PTR)book;
    book[0]=(UINT_PTR)q;book[2]=(UINT_PTR)title;book[5]=2;book[6]=5;
    stack[0]=(UINT_PTR)(book+5);stack[1]=(UINT_PTR)title;stack[2]=3;
    REQUIRE(queue_signal(q,title,1)==S_OK && queue_signal(q,title,2)==S_OK &&
            queue_signal(q,early,1)==S_OK && queue_signal(q,early,2)==S_OK && queue_signal(q,early,3)==S_OK &&
            ID3D12Fence_SetEventOnCompletion(as_fence(early),3,NULL)==S_OK);
    void *(WINAPI *alloc)(void*,SIZE_T,DWORD,UINT64,DWORD)=(void*)GetProcAddress(GetModuleHandleW(L"xmem.dll"),"XMemVirtualAlloc");
    REQUIRE(alloc);
    if(cpu){
        HRESULT(WINAPI *create)(ID3D12Device*,SIZE_T,void**)=(void*)GetProcAddress(GetModuleHandleW(L"d3d12core.dll"),"ScarlettCpuControlViewV1");
        REQUIRE(create && create(d,65536,(void**)&page)==S_OK);
    }else page=alloc(NULL,65536,0x20401000,0x283,0x42404);
    BYTE *sys=alloc(NULL,0x49400000,0x20003000,0x183,0x22004);
    REQUIRE(sys && page);memset(page,0,65536);
    UINT64 arr=(UINT_PTR)sys+(cpu?0x419aeb0:0x419aa30),used=arr+0x4010;
#ifdef TEST_RTX_Q0_SECOND_PAIR
    REQUIRE(cpu);arr=(UINT_PTR)sys+0x419aa30;used=arr+0x4010;
#endif
    DWORD *rec=(DWORD*)(sys+0x3fb3050);rec[2]=0x800;rec[3]=8;rec[14]=2<<16;
    memcpy(rec+18,&used,8);memcpy(rec+20,&arr,8);memcpy(rec+22,&arr,8);
    *(DWORD*)(exe+0xa943a0c)=2;*(UINT64*)(exe+0x75dedb0)=1;memcpy(exe+0xa943a18+16,&rec,8);
    volatile UINT64 *ticks=(volatile UINT64*)(UINT_PTR)arr;ticks[0]=ticks[1]=0;
    pool_vtbl.bind(l,2,inline_pair);pool_vtbl.marker(l,(SIZE_T)page+0x50,1,0,0);
    pool_vtbl.bind(l,2,inline_pair);pool_vtbl.timestamp(l,arr+8,0,0,1);
    pool_vtbl.marker(l,(SIZE_T)page+0x54,0x29a5ee15,0,0);pool_vtbl.wait64(l,(SIZE_T)page,7,1,1);
    pool_vtbl.sync418(l,0xc280,0x20000,0,0xffffffffff00ull);
    D3D12_TEXTURE_BARRIER b[2]={{.SyncBefore=D3D12_BARRIER_SYNC_ALL_SHADING,.SyncAfter=D3D12_BARRIER_SYNC_RENDER_TARGET,
        .AccessBefore=D3D12_BARRIER_ACCESS_UNORDERED_ACCESS,.AccessAfter=D3D12_BARRIER_ACCESS_RENDER_TARGET,
        .LayoutBefore=D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS,.LayoutAfter=D3D12_BARRIER_LAYOUT_RENDER_TARGET,
        .pResource=(ID3D12Resource*)key,.Subresources={UINT_MAX,0,0,0,0,0}}};
    D3D12_BARRIER_GROUP bg={.Type=D3D12_BARRIER_TYPE_TEXTURE,.NumBarriers=1,.pTextureBarriers=b};
    SIZE_T output=slots[0]+32;
    REQUIRE(xs_write_view(owner,0xa0,key,(DWORD[]){28,4,0,0},output)==S_OK);
    pool_vtbl.barrier(l,1,&bg);pool_vtbl.clear(l,output,(FLOAT[4]){0},0,NULL);
    pool_vtbl.topology(l,D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);pool_vtbl.rate(l,0,(D3D12_SHADING_RATE_COMBINER[2]){0});
    pool_vtbl.pipeline(l,pso);pool_vtbl.root(l,root);pool_vtbl.depth_order(l,0);pool_vtbl.index(l,NULL);
    pool_vtbl.stencil(l,0);pool_vtbl.state470(l,UINT64_MAX);pool_vtbl.blend(l,ones_factor);
    for(UINT k=0;k<2;k++){
        b[k]=(D3D12_TEXTURE_BARRIER){.SyncBefore=0x1000,.SyncAfter=k?D3D12_BARRIER_SYNC_DEPTH_STENCIL:0x40,
            .AccessBefore=0x80,.AccessAfter=k?D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE:8,.LayoutBefore=6,
            .LayoutAfter=k?D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE:2,.pResource=(ID3D12Resource*)ids[k],.Subresources={UINT_MAX,0,0,0,0,0}};
    }
    bg.NumBarriers=2;pool_vtbl.barrier(l,1,&bg);pool_vtbl.om(l,1,slots,FALSE,slots+1);pool_vtbl.bounds(l,0,1);
    pool_vtbl.rect(l,1,&clear_rect);pool_vtbl.vp(l,1,&clear_viewport);pool_vtbl.source(l,0,va);pool_vtbl.draw(l,3,1,0,0);
    node->holds=3;pool_vtbl.topology(l,17);IUnknown *colour=late_root_id(FALSE);pool_vtbl.root(l,colour);
    D3D12_VERTEX_BUFFER_VIEW vb={va-0x1050,4080,24};pool_vtbl.vb(l,0,1,&vb);
    b[1]=b[0];b[0].SyncBefore=0x40;b[0].SyncAfter=0x1000;b[0].AccessBefore=8;b[0].AccessAfter=0x80;b[0].LayoutBefore=2;b[0].LayoutAfter=6;
    pool_vtbl.barrier(l,1,&bg);pool_vtbl.om(l,1,slots,FALSE,slots+1);pool_vtbl.bounds(l,0,1);
    pool_vtbl.b27(l,0,va-0x1080);pool_vtbl.root(l,colour);vb.BufferLocation-=0x1020;pool_vtbl.vb(l,0,1,&vb);
    bg.NumBarriers=1;pool_vtbl.barrier(l,1,&bg);pool_vtbl.om(l,1,&output,FALSE,NULL);pool_vtbl.bounds(l,0,1);
    UINT_PTR *active=(UINT_PTR*)(ctx+0x127c8);
    active[0]=array[0]=records[0].list=entries[0]=(UINT_PTR)l;active[1]=records[0].arg=array[512]=(UINT_PTR)entries;
    records[0].pool=array[256]=(UINT_PTR)ctx;records[0].acq=kn;active[2]=(UINT_PTR)&records[0].next;
    pool_vtbl.b27(l,0,va-0x1080-131*32);REQUIRE(close_at(l,(UINT_PTR)exe+0x72a1)==S_OK && sl(l)->seal);
    REQUIRE(xs_device_list(d,0,a,TRUE,owner,&next)==S_OK);pool_vtbl.bind(next,2,inline_pair);
    b[0].pResource=(ID3D12Resource*)key;b[0].SyncAfter=0;b[0].AccessAfter=D3D12_BARRIER_ACCESS_NO_ACCESS;b[0].LayoutAfter=31;
    pool_vtbl.barrier(next,1,&bg);pool_vtbl.timestamp(next,arr,0,0,1);
#ifdef TEST_RTX_Q0_SECOND_PAIR
    struct system_timestamp *h=sl(l)->stamp,*t=sl(next)->stamp;
    REQUIRE(sl(next)->successor_query && h && t && h!=t && h->cpu_offset==0x419aa38 && t->cpu_offset==0x419aa30 &&
        h->cpu_section && t->cpu_section && !h->retained && !t->retained &&
        h->mapping!=t->mapping && h->query!=t->query && h->dst!=t->dst);
#endif
    active[0]=array[1]=records[1].list=entries[1]=(UINT_PTR)next;active[1]=records[1].arg=array[513]=(UINT_PTR)(entries+1);
    records[1].pool=array[257]=(UINT_PTR)ctx;active[2]=(UINT_PTR)&records[1].next;
    REQUIRE(close_at(next,(UINT_PTR)exe+0x72a1)==S_OK && sl(next)->seal);
    node->holds=0;node->stamp=3;records[0].next=(UINT_PTR)(records+1);records[1].rel=kn;
    struct startup_list *s=sl(next);
    s->ops[2]^=1;REQUIRE(!pair_profile(s));s->ops[2]^=1;
    s->successor_query=FALSE;REQUIRE(!pair_profile(s));s->successor_query=TRUE;
    IUnknown **ls=(IUnknown**)array,*reverse[]={next,l};UINT before=executions;
    REQUIRE(FAILED(checked_tagged_submit(q,2,reverse,2,3)));
    struct release_receipt *r=s->deferred;r->writer_epoch++;
    REQUIRE(FAILED(checked_tagged_submit(q,2,ls,2,3)));r->writer_epoch--;
    node->gen++;REQUIRE(FAILED(checked_tagged_submit(q,2,ls,2,3)));node->gen--;
    records[1].rel=0;REQUIRE(FAILED(checked_tagged_submit(q,2,ls,2,3)));records[1].rel=kn;
    *(UINT_PTR*)(exe+0x68929d0)=0;REQUIRE(FAILED(checked_tagged_submit(q,2,ls,2,3)));*(UINT_PTR*)(exe+0x68929d0)=(UINT_PTR)book;
    book[2]=stack[1]=(UINT_PTR)early;REQUIRE(FAILED(checked_tagged_submit(q,2,ls,2,3)));
    IUnknown *mixed;REQUIRE(xs_shared_fence(d,&mixed)==S_OK && queue_signal(foreign,mixed,1)==S_OK && queue_signal(q,mixed,2)==S_OK);
    book[2]=stack[1]=(UINT_PTR)mixed;REQUIRE(FAILED(checked_tagged_submit(q,2,ls,2,3)));
    book[2]=stack[1]=(UINT_PTR)title;
    REQUIRE(executions==before && checked_tagged_submit(q,2,ls,2,3)==S_OK && executions==before+3);
    REQUIRE(FAILED(checked_tagged_submit(q,2,ls,2,3)) && executions==before+3);
    REQUIRE(queue_signal(foreign,title,3)==E_ACCESSDENIED && xs_cpu_signal(title,3)==E_ACCESSDENIED &&
        fence_completed(title)==2 && queue_signal(q,title,3)==S_OK);
    REQUIRE(xs_allocator(d,0,&fresh)==S_OK && sl_reset(l,fresh,NULL,0)==S_OK && !sl(l)->seal);
    IUnknown_Release(l);IUnknown_Release(next);IUnknown_Release(key);
    REQUIRE(allocator_reset(a)==DXGI_ERROR_WAS_STILL_DRAWING);
    DWORD start=GetTickCount();
    while(*(volatile DWORD*)(page+0x54)!=0x29a5ee15 && GetTickCount()-start<10000)Sleep(1);
    REQUIRE(*(volatile DWORD*)(page+0x50)==1 && *(volatile DWORD*)(page+0x54)==0x29a5ee15 && ticks[1] && !ticks[0]);
    REQUIRE(xs_frame_submission_query(owner,2,as_queue(q),TRUE)==E_PENDING && fence_completed(title)==2);
    UINT64 low,high,tick;REQUIRE(SUCCEEDED(ID3D12CommandQueue_GetClockCalibration(as_queue(q),&low,&tick)));
    InterlockedExchange64((LONG64*)page,1);
    start=GetTickCount();
    while(xs_frame_submission_query(owner,2,as_queue(q),TRUE)==E_PENDING && GetTickCount()-start<10000)Sleep(1);
    REQUIRE(xs_frame_submission_query(owner,2,as_queue(q),TRUE)==S_OK && SUCCEEDED(ID3D12Device_GetDeviceRemovedReason(d)));
    REQUIRE(SUCCEEDED(ID3D12CommandQueue_GetClockCalibration(as_queue(q),&high,&tick)) && ticks[0]>=low && ticks[0]<=high && ticks[0]>=ticks[1]);
    start=GetTickCount();while(InterlockedCompareExchange(&pair_inflight,0,0) && GetTickCount()-start<10000)Sleep(1);
    REQUIRE(allocator_reset(a)==S_OK);IUnknown_Release(fresh);IUnknown_Release(a);IUnknown_Release(q);
    REQUIRE(ID3D12Fence_SetEventOnCompletion(as_fence(title),3,NULL)==S_OK && fence_completed(title)==3);
    IUnknown_Release(title);IUnknown_Release(early);IUnknown_Release(mixed);IUnknown_Release(foreign);
    printf("PASS sealed token2 pair: held gate, retired reset/released generations, post-release timestamp %llu/%llu\n",ticks[1],ticks[0]);
    REQUIRE(publish_late());return TRUE;
}
