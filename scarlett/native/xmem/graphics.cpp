#include "gpu_buffer.hpp"
#include "placed_capture.h"
#include "placed_table.h"
#include "arena_data_uav.hpp"
#include <new>
#include <winternl.h>
#include <wincrypt.h>

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

extern "C" HRESULT xmem_pin_placed(const void *base, UINT64 width, HANDLE parts[3], ULONG_PTR *generation);

struct XsPlacedCapture {
    LONG refs = 1;
    ULONG_PTR generation;
    IUnknown *owner = nullptr;
    ImportedBuffer code;
    ID3D12Resource *data[2]{};
    ID3D12Resource *resource(unsigned i) const { return i ? data[i-1] : code.resource(); }
    ~XsPlacedCapture() {
        for (auto *r : data) if (r) r->Release();
        if (owner) owner->Release();
    }
};
static constexpr UINT64 edges[] = {0,27ull<<20,74ull<<20,174ull<<20};

extern "C" __declspec(dllexport) HRESULT WINAPI ScarlettCapturePlacedV1(ID3D12Device3 *device,
        const void *base, UINT64 width, XsPlacedCapture **out)
{
    if (!out) return E_POINTER;
    *out = nullptr;
    if (!device) return E_INVALIDARG;
    HANDLE parts[3];
    ULONG_PTR generation;
    HRESULT hr = xmem_pin_placed(base,width,parts,&generation);
    if (FAILED(hr)) return hr;
    auto *p = new(std::nothrow) XsPlacedCapture;
    if (p) {
        p->generation=generation;
        hr = device->QueryInterface(IID_PPV_ARGS(&p->owner));
        if (SUCCEEDED(hr)) hr = p->code.wc(device,edges[1],parts[0],nullptr);
        if (SUCCEEDED(hr)) hr = xs_place_arena_data_uav(device,parts[1],0x2404,47u<<20,&p->data[0]);
        if (SUCCEEDED(hr)) hr = xs_place_arena_data_uav(device,parts[2],0x2004,100u<<20,&p->data[1]);
        if (FAILED(hr)) delete p;
        else *out = p;
    } else hr = E_OUTOFMEMORY;
    for (auto h:parts) CloseHandle(h);
    return hr;
}

extern "C" __declspec(dllexport) BOOL WINAPI ScarlettPlacedMatchesV1(const XsPlacedCapture *p,const void *base)
{
    ULONG_PTR live;
    return p && SUCCEEDED(xmem_pin_placed(base,edges[3],nullptr,&live)) && p->generation==live;
}

extern "C" __declspec(dllexport) HRESULT WINAPI ScarlettPlacedDataV1(const XsPlacedCapture *p,
        UINT64 at, UINT64 bytes, XsDataOperand *out)
{
    if (!out) return E_POINTER;
    *out = {};
    if (!p || !bytes || at < edges[1] || at >= edges[3]) return E_INVALIDARG;
    unsigned i = at < edges[2] ? 1 : 2;
    if (bytes > edges[i+1]-at) return E_INVALIDARG;
    *out = {p->resource(i),at-edges[i],bytes};
    return S_OK;
}

static bool on_device(ID3D12DeviceChild *child, IUnknown *owner)
{
    IUnknown *id;
    if (FAILED(child->GetDevice(IID_PPV_ARGS(&id)))) return false;
    bool same = id == owner;
    id->Release();
    return same;
}

extern "C" __declspec(dllexport) HRESULT WINAPI ScarlettPlacedCopyToV1(XsPlacedCapture *p,
        UINT64 at, UINT64 bytes, ID3D12GraphicsCommandList *list, ID3D12Resource *dst,
        UINT64 to, XsPlacedCapture **retained)
{
    if (!retained) return E_POINTER;
    *retained = nullptr;
    if (!p || !list || !dst || !bytes || at >= edges[3] || bytes > edges[3]-at ||
        !on_device(list,p->owner) || !on_device(dst,p->owner)) return E_INVALIDARG;
    auto type = list->GetType();
    if (type != D3D12_COMMAND_LIST_TYPE_COPY && type != D3D12_COMMAND_LIST_TYPE_COMPUTE &&
        type != D3D12_COMMAND_LIST_TYPE_DIRECT) return E_INVALIDARG;
    auto desc = dst->GetDesc();
    if (desc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER || to > desc.Width || bytes > desc.Width-to)
        return E_INVALIDARG;
    for (unsigned i=0; i<3; ++i) if (dst == p->resource(i)) return E_INVALIDARG;

    // All checks precede the first command: a crossing copy cannot leave a partial recording on error.
    InterlockedIncrement(&p->refs);
    *retained = p;
    for (unsigned i=0; bytes; ++i) {
        if (at >= edges[i+1]) continue;
        UINT64 n = bytes < edges[i+1]-at ? bytes : edges[i+1]-at;
        list->CopyBufferRegion(dst,to,p->resource(i),at-edges[i],n);
        bytes -= n; at += n; to += n;
    }
    return S_OK;
}

extern "C" __declspec(dllexport) void WINAPI ScarlettReleasePlacedV1(XsPlacedCapture *p)
{
    if (p && !InterlockedDecrement(&p->refs)) delete p;
}

struct XsPlacedTable {
    XsPlacedCapture *capture;
    ID3D12DescriptorHeap *heap;
};

extern "C" __declspec(dllexport) HRESULT WINAPI ScarlettCreatePlacedTableV1(XsPlacedCapture *p, XsPlacedTable **out)
{
    if (!out) return E_POINTER;
    *out=nullptr;
    if (!p) return E_INVALIDARG;
    for (unsigned i=0;i<3;i++) {
        auto b=p->resource(i)->GetDesc();
        if (b.Dimension!=D3D12_RESOURCE_DIMENSION_BUFFER || b.Width!=edges[i+1]-edges[i] ||
                b.Flags!=(i?D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS:D3D12_RESOURCE_FLAG_NONE)) return E_NOTIMPL;
    }
    ID3D12Device *d;
    HRESULT hr=p->owner->QueryInterface(IID_PPV_ARGS(&d));
    if (FAILED(hr)) return hr;
    auto t=new(std::nothrow) XsPlacedTable{};
    if (!t) { d->Release(); return E_OUTOFMEMORY; }
    D3D12_DESCRIPTOR_HEAP_DESC hd={D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,3,D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,0};
    hr=d->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&t->heap));
    if (SUCCEEDED(hr)) {
        auto cpu=t->heap->GetCPUDescriptorHandleForHeapStart();
        D3D12_SHADER_RESOURCE_VIEW_DESC s={}; s.Format=DXGI_FORMAT_R32_TYPELESS; s.ViewDimension=D3D12_SRV_DIMENSION_BUFFER;
        s.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        s.Buffer.NumElements=UINT(edges[1]/4); s.Buffer.Flags=D3D12_BUFFER_SRV_FLAG_RAW;
        d->CreateShaderResourceView(p->resource(0),&s,cpu);
        for (unsigned i=1;i<3;i++) {
            cpu.ptr+=d->GetDescriptorHandleIncrementSize(hd.Type);
            D3D12_UNORDERED_ACCESS_VIEW_DESC u={}; u.Format=DXGI_FORMAT_R32_TYPELESS; u.ViewDimension=D3D12_UAV_DIMENSION_BUFFER;
            u.Buffer.NumElements=UINT((edges[i+1]-edges[i])/4); u.Buffer.Flags=D3D12_BUFFER_UAV_FLAG_RAW;
            d->CreateUnorderedAccessView(p->resource(i),nullptr,&u,cpu);
        }
        t->capture=p; InterlockedIncrement(&p->refs); *out=t;
    } else delete t;
    d->Release();
    return hr;
}

extern "C" __declspec(dllexport) HRESULT WINAPI ScarlettBindPlacedTableV1(const XsPlacedTable *t, ID3D12GraphicsCommandList *l)
{
    if (!t || !l || !on_device(l,t->capture->owner) ||
            (l->GetType()!=D3D12_COMMAND_LIST_TYPE_COMPUTE && l->GetType()!=D3D12_COMMAND_LIST_TYPE_DIRECT)) return E_INVALIDARG;
    l->SetDescriptorHeaps(1,&t->heap);
    l->SetComputeRootDescriptorTable(0,t->heap->GetGPUDescriptorHandleForHeapStart());
    return S_OK;
}

extern "C" __declspec(dllexport) void WINAPI ScarlettReleasePlacedTableV1(XsPlacedTable *t)
{
    if (!t) return;
    t->heap->Release(); ScarlettReleasePlacedV1(t->capture); delete t;
}

/* Only this executable's pre-PSO control-page accesses have been traced. */
static bool traced_control_title()
{
    static constexpr BYTE expected[] = {0xe9,0x50,0x20,0xb2,0xfa,0x4a,0x72,0x1d,0xb3,0xdf,0x59,0xfd,0xec,0xf7,0x56,0x21,
        0x73,0x0a,0xc5,0x59,0xd8,0x83,0x52,0x52,0xf6,0xc3,0xb6,0x58,0xd5,0xa0,0x89,0x93};
    WCHAR path[1024];
    DWORD length=GetModuleFileNameW(nullptr,path,ARRAYSIZE(path));
    if (!length || length>=ARRAYSIZE(path)) return false;
    HANDLE file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_SEQUENTIAL_SCAN,nullptr);
    if (file==INVALID_HANDLE_VALUE) return false;
    HCRYPTPROV prov=0; HCRYPTHASH hash=0;
    bool ok=CryptAcquireContextW(&prov,nullptr,nullptr,PROV_RSA_AES,CRYPT_VERIFYCONTEXT) &&
            CryptCreateHash(prov,CALG_SHA_256,0,0,&hash);
    BYTE bytes[16384]; DWORD n=0;
    while(ok) {
        ok=ReadFile(file,bytes,sizeof(bytes),&n,nullptr);
        if (!ok || !n) break;
        ok=CryptHashData(hash,bytes,n,0);
    }
    n=sizeof(expected);
    ok=ok && CryptGetHashParam(hash,HP_HASHVAL,bytes,&n,0) && n==sizeof(expected) && !memcmp(bytes,expected,n);
    if (hash) CryptDestroyHash(hash);
    if (prov) CryptReleaseContext(prov,0);
    CloseHandle(file);
    return ok;
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
            if (control && hr == DXGI_ERROR_UNSUPPORTED && wc_result && !base && size == 65536 && traced_control_title()) {
                using Fn = HRESULT (WINAPI *)(ID3D12Device3 *, SIZE_T, void **);
                auto cpu = (Fn)(void *)GetProcAddress(GetModuleHandleW(L"d3d12core.dll"),"ScarlettCpuControlViewV1");
                if (cpu) {
                    hr = cpu(device,size,wc_result);
                    device->Release(); FreeLibrary(d3d);
                    return hr;
                }
            }
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

extern "C" HANDLE xmem_tile_backing(SIZE_T size)
{
    return xmem_page_backing(size,TRUE);
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
