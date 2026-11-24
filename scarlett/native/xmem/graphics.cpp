#include "gpu_buffer.hpp"
#include <winternl.h>
#include <new>

extern "C" HRESULT xmem_pin_code_source(const void *, SIZE_T, HANDLE *, SIZE_T *, SIZE_T *);
struct CodeSource : CodeReadSource {
    ReadSpanBacking backing;
    static HRESULT WINAPI record(CodeReadSource *s, ID3D12GraphicsCommandList *l, ID3D12Resource *dst, UINT64 at) {
        return static_cast<CodeSource *>(s)->backing.copy_to(l,dst,at);
    }
    static void WINAPI drop(CodeReadSource *s) { delete static_cast<CodeSource *>(s); }
    CodeSource() : CodeReadSource{record,drop} {}
};

extern "C" __declspec(dllexport) HRESULT WINAPI ScarlettAcquireCodeReadSourceV1(ID3D12Device3 *d,
        const void *va, SIZE_T bytes, CodeReadSource **out)
{
    if (!out) return E_POINTER;
    *out = nullptr;
    if (!d || !va || !bytes) return E_INVALIDARG;
    scarlett_section_info i = {};
    HRESULT hr = xmem_pin_code_source(va,bytes,&i.section,&i.view_size,&i.offset);
    if (FAILED(hr)) return hr;
    auto s = new (std::nothrow) CodeSource;
    hr = s ? s->backing.bind(d,i,bytes,false) : E_OUTOFMEMORY;
    CloseHandle(i.section);
    if (SUCCEEDED(hr)) *out = s;
    else delete s;
    return hr;
}

static HRESULT validate_import(void *base, SIZE_T size, void **wc_result, HANDLE *wc_backing = nullptr, bool control = false, bool wb = false)
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
        if (!wc_result && !wc_backing) {
            SIZE_T offset;
            hr = probe.acquire(device, base, size, &offset);
        } else {
            HANDLE h = nullptr;
            hr = probe.wc(device,size,nullptr,&h,control,wb,wc_backing && wb);
            ImportedBuffer rebound;
            if (SUCCEEDED(hr)) hr = rebound.wc(device,size,h,nullptr,control,wb,wc_backing && wb);
            scarlett_section_info info;
            if (SUCCEEDED(hr) && wc_backing) {
                *wc_backing = h;
                h = nullptr;
            } else if (SUCCEEDED(hr)) {
                hr = scarlett_query_section_handle(control ? 0x53430008 : wb ? 0x5343000b : 0x53430004,base,size,&info,h);
                if (SUCCEEDED(hr)) *wc_result = info.base;
            }
            if (h) CloseHandle(h);
        }
        device->Release();
    }
    FreeLibrary(d3d);
    return hr;
}

static void set_graphics_error(HRESULT hr)
{
    DWORD error = ERROR_NOT_SUPPORTED;
    if (hr & FACILITY_NT_BIT) error = RtlNtStatusToDosError(hr & ~FACILITY_NT_BIT);
    else if (HRESULT_FACILITY(hr) == FACILITY_WIN32) error = HRESULT_CODE(hr);
    SetLastError(error);
}

extern "C" BOOL xmem_wc_slot(void *addr, SIZE_T size, HANDLE backing, SIZE_T offset, DWORD policy)
{
    scarlett_section_info info = {size, nullptr, 0, offset, backing};
    ULONG op = policy == 0x2004 ? 0x5343000d : policy == 0x1004 ? 0x5343000e :
               policy == 0x1404 ? 0x5343000f : policy == 0x8404 ? 0x53430012 : 0x53430006;
    HRESULT hr = scarlett_section_call(backing ? op : 0x53430007, addr, &info);
    if (FAILED(hr)) set_graphics_error(hr);
    return SUCCEEDED(hr);
}

extern "C" HANDLE xmem_page_backing(SIZE_T size, BOOL wb)
{
    HANDLE h = nullptr;
    HRESULT hr = validate_import(nullptr, size, nullptr, &h, false, wb);
    if (FAILED(hr)) set_graphics_error(hr);
    return h;
}

extern "C" void *xmem_graphics_alloc(void *base, SIZE_T size, BOOL wc)
{
    scarlett_section_info info;
    HRESULT hr;
    if (wc) {
        void *result = nullptr;
        hr = validate_import(base,size,&result);
        if (SUCCEEDED(hr)) return result;
    } else {
        void *result = nullptr;
        if ((UINT_PTR)base & ((2u << 20) - 1)) {
            SetLastError(ERROR_INVALID_PARAMETER);
            return nullptr;
        }
        hr = validate_import(base,size,&result,nullptr,false,true);
        if (SUCCEEDED(hr)) return result;
        if (hr != E_NOINTERFACE) { set_graphics_error(hr); return nullptr; }
        if (SUCCEEDED(hr = scarlett_query_section(size == 0x23300000 ? 0x53430003 : 0x53430001,
                                                      base, size, &info))) {
            hr = validate_import(info.base, size, nullptr);
            if (SUCCEEDED(hr)) return info.base;
            VirtualFree(info.base, 0, MEM_RELEASE);
        }
    }
    set_graphics_error(hr);
    return nullptr;
}

extern "C" void *xmem_control64_alloc(void)
{
    void *result = nullptr;
    HRESULT hr = validate_import(nullptr,65536,&result,nullptr,true);
    if (FAILED(hr)) set_graphics_error(hr);
    return result;
}
