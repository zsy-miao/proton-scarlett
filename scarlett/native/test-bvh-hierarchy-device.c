/* Fixture-only borrowed private pipeline; callers hold the live wrapper. */
#include "d3d12_device.c"
ID3D12PipelineState *test_hierarchy_host(IUnknown *pipeline,ID3D12RootSignature **root)
{
    struct bvh_pipeline *p=CONTAINING_RECORD(pipeline,struct bvh_pipeline,iface);
    if (pipeline->lpVtbl!=&bp_methods.unknown || (p->kind!=BVH_SPLITS32 && p->kind!=BVH_SPLITS64)) return NULL;
    *root=p->root;
    return p->host;
}
