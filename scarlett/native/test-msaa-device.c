#include "d3d12_device.c"
/* Fixture-only: pin the exact native image held by this successfully placed wrapper. */
ID3D12Resource *test_msaa_host(IUnknown *obj)
{
    struct color_resource *r=CONTAINING_RECORD(obj,struct color_resource,iface);
    if (obj->lpVtbl!=&color_vtbl.unknown || !r->msaa) return NULL;
    ID3D12Resource_AddRef(r->host);
    return r->host;
}
