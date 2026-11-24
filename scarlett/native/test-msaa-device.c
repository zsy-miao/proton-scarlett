#include "d3d12_device.c"
/* Fixture-only: pin the exact native image held by this successfully placed wrapper. */
ID3D12Resource *test_msaa_host(IUnknown *obj)
{
    struct color_resource *r=CONTAINING_RECORD(obj,struct color_resource,iface);
    if (obj->lpVtbl!=&color_vtbl.unknown || !r->msaa) return NULL;
    ID3D12Resource_AddRef(r->host);
    return r->host;
}

/* For native RTV creation and barrier recording only; shader reads use +90's host slot. */
ID3D12Resource *test_msaa_slot(IUnknown *heap,SIZE_T slot)
{
    struct desc_heap *h=CONTAINING_RECORD(heap,struct desc_heap,iface);
    D3D12_CPU_DESCRIPTOR_HANDLE cpu;
    if (FAILED(descriptor_slot(heap,slot,&cpu,NULL))) return NULL;
    struct descriptor_record *r=&h->records[(slot-h->start)/h->stride];
    if (!r->resource || r->kind!=SRV) return NULL;
    ID3D12Resource_AddRef(r->resource);return r->resource;
}
