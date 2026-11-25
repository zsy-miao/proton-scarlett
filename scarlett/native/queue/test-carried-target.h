/* Test TU only. No WSI: register a synthetic return in COMMON under its
 * original writer and view generation, then clear through the ordinary RTV. */
static BOOL prepare_returned_target(struct startup_list *s,ID3D12CommandQueue *writer,SIZE_T slot)
{
    extern IUnknown *test_returned_image(IUnknown*,ID3D12Device*);
    IUnknown *key=test_returned_image(s->pool_owner,s->device);
    REQUIRE(key && (s->primary=xs_acquire_primary(s->pool_owner,key,&s->primary_pin)) && s->primary_pin==key);
    s->primary_key=(UINT_PTR)key;
    REQUIRE(xs_write_view(s->pool_owner,0xa0,key,(DWORD[]){28,4,0,0},slot)==S_OK &&
        xs_primary_writer(s->pool_owner,s->primary,writer)==S_OK);
    UINT64 serial=*(UINT64*)slot;
    IUnknown *flip=xs_claim_view(s->pool_owner,key,s->primary,writer,slot,serial,TRUE);
    REQUIRE(flip==key);xs_returned_common(flip,slot,serial);xs_finish_flip(flip);IUnknown_Release(flip);
    REQUIRE(xs_reuse_common(s->pool_owner,key,s->primary,NULL,&s->reuse_epoch)==S_OK && s->reuse_epoch==1);
    D3D12_RESOURCE_BARRIER b={.Transition={s->primary,0,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_RENDER_TARGET}};
    ID3D12GraphicsCommandList7_ResourceBarrier(s->host,1,&b);
    REQUIRE(record_primary_clear(s,slot,(FLOAT[4]){0},0,NULL)==S_OK && s->clear_serial==serial);
    IUnknown_Release(key);return TRUE;
}

BOOL test_target_frame(IUnknown*,ID3D12CommandQueue*,ID3D12Fence*);
