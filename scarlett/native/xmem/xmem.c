#include <windows.h>

#if !defined(__x86_64__) || !defined(_WIN64)
#error This DLL implements only the Windows x64 ABI.
#endif

#define XMEM_CPU 0x00000001ULL

__declspec(dllexport) BOOL WINAPI XMemIsToolsMemAvailable(void)
{
    return FALSE;
}

__declspec(dllexport) PVOID WINAPI XMemVirtualAlloc(PVOID base, SIZE_T size,
        DWORD type, ULONGLONG flags, DWORD protect)
{
    if (flags != XMEM_CPU ||
        (type != MEM_RESERVE && type != MEM_COMMIT && type != (MEM_RESERVE | MEM_COMMIT)) ||
        (protect != PAGE_READONLY && protect != PAGE_READWRITE)) {
        SetLastError(ERROR_NOT_SUPPORTED);
        return NULL;
    }
    return VirtualAlloc(base, size, type, protect);
}
