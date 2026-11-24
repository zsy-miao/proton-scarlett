// Private desktop-D3D12 prototype, not an xmem.dll ABI or an Xbox heap.
#pragma once
#include <windows.h>
#include <d3d12.h>

class ImportedBuffer {
public:
    ImportedBuffer() = default;
    ImportedBuffer(const ImportedBuffer &) = delete;
    ImportedBuffer &operator=(const ImportedBuffer &) = delete;
    ~ImportedBuffer() {
        // The owner must retire GPU work and release any other views first.
        if (buffer) buffer->Release();
        if (heap) heap->Release();
        if (base) VirtualFree(base, 0, MEM_RELEASE);
    }

    HRESULT create(ID3D12Device3 *device, SIZE_T size) {
        const SIZE_T alignment = 2u << 20;
        if (!device || base || !size || size % alignment) return E_INVALIDARG;
        using Alloc2 = void *(WINAPI *)(HANDLE, void *, SIZE_T, ULONG, ULONG, MEM_EXTENDED_PARAMETER *, ULONG);
        auto reserve = (Alloc2)(void *)GetProcAddress(GetModuleHandleW(L"kernelbase.dll"), "VirtualAlloc2");
        if (!reserve) return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
        MEM_ADDRESS_REQUIREMENTS limits = {};
        limits.Alignment = alignment;
        MEM_EXTENDED_PARAMETER param = {};
        param.Type = MemExtendedParameterAddressRequirements;
        param.Pointer = &limits;
        base = reserve(nullptr, nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE, &param, 1);
        if (!base) return HRESULT_FROM_WIN32(GetLastError());
        HRESULT hr = device->OpenExistingHeapFromAddress(base, IID_PPV_ARGS(&heap));
        if (FAILED(hr)) return hr;
        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = size;
        desc.Height = desc.DepthOrArraySize = desc.MipLevels = desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_CROSS_ADAPTER;
        hr = device->CreatePlacedResource(heap, 0, &desc, D3D12_RESOURCE_STATE_COMMON,
                                          nullptr, IID_PPV_ARGS(&buffer));
        if (FAILED(hr)) return hr;
        length = size;
        return S_OK;
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
    void *base = nullptr;
    ID3D12Heap *heap = nullptr;
    ID3D12Resource *buffer = nullptr;
    SIZE_T length = 0;
};
