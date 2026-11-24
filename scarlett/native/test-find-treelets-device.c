#include "d3d12_device.c"
/* Test-only access; do not expose host PSOs or descriptors to the title. */
ID3D12PipelineState *test_treelets_host(IUnknown *i,ID3D12RootSignature **r)
{
    struct bvh_pipeline *p=CONTAINING_RECORD(i,struct bvh_pipeline,iface);
    if(i->lpVtbl!=&bp_methods.unknown || p->kind!=BVH_FIND_TREELETS) return NULL;
    *r=p->root; return p->host;
}
