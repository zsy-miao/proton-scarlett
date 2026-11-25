#include "d3d12_device.c"
#ifdef TEST_SEVEN_VA
UINT64 test_seven_guest_va(IUnknown *id,UINT_PTR caller)
{
    return seven_guest_va(id,caller);
}
#endif
/* Fixture only: acquire the buffer that the actual +428 factory pinned. */
ID3D12Resource *test_r32_backing(IUnknown *object)
{
    struct color_resource *r=CONTAINING_RECORD(object,struct color_resource,iface);
    if (object->lpVtbl!=&color_vtbl.unknown || !r->tiled_r32) return NULL;
    ID3D12Resource_AddRef(r->host);return r->host;
}
ID3D12Resource *test_r11_backing(IUnknown *object)
{
    struct color_resource *r=CONTAINING_RECORD(object,struct color_resource,iface);
    if (object->lpVtbl!=&color_vtbl.unknown || !r->tiled_r11) return NULL;
    ID3D12Resource_AddRef(r->host);return r->host;
}
ID3D12Resource *test_row_backing(IUnknown *object)
{
    if (object->lpVtbl!=&row_vtbl.unknown) return NULL;
    struct row_buffer *r=CONTAINING_RECORD(object,struct row_buffer,iface);
    ID3D12Resource_AddRef(r->host);return r->host;
}
/* Inspect the retained descriptor, not a new allocation at the public address. */
ID3D12Resource *test_small_tile_backing(IUnknown *heap,SIZE_T slot,DXGI_FORMAT fmt)
{
    struct desc_heap *h=CONTAINING_RECORD(heap,struct desc_heap,iface);
    UINT kind=fmt==DXGI_FORMAT_R32G32_FLOAT?TILE_RG_SRV:fmt==DXGI_FORMAT_R32_UINT?TILE_UINT160_SRV:TILE_F_SRV;
    D3D12_CPU_DESCRIPTOR_HANDLE unused;
    if(!h->visible || !h->records || FAILED(descriptor_slot(heap,slot,&unused,NULL)))return NULL;
    struct descriptor_record *r=&h->records[(slot-h->start)/h->stride];
    if(!valid_address_record(r,slot,kind))return NULL;
    ID3D12Resource_AddRef(r->resource);return r->resource;
}
