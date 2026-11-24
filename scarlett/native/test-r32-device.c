#include "d3d12_device.c"
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
