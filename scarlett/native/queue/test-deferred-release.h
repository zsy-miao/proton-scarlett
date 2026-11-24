/* Drive the actual device clock and tagged sync singleton before the composite Close. */
static BOOL release_fixture_clock(ID3D12Device *hd,IUnknown *owner,IUnknown **queue)
{
    void **vt=(void**)owner->lpVtbl;
    HRESULT (WINAPI *interval)(IUnknown*,IDXGIOutput*,UINT,UINT,UINT)=(void*)vt[0x270/8];
    HRESULT (WINAPI *schedule)(IUnknown*,UINT,UINT,const void*,UINT)=(void*)vt[0x278/8];
    HRESULT (WINAPI *wait)(IUnknown*,UINT,DWORD,const void*,UINT,UINT64*)=(void*)vt[0x280/8];
    UINT64 token=0;
    REQUIRE(interval(owner,NULL,16667,1,0)==S_OK && schedule(owner,0,0,NULL,0)==S_OK &&
            schedule(owner,2,0,NULL,0)==S_OK && wait(owner,0,1000,NULL,0,&token)==S_OK &&
            wait(owner,0,1000,NULL,0,&token)==S_OK && token==2);
    IUnknown *a,*l;
    REQUIRE(xs_console_direct_queue(hd,owner,queue)==S_OK && xs_allocator(hd,0,&a)==S_OK &&
            xs_device_list(hd,0,a,FALSE,owner,&l)==S_OK && sl_close(l)==S_OK &&
            checked_tagged_submit(*queue,1,&l,token,0)==S_OK);
    IUnknown_Release(l);IUnknown_Release(a);return TRUE;
}
static BOOL test_successor(ID3D12Device *hd,IUnknown *owner,struct startup_list *producer,IUnknown *q,
        IUnknown *wrong,IUnknown *const *heaps)
{
    IUnknown *a,*l,*other,*a2;
    REQUIRE(xs_allocator(hd,0,&a)==S_OK && xs_device_list(hd,0,a,TRUE,owner,&l)==S_OK &&
            xs_allocator(hd,0,&a2)==S_OK && xs_device_list(hd,0,a2,TRUE,owner,&other)==S_OK);
    struct startup_list *s=sl(l);struct release_receipt *r=producer->offered;
    REQUIRE(r && !r->claimed && !s->primary && !s->primary_key && !s->deferred);
    D3D12_TEXTURE_BARRIER b={.pResource=(ID3D12Resource*)r->key,.Subresources={UINT_MAX,0,0,0,0,0},
        .SyncBefore=0x40,.SyncAfter=0,.AccessBefore=8,.AccessAfter=D3D12_BARRIER_ACCESS_NO_ACCESS,
        .LayoutBefore=2,.LayoutAfter=(D3D12_BARRIER_LAYOUT)31};
    D3D12_BARRIER_GROUP g={.Type=D3D12_BARRIER_TYPE_TEXTURE,.NumBarriers=1,.pTextureBarriers=&b};
    UINT before=barrier_total;
    REQUIRE(FAILED(record_primary_barrier(s,1,&g)) && bind_initial_heaps(s,2,heaps)==S_OK);
    b.pResource=(ID3D12Resource*)wrong;REQUIRE(FAILED(record_primary_barrier(s,1,&g)));b.pResource=(ID3D12Resource*)r->key;
    b.SyncBefore=0;REQUIRE(FAILED(record_primary_barrier(s,1,&g)));b.SyncBefore=0x40;
    b.Subresources.NumMipLevels=1;REQUIRE(FAILED(record_primary_barrier(s,1,&g)));b.Subresources.NumMipLevels=0;
    UINT64 *tag=(UINT64*)r->slot;(*tag)++;REQUIRE(FAILED(record_primary_barrier(s,1,&g)));(*tag)--;
    REQUIRE(!s->deferred && !r->claimed && before==barrier_total);
    pool_vtbl.barrier(l,1,&g);
    REQUIRE(s->deferred==r && r->key==producer->primary_pin && r->host==producer->primary &&
            r->slot==producer->output_slot && r->serial==producer->output_serial && r->token==2 &&
            r->table_epoch==producer->table_epoch && !s->primary_released && !s->primary && !xs_list_host(l) &&
            !memcmp(&s->deferred_wire,&b,offsetof(D3D12_TEXTURE_BARRIER,Flags)+sizeof b.Flags));
    REQUIRE(FAILED(record_primary_barrier(s,1,&g)) && bind_initial_heaps(sl(other),2,heaps)==S_OK &&
            FAILED(record_primary_barrier(sl(other),1,&g)) && !sl(other)->deferred);
    REQUIRE(sl_close(l)==S_OK && !xs_list_host(l) && checked_startup_submit(q,1,&l,0,0)==E_ACCESSDENIED &&
            checked_tagged_submit(q,1,&l,2,3)==E_ACCESSDENIED && FAILED(checked_tagged_submit(q,1,&l,2,0)));
    REQUIRE(FAILED(sl_reset(l,a,NULL,1)) && s->deferred==r && sl_reset(l,a,NULL,0)==S_OK && !s->deferred &&
            producer->offered==r && !s->deferred_wire.pResource && r->claimed && before==barrier_total);
    IUnknown_Release(l);IUnknown_Release(other);IUnknown_Release(a2);IUnknown_Release(a);
    puts("PASS successor +490: closed producer receipt, live RTV, token/queue, exact deferred wire, replay/borrow/submit denied, reset retired intent");
    return TRUE;
}
