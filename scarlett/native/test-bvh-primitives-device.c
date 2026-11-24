/* Test translation unit only; no new adapter export or guest recording path. */
#include "d3d12_device.c"
ID3D12PipelineState *test_bvh_primitives_host(IUnknown *i,ID3D12RootSignature **r)
{
    struct bvh_pipeline *p=CONTAINING_RECORD(i,struct bvh_pipeline,iface);
    if(i->lpVtbl!=&bp_methods.unknown || p->kind<BVH_LOAD_TRIANGLES || p->kind>BVH_RELOAD_PROCEDURAL) return NULL;
    *r=p->root; return p->host;
}
