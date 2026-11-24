#pragma once
#include <d3d12.h>

enum { R8_LAYOUT=0x105, R8_ALIGNMENT=4096, R8_BYTES=256*128 };

static inline BOOL is_r8(const D3D12_RESOURCE_DESC *d)
{
    return d->Dimension==D3D12_RESOURCE_DIMENSION_TEXTURE2D && d->Width==256 && d->Height==128 &&
        d->DepthOrArraySize==1 && d->MipLevels==1 && d->Format==DXGI_FORMAT_R8_UNORM &&
        d->SampleDesc.Count==1 && !d->SampleDesc.Quality && (UINT)d->Flags==0x300000;
}
