#include <windows.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <stdio.h>

__declspec(dllimport) HRESULT WINAPI AcpHalAllocateShapeContexts(void *);
__declspec(dllimport) HRESULT WINAPI AcpHalCreate(void **);
__declspec(dllimport) HRESULT WINAPI AcpHalReleaseShapeContexts(void);
__declspec(dllimport) HRESULT WINAPI ApuCreateHeap(uint32_t, uint32_t);
__declspec(dllimport) HRESULT WINAPI ApuAlloc(void **, uint32_t *, uint32_t, uint32_t, uint32_t);
__declspec(dllimport) void WINAPI ApuFree(void *);
__declspec(dllimport) bool WINAPI ApuIsVirtualAddressValid(const void *, uint32_t);
__declspec(dllimport) void *WINAPI ApuMapApuAddress(uint32_t);
__declspec(dllimport) uint32_t WINAPI ApuMapVirtualAddress(const void *);

int main(void)
{
    HRESULT unsupported = HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
    struct { uint32_t address, guard; } physical = {1, UINT32_MAX};
    void *out = &physical;
    assert(AcpHalAllocateShapeContexts(NULL) == E_INVALIDARG);
    assert(AcpHalAllocateShapeContexts(&physical) == unsupported && physical.address == 1);
    assert(AcpHalCreate(&out) == unsupported && !out);
    assert(AcpHalReleaseShapeContexts() == unsupported);
    assert(ApuCreateHeap(0, 0) == E_INVALIDARG);
    assert(ApuCreateHeap(1 << 20, 0) == unsupported);
    out = &physical;
    assert(ApuAlloc(&out, &physical.address, 2048, 2048, 0) == unsupported);
    assert(!out && !physical.address && physical.guard == UINT32_MAX);
    assert(ApuAlloc(&out, NULL, 32, 6, 0) == E_INVALIDARG);
    assert(ApuAlloc(&out, NULL, 32, 4, 0) == unsupported && !out);
    assert(!ApuMapApuAddress(UINT32_MAX));
    assert(!ApuMapVirtualAddress(&physical));
    assert(!ApuIsVirtualAddressValid(&physical, 4));
    out = HeapAlloc(GetProcessHeap(), 0, 16);
    assert(out);
    ApuFree(NULL);
    ApuFree(out);
    assert(GetLastError() == ERROR_INVALID_ADDRESS);
    assert(HeapFree(GetProcessHeap(), 0, out));
    puts("acphal: no device allocation or mapping exposed; foreign heap block preserved");
    return 0;
}
