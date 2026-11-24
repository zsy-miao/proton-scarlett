#include <windows.h>
#include <stdbool.h>
#include <stdint.h>

#if !defined(__x86_64__) || !defined(_WIN64)
#error Windows x64 ABI required
#endif

typedef uint32_t APU_ADDRESS;
typedef struct IAcpHal IAcpHal;
typedef struct SHAPE_CONTEXTS SHAPE_CONTEXTS;
#define NO_APU HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED)

__declspec(dllexport) HRESULT WINAPI AcpHalAllocateShapeContexts(SHAPE_CONTEXTS *contexts)
{
    return contexts ? NO_APU : E_INVALIDARG;
}

__declspec(dllexport) HRESULT WINAPI AcpHalCreate(IAcpHal **acp)
{
    if (!acp) return E_INVALIDARG;
    *acp = NULL;
    return NO_APU;
}

__declspec(dllexport) HRESULT WINAPI AcpHalReleaseShapeContexts(void)
{
    return NO_APU;
}

__declspec(dllexport) HRESULT WINAPI ApuCreateHeap(uint32_t cached, uint32_t noncached)
{
    (void)noncached;
    return cached ? NO_APU : E_INVALIDARG;
}

__declspec(dllexport) HRESULT WINAPI ApuAlloc(void **address, APU_ADDRESS *physical,
        uint32_t size, uint32_t alignment, uint32_t flags)
{
    (void)flags;
    if (address) *address = NULL;
    if (physical) *physical = 0;
    if (!address || !size || alignment < 4 || (alignment & (alignment - 1)))
        return E_INVALIDARG;
    /* An omitted physical out-parameter still requires a device-mappable allocation. */
    return NO_APU;
}

__declspec(dllexport) void WINAPI ApuFree(void *address)
{
    /* No APU allocations exist. A foreign allocation is never ours to free. */
    if (address) SetLastError(ERROR_INVALID_ADDRESS);
}

__declspec(dllexport) bool WINAPI ApuIsVirtualAddressValid(const void *address, uint32_t alignment)
{
    (void)address;
    (void)alignment;
    return false;
}

__declspec(dllexport) void *WINAPI ApuMapApuAddress(APU_ADDRESS physical)
{
    (void)physical;
    return NULL;
}

__declspec(dllexport) APU_ADDRESS WINAPI ApuMapVirtualAddress(const void *address)
{
    (void)address;
    return 0;
}
