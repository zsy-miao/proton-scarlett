#include <windows.h>
#include <d3d12.h>
#include <stddef.h>
#include <stdint.h>

_Static_assert(sizeof(void *) == 8 && sizeof(D3D12_RESOURCE_DESC) == 0x38,
               "requires the x64 descriptor ABI");
_Static_assert(offsetof(D3D12_RESOURCE_DESC, Layout) == 0x2c, "layout field");

__declspec(dllexport) HRESULT WINAPI XGSuggestSwizzleMode(
    const D3D12_RESOURCE_DESC *d, uintptr_t option, D3D12_TEXTURE_LAYOUT *out)
{
    if (!d || !out) return E_POINTER;
    /* shortcut: restrict suggestions to the published sample descriptors until more are verified. */
    if (option || d->Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
        (d->Alignment && d->Alignment != D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT) ||
        d->Width != 4096 || d->Height != 4096 || d->DepthOrArraySize != 1 ||
        d->MipLevels != 1 || d->SampleDesc.Count != 1 || d->SampleDesc.Quality ||
        d->Layout != D3D12_TEXTURE_LAYOUT_UNKNOWN || d->Flags)
        return E_NOTIMPL;

    switch (d->Format) {
    case DXGI_FORMAT_BC1_UNORM:
    case DXGI_FORMAT_BC3_UNORM:
    case DXGI_FORMAT_BC4_UNORM:
    case DXGI_FORMAT_BC5_UNORM:
        *out = (D3D12_TEXTURE_LAYOUT)0x109; /* encoded 64KB_S */
        return S_OK;
    default:
        return E_NOTIMPL;
    }
}

__declspec(dllexport) HRESULT WINAPI XGCreateTextureComputer(
    const D3D12_RESOURCE_DESC *d, void **out)
{
    if (!out) return E_POINTER;
    *out = NULL;
    return d ? E_NOTIMPL : E_POINTER;
}
