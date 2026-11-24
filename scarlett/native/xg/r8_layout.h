#pragma once
#include <d3d12.h>

enum { R8_LAYOUT=0x105, R8_ALIGNMENT=4096, R8_BYTES=256*128 };

static inline BOOL runtime_texture(const D3D12_RESOURCE_DESC *d)
{
    return d->DepthOrArraySize==1 && d->MipLevels==1 &&
        d->SampleDesc.Count==1 && !d->SampleDesc.Quality && (UINT)d->Flags==0x300000;
}

static inline BOOL is_r8(const D3D12_RESOURCE_DESC *d)
{
    return d->Dimension==D3D12_RESOURCE_DIMENSION_TEXTURE2D && runtime_texture(d) && d->Width==256 && d->Height==128 && d->Format==DXGI_FORMAT_R8_UNORM;
}
static inline BOOL texel32_format(DXGI_FORMAT format)
{
    return format==DXGI_FORMAT_R8G8B8A8_UNORM || format==DXGI_FORMAT_R8G8B8A8_UINT || format==DXGI_FORMAT_R32_FLOAT;
}
static inline BOOL is_volume1(const D3D12_RESOURCE_DESC *d)
{
    return d->Dimension==D3D12_RESOURCE_DIMENSION_TEXTURE3D && runtime_texture(d) &&
        d->Width==1 && d->Height==1 && d->Format==DXGI_FORMAT_R8G8B8A8_UNORM;
}
static inline BOOL is_texel32(const D3D12_RESOURCE_DESC *d)
{
    return is_volume1(d) || (d->Dimension==D3D12_RESOURCE_DIMENSION_TEXTURE2D && runtime_texture(d) &&
        d->Width==1 && d->Height==1 && texel32_format(d->Format));
}

/* Independent 2D array slices, one 4KB_S tile per face at this extent. */
enum { CUBE1_FACES=6, CUBE_PAIR_FACES=2*CUBE1_FACES };
static inline BOOL is_layered_texel(const D3D12_RESOURCE_DESC *d)
{
    return d->Dimension==D3D12_RESOURCE_DIMENSION_TEXTURE2D && d->Width==1 && d->Height==1 &&
        (d->DepthOrArraySize==2 || d->DepthOrArraySize==CUBE1_FACES || d->DepthOrArraySize==CUBE_PAIR_FACES) && d->MipLevels==1 && d->Format==DXGI_FORMAT_R8G8B8A8_UNORM &&
        d->SampleDesc.Count==1 && !d->SampleDesc.Quality && (UINT)d->Flags==0x300000;
}
