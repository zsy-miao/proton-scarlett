/* Borrowed only while the fixture owns the wrapper. */
#include "d3d12_device.c"
ID3D12PipelineState *test_postbuild_host(IUnknown *i,ID3D12RootSignature **root)
{
    struct bvh_pipeline *p=CONTAINING_RECORD(i,struct bvh_pipeline,iface);
    if (i->lpVtbl!=&bp_methods.unknown || p->kind!=BVH_POSTBUILD) return NULL;
    *root=p->root;
    return p->host;
}
