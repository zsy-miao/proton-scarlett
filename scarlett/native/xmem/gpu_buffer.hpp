// Private desktop-D3D12 binding, not an xmem.dll ABI or an Xbox heap.
#pragma once
#include <windows.h>
#include <d3d12.h>
#include "marked_section.h"

using Alloc2 = void *(WINAPI *)(HANDLE, void *, SIZE_T, ULONG, ULONG, MEM_EXTENDED_PARAMETER *, ULONG);
using Map3 = void *(WINAPI *)(HANDLE, HANDLE, void *, ULONG64, SIZE_T, ULONG, ULONG, MEM_EXTENDED_PARAMETER *, ULONG);

static void *map_aligned_section(HANDLE section, SIZE_T size)
{
    HMODULE kernel = GetModuleHandleW(L"kernelbase.dll");
    auto reserve = (Alloc2)(void *)GetProcAddress(kernel, "VirtualAlloc2");
    auto map = (Map3)(void *)GetProcAddress(kernel, "MapViewOfFile3");
    if (!reserve || !map) { SetLastError(ERROR_NOT_SUPPORTED); return nullptr; }
    MEM_ADDRESS_REQUIREMENTS limits = {};
    limits.Alignment = 2u << 20;
    MEM_EXTENDED_PARAMETER param = {};
    param.Type = MemExtendedParameterAddressRequirements;
    param.Pointer = &limits;
    void *p = reserve(nullptr, nullptr, size, MEM_RESERVE | MEM_RESERVE_PLACEHOLDER,
                      PAGE_NOACCESS, &param, 1);
    if (!p) return nullptr;
    if (map(section, GetCurrentProcess(), p, 0, size, MEM_REPLACE_PLACEHOLDER,
            PAGE_READWRITE, nullptr, 0)) return p;
    DWORD err = GetLastError();
    VirtualFree(p, 0, MEM_RELEASE);
    SetLastError(err);
    return nullptr;
}

class ImportedBuffer {
public:
    ImportedBuffer() = default;
    ImportedBuffer(const ImportedBuffer &) = delete;
    ImportedBuffer &operator=(const ImportedBuffer &) = delete;
    ~ImportedBuffer() { reset(); } // The owner must first retire every consuming queue.

    HRESULT create(ID3D12Device3 *device, HANDLE section, SIZE_T size) {
        const SIZE_T alignment = 2u << 20;
        if (!device || !section || base || !size || size % alignment) return E_INVALIDARG;
        base = map_aligned_section(section, size);
        if (!base) return HRESULT_FROM_WIN32(GetLastError());
        HRESULT hr = device->OpenExistingHeapFromAddress(base, IID_PPV_ARGS(&heap));
        if (FAILED(hr)) { reset(); return hr; }
        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = size;
        desc.Height = desc.DepthOrArraySize = desc.MipLevels = desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_CROSS_ADAPTER;
        hr = device->CreatePlacedResource(heap, 0, &desc, D3D12_RESOURCE_STATE_COMMON,
                                          nullptr, IID_PPV_ARGS(&buffer));
        if (FAILED(hr)) { reset(); return hr; }
        if (!buffer->GetGPUVirtualAddress()) { reset(); return E_FAIL; }
        length = size;
        return S_OK;
    }

    HRESULT acquire(ID3D12Device3 *device, const void *address, SIZE_T bytes, SIZE_T *offset) {
        if (!offset) return E_POINTER;
        *offset = 0;
        scarlett_section_info info;
        HRESULT hr = scarlett_acquire_section(address, bytes, &info);
        if (FAILED(hr)) return hr;
        hr = create(device, info.section, info.view_size);
        CloseHandle(info.section);
        if (SUCCEEDED(hr)) *offset = info.offset;
        return hr;
    }

    void *cpu() const { return buffer ? base : nullptr; }
    ID3D12Resource *resource() const { return buffer; } // borrowed
    D3D12_GPU_VIRTUAL_ADDRESS translate(const void *address, SIZE_T bytes) const {
        UINT_PTR offset = (UINT_PTR)address - (UINT_PTR)base;
        if (!buffer || !bytes || offset >= length || bytes > length - offset) return 0;
        auto gpu = buffer->GetGPUVirtualAddress();
        return gpu ? gpu + offset : 0;
    }

private:
    void reset() {
        if (buffer) { buffer->Release(); buffer = nullptr; }
        if (heap) { heap->Release(); heap = nullptr; }
        if (base) { UnmapViewOfFile(base); base = nullptr; }
    }
    void *base = nullptr;
    ID3D12Heap *heap = nullptr;
    ID3D12Resource *buffer = nullptr;
    SIZE_T length = 0;
};
