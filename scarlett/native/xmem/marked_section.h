/* Private Wine native-x64 ABI; see ../../wine-patches/README.md. */
#pragma once
#include <windows.h>

struct scarlett_section_info {
    SIZE_T size;
    void *base;
    SIZE_T view_size, offset;
    HANDLE section;
};
C_ASSERT(sizeof(struct scarlett_section_info) == 40);

struct scarlett_wc_view_info {
    struct scarlett_section_info info;
    UINT64 generation;
};
C_ASSERT(sizeof(struct scarlett_wc_view_info) == 48);
/* One Wine VM lookup ties the returned handle and generation to the same live view. */
static inline HRESULT scarlett_acquire_wc_view(const void *address,SIZE_T bytes,struct scarlett_wc_view_info *out)
{
    typedef LONG (WINAPI *query_fn)(HANDLE,const void *,ULONG,void *,SIZE_T,SIZE_T *);
    query_fn query=(query_fn)(void *)GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"NtQueryVirtualMemory");
    ZeroMemory(out,sizeof *out);out->info.size=bytes;
    if(!query)return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
    LONG status=query(GetCurrentProcess(),address,0x5343010a,out,sizeof *out,NULL);
    if(status==(LONG)0xc0000003)return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
    return !status?S_OK:status<0?HRESULT_FROM_NT(status):E_FAIL;
}

static inline HRESULT scarlett_section_call(ULONG op, const void *address, struct scarlett_section_info *info)
{
    typedef LONG (WINAPI *query_fn)(HANDLE, const void *, ULONG, void *, SIZE_T, SIZE_T *);
    query_fn query = (query_fn)(void *)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryVirtualMemory");
    if (!query) return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
    LONG status = query(GetCurrentProcess(), address, op, info, sizeof(*info), NULL);
    if (!status) return S_OK;
    if (status == (LONG)0xc0000003) return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED); /* invalid class */
    return status < 0 ? HRESULT_FROM_NT(status) : E_FAIL;
}

static inline HRESULT scarlett_query_section_handle(ULONG op, const void *address, SIZE_T bytes,
                                             struct scarlett_section_info *info, HANDLE input)
{
    if (!info) return E_POINTER;
    ZeroMemory(info, sizeof(*info));
    info->size = bytes;
    info->section = input;
    return scarlett_section_call(op, address, info);
}

static inline HRESULT scarlett_query_section(ULONG op, const void *address, SIZE_T bytes,
                                             struct scarlett_section_info *info)
{
    return scarlett_query_section_handle(op,address,bytes,info,NULL);
}

/* On success the caller owns info->section. */
static inline HRESULT scarlett_acquire_section(const void *address, SIZE_T bytes,
                                               struct scarlett_section_info *info)
{
    return scarlett_query_section(0x53430002, address, bytes, info);
}
