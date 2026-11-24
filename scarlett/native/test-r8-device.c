#include "d3d12_device.c"

/* Fixture-only host returning a failed/invalid aggregate, no HRESULT channel. */
static D3D12_RESOURCE_ALLOCATION_INFO injected;
static D3D12_RESOURCE_ALLOCATION_INFO *WINAPI bad_info(ID3D12Device *host,D3D12_RESOURCE_ALLOCATION_INFO *out,
        UINT node,UINT count,const D3D12_RESOURCE_DESC *rd)
{
    (void)host; (void)node; (void)count; (void)rd;
    *out=injected;
    return out;
}
int r8_bad_host(UINT64 size,UINT64 alignment,const void *wire)
{
    static const ID3D12DeviceVtbl methods={.GetResourceAllocationInfo=bad_info};
    ID3D12Device host={&methods};
    struct xbox_device dev={.host=&host};
    D3D12_RESOURCE_ALLOCATION_INFO out={0};
    injected=(D3D12_RESOURCE_ALLOCATION_INFO){size,alignment};
    return bootstrap_allocation_info(&out,&dev.iface,1,1,wire)==&out &&
        out.SizeInBytes==UINT64_MAX && out.Alignment==D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
}
