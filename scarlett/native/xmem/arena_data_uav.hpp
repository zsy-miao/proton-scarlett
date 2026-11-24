#pragma once
#include <d3d12.h>

// Private: input is a retained, verified RW section from the arena capture owner.
// No VA lookup, allocation, code-section handle, or writable guest export.
static inline HRESULT xs_place_arena_data_uav(ID3D12Device3 *d, HANDLE input, UINT policy,
        SIZE_T bytes, ID3D12Resource **out)
{
    if (!out) return E_POINTER;
    *out=nullptr;
    bool wb=policy==0x2004;
    if (!d || !input || !(wb ? bytes==(100u<<20) : policy==0x2404 && bytes==(47u<<20)))
        return E_INVALIDARG;
    using Open = HRESULT (WINAPI *)(ID3D12Device3 *,SIZE_T,HANDLE,HANDLE *,ID3D12Heap **);
    auto open=(Open)(void *)GetProcAddress(GetModuleHandleW(L"d3d12core.dll"),
            wb?"ScarlettWbPagesHeapV1":"ScarlettWcHeapV1");
    if (!open) return DXGI_ERROR_UNSUPPORTED;
    ID3D12Heap *heap=nullptr;
    HRESULT hr=open(d,bytes,input,nullptr,&heap);
    if (FAILED(hr)) return hr;
    auto h=heap->GetDesc();
    if (h.SizeInBytes!=bytes || h.Flags!=D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS ||
            h.Properties.Type!=D3D12_HEAP_TYPE_CUSTOM || h.Properties.MemoryPoolPreference!=D3D12_MEMORY_POOL_L0 ||
            h.Properties.CPUPageProperty!=(wb?D3D12_CPU_PAGE_PROPERTY_WRITE_BACK:D3D12_CPU_PAGE_PROPERTY_WRITE_COMBINE) ||
            h.Properties.CreationNodeMask!=1 || h.Properties.VisibleNodeMask!=1)
        hr=DXGI_ERROR_UNSUPPORTED;
    else {
        D3D12_RESOURCE_DESC r={}; r.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;
        r.Width=bytes; r.Height=r.DepthOrArraySize=r.MipLevels=r.SampleDesc.Count=1;
        r.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR; r.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        hr=d->CreatePlacedResource(heap,0,&r,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(out));
    }
    heap->Release();
    return hr;
}
