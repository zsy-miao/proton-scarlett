/* Test translation unit only; no new adapter export or guest recording path. */
#include "d3d12_device.c"
ID3D12PipelineState *test_bvh_split_pass_host(IUnknown *i,ID3D12RootSignature **r)
{
    struct bvh_pipeline *p=CONTAINING_RECORD(i,struct bvh_pipeline,iface);
    if(i->lpVtbl!=&bp_methods.unknown || p->kind!=BVH_SPLIT_PASS) return NULL;
    *r=p->root; return p->host;
}
