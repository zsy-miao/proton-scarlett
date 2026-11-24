#pragma once
#include <windows.h>
#include <d3d12.h>

/* Private host consumer. approved is immutable, trusted host-compiled compute
 * bytecode from the translator, never a pointer or whitelist supplied by the title. */
static inline HRESULT xs_code_pipeline(ID3D12Device *device, ID3D12RootSignature *root,
        const void *title, D3D12_SHADER_BYTECODE approved, ID3D12PipelineState **out)
{
    if (!out) return E_POINTER;
    *out = nullptr;
    const SIZE_T n = approved.BytecodeLength;
    if (!device || !root || !title || !approved.pShaderBytecode || !n || n > 65536) return E_INVALIDARG;
    using Query = LONG (WINAPI *)(HANDLE,const void *,ULONG,void *,SIZE_T,SIZE_T *);
    auto snapshot = (Query)(void *)GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"NtQueryVirtualMemory");
    if (!snapshot) return E_NOTIMPL;
    void *copy = HeapAlloc(GetProcessHeap(),0,n);
    if (!copy) return E_OUTOFMEMORY;
    LONG status = snapshot(GetCurrentProcess(),title,0x53430013,copy,n,nullptr);
    HRESULT hr = status < 0 ? HRESULT_FROM_NT(status) : E_NOTIMPL;
    if (!status && !memcmp(copy,approved.pShaderBytecode,n)) {
        D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {};
        desc.pRootSignature = root;
        desc.CS = {copy,n};
        hr = device->CreateComputePipelineState(&desc,IID_PPV_ARGS(out));
    }
    HeapFree(GetProcessHeap(),0,copy);
    return hr;
}
