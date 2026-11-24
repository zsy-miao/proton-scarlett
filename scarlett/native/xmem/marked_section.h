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

static inline HRESULT scarlett_query_section(ULONG op, const void *address, SIZE_T bytes,
                                             struct scarlett_section_info *info)
{
    typedef LONG (WINAPI *query_fn)(HANDLE, const void *, ULONG, void *, SIZE_T, SIZE_T *);
    query_fn query = (query_fn)(void *)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryVirtualMemory");
    if (!info) return E_POINTER;
    ZeroMemory(info, sizeof(*info));
    info->size = bytes;
    if (!query) return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
    LONG status = query(GetCurrentProcess(), address, op, info, sizeof(*info), NULL);
    if (!status) return S_OK;
    if (status == (LONG)0xc0000003) return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED); /* invalid class */
    return status < 0 ? HRESULT_FROM_NT(status) : E_FAIL;
}

/* On success the caller owns info->section. */
static inline HRESULT scarlett_acquire_section(const void *address, SIZE_T bytes,
                                               struct scarlett_section_info *info)
{
    return scarlett_query_section(0x53430002, address, bytes, info);
}
