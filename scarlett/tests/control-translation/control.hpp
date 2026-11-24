// Test-only: synthetic Wine control creator. No XMem export or runtime enable switch.
#pragma once
#include "gpu_buffer.hpp"
#include <memory>
#include <wrl/client.h>

struct Control {
    static constexpr SIZE_T extent=65536, bank_bytes=3*18*sizeof(UINT32);
    using Ptr=std::shared_ptr<Control>;
    Control(const Control &)=delete;
    ~Control() { if (identity) CloseHandle(identity); }

    // Scratch dispatch boundary. The creator capability is only present in the synthetic Wine tool.
    static HRESULT request(ID3D12Device3 *d,void *requested,SIZE_T bytes,DWORD type,UINT64 flags,DWORD protect,
                           SIZE_T cell,SIZE_T banks,Ptr &out) {
        out.reset();
        if (requested || bytes!=extent || type!=0x20401000 || flags!=0x283 || protect!=0x42404)
            return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
        return create(d,nullptr,cell,banks,out);
    }

    // Trusted role offsets supplied by the test allocator, never inferred from a caller VA.
    static HRESULT create(ID3D12Device3 *d, void *at, SIZE_T cell, SIZE_T banks, Ptr &out) {
        out.reset();
        if (!d || cell%8 || banks%4 || cell>extent-8 || banks>extent-bank_bytes ||
            !(cell+8<=banks || banks+bank_bytes<=cell)) return E_INVALIDARG;
        D3D12_FEATURE_DATA_D3D12_OPTIONS3 f={};
        HRESULT hr=d->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS3,&f,sizeof f);
        if (FAILED(hr)) return hr;
        if (!(f.WriteBufferImmediateSupportFlags & D3D12_COMMAND_LIST_SUPPORT_FLAG_DIRECT)) return E_NOTIMPL;
        Ptr p(new Control);
        p->compare=(Compare)(void *)GetProcAddress(GetModuleHandleW(L"kernelbase.dll"),"CompareObjectHandles");
        if (!p->compare) return E_NOTIMPL;
        hr=d->QueryInterface(IID_PPV_ARGS(&p->device));
        if (FAILED(hr)) return hr;
        scarlett_section_info i;
        hr=scarlett_query_section(0x5354ff01,at,extent,&i); // release agent's test-only mark
        if (FAILED(hr)) return hr;
        p->base=(BYTE *)i.base;
        hr=scarlett_acquire_section(p->base,extent,&i);
        if (SUCCEEDED(hr)) {
            p->identity=i.section;
            hr=i.base==p->base && i.view_size==extent && i.offset==0 ?
                p->buffer.create(d,i.section,extent) : E_UNEXPECTED;
        }
        if (FAILED(hr)) { VirtualFree(p->base,0,MEM_RELEASE); return hr; }
        p->cell=cell; p->banks=banks; out=std::move(p);
        return S_OK;
    }
    BYTE *public_base() const { return base; } // caller owns release of public mapping

    HRESULT record_wait(const void *a, UINT comparison, UINT flags, const UINT64 **retained) const {
        if (!retained) return E_POINTER;
        *retained=nullptr;
        if (comparison!=7 || flags!=1) return E_NOTIMPL;
        const SIZE_T off=(UINT_PTR)a-(UINT_PTR)base;
        if (off!=cell) return E_INVALIDARG;
        HRESULT hr=validate(a,8,off);
        if (SUCCEEDED(hr)) *retained=(UINT64 *)((BYTE *)buffer.cpu()+off);
        return hr; // valid while recorder holds Ptr, even if public VA is released
    }
    HRESULT marker(ID3D12GraphicsCommandList2 *list,const void *a,UINT32 value,UINT64 arg4,UINT64 arg5) const {
        if (!list) return E_POINTER;
        if (arg4 || arg5 || list->GetType()!=D3D12_COMMAND_LIST_TYPE_DIRECT) return E_NOTIMPL;
        const SIZE_T off=(UINT_PTR)a-(UINT_PTR)base;
        if (off%4 || off<banks || off-banks>bank_bytes-4) return E_INVALIDARG;
        Microsoft::WRL::ComPtr<IUnknown> owner;
        HRESULT hr=list->GetDevice(IID_PPV_ARGS(&owner));
        if (FAILED(hr)) return hr;
        if (owner.Get()!=device.Get()) return E_INVALIDARG;
        hr=validate(a,4,off); if (FAILED(hr)) return hr;
        auto dst=buffer.translate((BYTE *)buffer.cpu()+off,4);
        if (!dst || dst==(UINT_PTR)a) return E_FAIL;
        D3D12_WRITEBUFFERIMMEDIATE_PARAMETER w={dst,value};
        list->WriteBufferImmediate(1,&w,nullptr); // synthesis for this experiment; stage unproven
        return S_OK;
    }
private:
    Control()=default;
    using Compare=BOOL(WINAPI *)(HANDLE,HANDLE);
    Compare compare=nullptr;
    HANDLE identity=nullptr;
    ImportedBuffer buffer;
    Microsoft::WRL::ComPtr<IUnknown> device;
    BYTE *base=nullptr;
    SIZE_T cell=0,banks=0;
    HRESULT validate(const void *a,SIZE_T n,SIZE_T off) const {
        scarlett_section_info i;
        HRESULT hr=scarlett_acquire_section(a,n,&i); if (FAILED(hr)) return hr;
        bool same=i.base==base && i.offset==off && i.view_size==extent && compare(identity,i.section);
        CloseHandle(i.section);
        return same?S_OK:E_INVALIDARG;
    }
};
