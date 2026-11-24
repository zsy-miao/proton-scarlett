#include "d3d12_device.c"

/* Fixture only: borrow the host object while the successful guest PSO is live. */
ID3D12PipelineState *clear_test_host(IUnknown *i)
{
    return i && i->lpVtbl==&clear_methods.unknown ? CONTAINING_RECORD(i,struct bvh_pipeline,iface)->host : NULL;
}
