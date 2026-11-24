#include "d3d12_device.c"
/* Fixture-only: host is borrowed while the retail-IID bootstrap wrapper is live. */
HRESULT get_test_device(IUnknown **owner, ID3D12Device **host)
{
    DWORD w[20]={0x20004,0,0x100000,0,0,0,0,0,0,0,1,0,0,0,0,0x20};
    HRESULT hr=D3D12XboxCreateDevice(NULL,w,&bootstrap_iid,(void **)owner);
    if (SUCCEEDED(hr)) *host=CONTAINING_RECORD(*owner,struct xbox_device,iface)->host;
    return hr;
}
