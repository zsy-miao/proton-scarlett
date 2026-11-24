/* Fixture-only borrowed private pipeline; callers hold the live wrapper. */
#include "d3d12_device.c"
ID3D12PipelineState *test_rearrange_host(IUnknown *pipeline,ID3D12RootSignature **root)
{
    struct bvh_pipeline *p=CONTAINING_RECORD(pipeline,struct bvh_pipeline,iface);
    if (pipeline->lpVtbl!=&bp_methods.unknown || (p->kind!=BVH_REARRANGE_TRIANGLES && p->kind!=BVH_REARRANGE_BVHS)) return NULL;
    *root=p->root;
    return p->host;
}
