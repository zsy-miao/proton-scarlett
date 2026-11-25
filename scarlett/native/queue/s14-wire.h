/* The guest release names the registered identity; the GPU sees its pinned host. */
static HRESULT s14_emit(IUnknown *owner,const struct xs_attachment_pin *pin,
        ID3D12GraphicsCommandList7 *list,D3D12_TEXTURE_BARRIER b)
{
    const D3D12_BARRIER_SUBRESOURCE_RANGE all={.IndexOrFirstMipLevel=UINT_MAX};
    if(!pin || !list || pin->serial!=14 || b.pResource!=(ID3D12Resource*)pin->identity ||
       b.SyncBefore!=D3D12_BARRIER_SYNC_RENDER_TARGET || b.SyncAfter!=D3D12_BARRIER_SYNC_NONE ||
       b.AccessBefore!=D3D12_BARRIER_ACCESS_RENDER_TARGET || b.AccessAfter!=D3D12_BARRIER_ACCESS_NO_ACCESS ||
       b.LayoutBefore!=D3D12_BARRIER_LAYOUT_RENDER_TARGET || (UINT)b.LayoutAfter!=31 ||
       b.Flags || memcmp(&b.Subresources,&all,sizeof all) ||
       !xs_live_output(owner,pin->identity,pin->host,pin->slot,pin->serial))return E_ACCESSDENIED;
    b.LayoutAfter=D3D12_BARRIER_LAYOUT_COMMON;b.pResource=pin->host;
    D3D12_BARRIER_GROUP g={.Type=D3D12_BARRIER_TYPE_TEXTURE,.NumBarriers=1,.pTextureBarriers=&b};
    ID3D12GraphicsCommandList7_Barrier(list,1,&g);
    return S_OK;
}
