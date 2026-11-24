/* The two entries at +32113 are one request. A valid prefix alone must not
 * reach the host. The caller has already rewritten the public OM slots. */
static BOOL test_rt_roundtrip(struct startup_list *s,IUnknown *original,IUnknown *replacement,ID3D12Resource *host,BOOL admit)
{
    D3D12_TEXTURE_BARRIER b[2]={
        {.SyncBefore=0x40,.SyncAfter=0x1000,.AccessBefore=8,.AccessAfter=0x80,.LayoutBefore=2,.LayoutAfter=6},
        {.SyncBefore=0x1000,.SyncAfter=0x40,.AccessBefore=0x80,.AccessAfter=8,.LayoutBefore=6,.LayoutAfter=2}
    };
    for(UINT k=0;k<2;k++){b[k].pResource=(ID3D12Resource*)original;b[k].Subresources.IndexOrFirstMipLevel=UINT_MAX;}
    D3D12_BARRIER_GROUP g={.Type=D3D12_BARRIER_TYPE_TEXTURE,.NumBarriers=2,.pTextureBarriers=b};
    UINT before=barrier_total;
    if(!admit)return FAILED(record_primary_barrier(s,1,&g)) && barrier_total==before && !s->rt_roundtripped;
    D3D12_TEXTURE_BARRIER reversed[]={b[1],b[0]};g.pTextureBarriers=reversed;
    REQUIRE(FAILED(record_primary_barrier(s,1,&g)));g.pTextureBarriers=b;
    b[1].pResource=(ID3D12Resource*)replacement;REQUIRE(FAILED(record_primary_barrier(s,1,&g)));
    b[0].pResource=b[1].pResource;REQUIRE(FAILED(record_primary_barrier(s,1,&g)));
    b[0].pResource=b[1].pResource=(ID3D12Resource*)original;
    b[1].LayoutAfter=6;REQUIRE(FAILED(record_primary_barrier(s,1,&g)));b[1].LayoutAfter=2;
    b[1].Flags=1;REQUIRE(FAILED(record_primary_barrier(s,1,&g)));b[1].Flags=0;
    b[1].Subresources.NumPlanes=1;REQUIRE(FAILED(record_primary_barrier(s,1,&g)));b[1].Subresources.NumPlanes=0;
    REQUIRE(barrier_total==before && !s->rt_roundtripped && s->pending_470);
    REQUIRE(record_primary_barrier(s,1,&g)==S_OK && barrier_total==before+1 &&
            barrier_host==s->host && s->rt_roundtripped && s->pending_470 && s->depth_zero_pending);
    REQUIRE(FAILED(record_primary_barrier(s,1,&g)) && barrier_total==before+1);
    for(UINT k=0;k<2;k++){
        REQUIRE(b[k].pResource==(ID3D12Resource*)original);b[k].pResource=host;
        REQUIRE(!memcmp(b+k,seen+k,offsetof(D3D12_TEXTURE_BARRIER,Flags)+sizeof b[k].Flags));
    }
    return TRUE;
}
