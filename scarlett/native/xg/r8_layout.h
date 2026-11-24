#pragma once
#include <d3d12.h>

enum { R8_LAYOUT=0x105, R8_ALIGNMENT=4096, R8_BYTES=256*128 };

static inline BOOL runtime_texture(const D3D12_RESOURCE_DESC *d)
{
    return d->Dimension==D3D12_RESOURCE_DIMENSION_TEXTURE2D &&
        d->DepthOrArraySize==1 && d->MipLevels==1 &&
        d->SampleDesc.Count==1 && !d->SampleDesc.Quality && (UINT)d->Flags==0x300000;
}

static inline BOOL is_r8(const D3D12_RESOURCE_DESC *d)
{
    return runtime_texture(d) && d->Width==256 && d->Height==128 && d->Format==DXGI_FORMAT_R8_UNORM;
}
static inline BOOL texel32_format(DXGI_FORMAT format)
{
    return format==DXGI_FORMAT_R8G8B8A8_UNORM || format==DXGI_FORMAT_R8G8B8A8_UINT || format==DXGI_FORMAT_R32_FLOAT;
}
static inline BOOL is_texel32(const D3D12_RESOURCE_DESC *d)
{
    return runtime_texture(d) && d->Width==1 && d->Height==1 && texel32_format(d->Format);
}
