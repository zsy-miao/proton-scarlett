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
        const SIZE_T alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
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
        if (SUCCEEDED(hr)) hr = create(device, info.section, info.view_size);
        else {
            hr = scarlett_query_section(0x53430005, address, bytes, &info);
            bool control = false, wb = false;
            if (FAILED(hr)) {
                hr = scarlett_query_section(0x5343000c,address,bytes,&info);
                wb = SUCCEEDED(hr);
                if (!wb) {
                    hr = scarlett_query_section(0x5343000a,address,bytes,&info);
                    if (FAILED(hr)) return hr;
                    control = true;
                }
            }
            hr = wc(device, info.view_size, info.section, nullptr, control, wb, wb && (info.view_size == 0xa00000 || info.view_size == 0x6400000));
        }
        CloseHandle(info.section);
        if (SUCCEEDED(hr)) *offset = info.offset;
        return hr;
    }

    HRESULT wc(ID3D12Device3 *device, SIZE_T size, HANDLE input, HANDLE *output, bool control = false, bool wb = false, bool pages = false) {
        if (!device || heap || base) return E_INVALIDARG;
        using Fn = HRESULT (WINAPI *)(ID3D12Device3 *,SIZE_T,HANDLE,HANDLE *,ID3D12Heap **);
        auto fn = (Fn)(void *)GetProcAddress(GetModuleHandleW(L"d3d12core.dll"),control ? "ScarlettControlHeapV1" : pages && wb ? "ScarlettWbPagesHeapV1" : wb ? "ScarlettWbHeapV1" : "ScarlettWcHeapV1");
        if (!fn) return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
        HRESULT hr = fn(device,size,input,output,&heap);
        if (FAILED(hr)) return hr;
        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = size;
        desc.Height = desc.DepthOrArraySize = desc.MipLevels = desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        hr = device->CreatePlacedResource(heap,0,&desc,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&buffer));
        if (SUCCEEDED(hr) && !buffer->GetGPUVirtualAddress()) hr = E_FAIL;
        if (SUCCEEDED(hr) && wb) {
            scarlett_section_info info;
            hr = scarlett_query_section_handle(0x5343000b,nullptr,size,&info,input ? input : *output);
            if (SUCCEEDED(hr)) { base = info.base; wb_mapped = true; }
        } else if (SUCCEEDED(hr)) hr = buffer->Map(0,nullptr,&base);
        if (FAILED(hr)) { reset(); return hr; }
        wc_mapped = !wb;
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
    void reset() {
        if (wc_mapped) { buffer->Unmap(0,nullptr); wc_mapped = false; base = nullptr; }
        if (buffer) { buffer->Release(); buffer = nullptr; }
        if (heap) { heap->Release(); heap = nullptr; }
        if (base) {
            if (wb_mapped) VirtualFree(base,0,MEM_RELEASE);
            else UnmapViewOfFile(base);
            base = nullptr; wb_mapped = false;
        }
    }
    void *base = nullptr;
    ID3D12Heap *heap = nullptr;
    ID3D12Resource *buffer = nullptr;
    SIZE_T length = 0;
    bool wc_mapped = false;
    bool wb_mapped = false;
};

/* Restricted imports are usable only as copy sources. No writable resource or raw GPU VA escapes. */
class ImportedReadSpan {
public:
    HRESULT acquire(ID3D12Device3 *d, const void *va, SIZE_T bytes) {
        scarlett_section_info i;
        bool wb = SUCCEEDED(scarlett_query_section(0x53430010,va,bytes,&i));
        HRESULT hr = wb ? S_OK : scarlett_query_section(0x53430011,va,bytes,&i);
        if (FAILED(hr)) return hr;
        hr = backing.wc(d,i.view_size,i.section,nullptr,false,wb,true);
        CloseHandle(i.section);
        if (SUCCEEDED(hr)) { offset=i.offset; length=bytes; }
        return hr;
    }
    HRESULT copy_to(ID3D12GraphicsCommandList *list, ID3D12Resource *dst, UINT64 at) const {
        if (!list || !dst || !length) return E_INVALIDARG;
        auto desc=dst->GetDesc();
        if (desc.Dimension!=D3D12_RESOURCE_DIMENSION_BUFFER || at>desc.Width || length>desc.Width-at) return E_INVALIDARG;
        list->CopyBufferRegion(dst,at,backing.resource(),offset,length);
        return S_OK;
    }
private:
    ImportedBuffer backing;
    SIZE_T offset=0, length=0;
};
