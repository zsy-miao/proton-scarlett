#pragma once
/* Page provenance alone doesn't identify the device that owns this list. */
static inline BOOL xs_is_rtx3070(ID3D12Device *device)
{
    HRESULT (WINAPI *create)(REFIID,void **)=(void *)GetProcAddress(GetModuleHandleW(L"dxgi.dll"),"CreateDXGIFactory1");
    IDXGIFactory4 *factory;
    IDXGIAdapter1 *adapter;
    DXGI_ADAPTER_DESC1 desc;
    LUID luid;
    if (!create || FAILED(create(&IID_IDXGIFactory4,(void **)&factory))) return FALSE;
    device->lpVtbl->GetAdapterLuid(device,&luid);
    HRESULT hr=IDXGIFactory4_EnumAdapterByLuid(factory,luid,&IID_IDXGIAdapter1,(void **)&adapter);
    IDXGIFactory4_Release(factory);
    if (FAILED(hr)) return FALSE;
    hr=IDXGIAdapter1_GetDesc1(adapter,&desc);
    IDXGIAdapter1_Release(adapter);
    return SUCCEEDED(hr) && desc.VendorId==0x10de && desc.DeviceId==0x2484;
}
