#include <windows.h>
#include <stdio.h>
#include <string.h>

__declspec(dllimport) BOOL WINAPI XMemIsToolsMemAvailable(void);
__declspec(dllimport) PVOID WINAPI XMemVirtualAlloc(PVOID, SIZE_T, DWORD, ULONGLONG, DWORD);

#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "line %d: %s failed (last error %lu)\n", __LINE__, #expr, GetLastError()); \
    return 1; \
} } while (0)

int main(int argc, char **argv)
{
    const ULONGLONG cpu = 1;
    MEMORY_BASIC_INFORMATION info;
    BYTE *base;

    CHECK(!XMemIsToolsMemAvailable());
    base = XMemVirtualAlloc(NULL, 8192, MEM_RESERVE, cpu, PAGE_READWRITE);
    CHECK(base != NULL);
    CHECK(VirtualQuery(base, &info, sizeof(info)) == sizeof(info) && info.State == MEM_RESERVE);
    CHECK(XMemVirtualAlloc(base + 4096, 4096, MEM_COMMIT, cpu, PAGE_READWRITE) == base + 4096);
    CHECK(base[8191] == 0);
    base[8191] = 0x5a;
    CHECK(VirtualQuery(base + 4096, &info, sizeof(info)) == sizeof(info));
    CHECK(info.State == MEM_COMMIT && info.Protect == PAGE_READWRITE && info.AllocationBase == base);
    CHECK(base[8191] == 0x5a);
    CHECK(VirtualFree(base, 0, MEM_RELEASE));

    base = XMemVirtualAlloc(NULL, 1, MEM_RESERVE | MEM_COMMIT, cpu, PAGE_READONLY);
    CHECK(base != NULL && *base == 0);
    CHECK(VirtualQuery(base, &info, sizeof(info)) == sizeof(info) && info.Protect == PAGE_READONLY);
    CHECK(VirtualFree(base, 0, MEM_RELEASE));

    const SIZE_T pool = 0x1400000, page64 = 65536;
    base = XMemVirtualAlloc(NULL, pool, 0x20403000, 0x181, PAGE_READWRITE);
    CHECK(base && !((UINT_PTR)base % page64));
    CHECK(VirtualQuery(base, &info, sizeof(info)) == sizeof(info));
    CHECK(info.State == MEM_COMMIT && info.Type == MEM_PRIVATE && info.Protect == PAGE_READWRITE &&
          info.AllocationBase == base && info.RegionSize == pool);
    CHECK(!base[0] && !base[pool - 1]);
    base[0] = 1;
    base[pool - 1] = 2;
    CHECK(!XMemVirtualAlloc(base, pool, 0x20403000, 0x181, PAGE_READWRITE));
    CHECK(GetLastError() == ERROR_INVALID_ADDRESS && base[0] == 1 && base[pool - 1] == 2);
    CHECK(VirtualFree(base, 0, MEM_RELEASE));
    CHECK(XMemVirtualAlloc(base, page64, 0x20403000, 0x181, PAGE_READWRITE) == base);
    CHECK(!*base && VirtualFree(base, 0, MEM_RELEASE));
    CHECK(!XMemVirtualAlloc(base + 1, page64, 0x20403000, 0x181, PAGE_READWRITE));
    CHECK(GetLastError() == ERROR_INVALID_PARAMETER);
    CHECK(!XMemVirtualAlloc(NULL, page64 - 1, 0x20403000, 0x181, PAGE_READWRITE));
    CHECK(GetLastError() == ERROR_INVALID_PARAMETER);
    CHECK(!XMemVirtualAlloc(NULL, pool, 0x20403000, 0x183, PAGE_READWRITE));
    CHECK(GetLastError() == ERROR_NOT_SUPPORTED);
    CHECK(!XMemVirtualAlloc(NULL, pool, 0x20403000, 0x181, PAGE_READONLY));
    CHECK(GetLastError() == ERROR_NOT_SUPPORTED);

    /* A high tag bit must not disappear through a 32-bit flags parameter. */
    CHECK(!XMemVirtualAlloc(NULL, 4096, MEM_COMMIT, cpu | (1ULL << 63), PAGE_READWRITE));
    CHECK(GetLastError() == ERROR_NOT_SUPPORTED);
    SetLastError(ERROR_SUCCESS);
    CHECK(!XMemVirtualAlloc(NULL, 4096, MEM_RESERVE | MEM_LARGE_PAGES, cpu, PAGE_READWRITE));
    CHECK(GetLastError() == ERROR_NOT_SUPPORTED);
    SetLastError(ERROR_SUCCESS);
    CHECK(!XMemVirtualAlloc(NULL, 4096, MEM_COMMIT, cpu, PAGE_EXECUTE_READWRITE));
    CHECK(GetLastError() == ERROR_NOT_SUPPORTED);
    SetLastError(ERROR_SUCCESS);
    CHECK(!XMemVirtualAlloc(NULL, 0x49400000, 0x20003000, 0x283, 0x2404));
    CHECK(GetLastError() == ERROR_NOT_SUPPORTED);
    /* A host-WC heap does not satisfy PAGE_GRAPHICS_NOCACHE on the qualified RTX 3070. */
    SetLastError(ERROR_SUCCESS);
    CHECK(!XMemVirtualAlloc(NULL, 65536, 0x20401000, 0x283, 0x42404));
    CHECK(GetLastError() == ERROR_NOT_SUPPORTED);
    CHECK(!GetProcAddress(GetModuleHandleA("xmem.dll"), "XMemTransferMemory"));
    if (argc == 2 && !strcmp(argv[1], "--no-graphics")) {
        /* Run on stock Wine or with the graphics backend disabled. */
        const SIZE_T align = 2u << 20;
        base = VirtualAlloc(NULL, 2 * align, MEM_RESERVE, PAGE_NOACCESS);
        CHECK(base);
        void *at = (void *)(((UINT_PTR)base + align - 1) & ~(align - 1));
        CHECK(VirtualFree(base, 0, MEM_RELEASE));
        SetLastError(ERROR_SUCCESS);
        CHECK(!XMemVirtualAlloc(at, align, 0x20003000, 0x183, 0x22004));
        CHECK(GetLastError() != ERROR_SUCCESS);
        CHECK(VirtualQuery(at, &info, sizeof(info)) && info.State == MEM_FREE);
    }
    puts("xmem CPU page allocation tests passed");
    return 0;
}
