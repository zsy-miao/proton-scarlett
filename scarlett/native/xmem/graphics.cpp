#include "gpu_buffer.hpp"
#include <winternl.h>

static HRESULT validate_import(void *base, SIZE_T size, void **wc_result)
{
    // Resolve only for graphics requests: CPU allocations do not require D3D12.
    HMODULE d3d = LoadLibraryExW(L"d3d12.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!d3d) return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
    auto create = (PFN_D3D12_CREATE_DEVICE)(void *)GetProcAddress(d3d, "D3D12CreateDevice");
    ID3D12Device3 *device = nullptr;
    HRESULT hr = create ? create(nullptr, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device))
                        : HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
    if (SUCCEEDED(hr)) {
        ImportedBuffer probe;
        if (!wc_result) {
            SIZE_T offset;
            hr = probe.acquire(device, base, size, &offset);
        } else {
            HANDLE h = nullptr;
            hr = probe.wc(device,size,nullptr,&h);
            ImportedBuffer rebound;
            if (SUCCEEDED(hr)) hr = rebound.wc(device,size,h,nullptr);
            scarlett_section_info info;
            if (SUCCEEDED(hr)) hr = scarlett_query_section_handle(0x53430004,base,size,&info,h);
            if (SUCCEEDED(hr)) *wc_result = info.base;
            if (h) CloseHandle(h);
        }
        device->Release();
    }
    FreeLibrary(d3d);
    return hr;
}

extern "C" void *xmem_graphics_alloc(void *base, SIZE_T size, BOOL wc)
{
    scarlett_section_info info;
    HRESULT hr;
    if (wc) {
        void *result = nullptr;
        hr = validate_import(base,size,&result);
        if (SUCCEEDED(hr)) return result;
    } else if (SUCCEEDED(hr = scarlett_query_section(0x53430001, base, size, &info))) {
        hr = validate_import(info.base, size, nullptr);
        if (SUCCEEDED(hr)) return info.base;
        VirtualFree(info.base, 0, MEM_RELEASE);
    }
    DWORD error = ERROR_NOT_SUPPORTED;
    if (hr & FACILITY_NT_BIT) error = RtlNtStatusToDosError(hr & ~FACILITY_NT_BIT);
    else if (HRESULT_FACILITY(hr) == FACILITY_WIN32) error = HRESULT_CODE(hr);
    SetLastError(error);
    return nullptr;
}
