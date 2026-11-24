#pragma once
#include <d3d12.h>

// Private host table, PS29 only. Call after resolving owned live descriptors;
// the native table's byte offsets are not desktop heap indices.
namespace im_ui_sdr {
inline constexpr D3D12_DESCRIPTOR_RANGE ranges[]={
    {D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,257,1,0},
    {D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,274,1,1},
    {D3D12_DESCRIPTOR_RANGE_TYPE_CBV,2,26,1,2}
};
inline D3D12_STATIC_SAMPLER_DESC sampler(){
    D3D12_STATIC_SAMPLER_DESC s={};s.Filter=D3D12_FILTER_MIN_MAG_MIP_POINT;
    s.AddressU=s.AddressV=s.AddressW=D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    s.MaxAnisotropy=1;s.ComparisonFunc=D3D12_COMPARISON_FUNC_ALWAYS;s.MaxLOD=12;
    s.RegisterSpace=1;s.ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
    return s;
}
struct Views {D3D12_CPU_DESCRIPTOR_HANDLE ui, noise, display, pass;};
// Sources must be same-device non-shader-visible CBV/SRV/UAV heap descriptors.
// Destination is a fresh 4-descriptor allocation, held through execution.
inline HRESULT populate(ID3D12Device *d,D3D12_CPU_DESCRIPTOR_HANDLE start,UINT capacity,Views v){
    if(!d||!start.ptr||capacity<4||!v.ui.ptr||!v.noise.ptr||!v.display.ptr||!v.pass.ptr)return E_INVALIDARG;
    D3D12_CPU_DESCRIPTOR_HANDLE from[]={v.ui,v.noise,v.display,v.pass};
    UINT four=4;d->CopyDescriptors(1,&start,&four,4,from,nullptr,D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);return S_OK;
}
}
