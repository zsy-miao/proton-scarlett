#include <windows.h>
#include <stdio.h>

__declspec(dllimport) PVOID WINAPI XMemVirtualAlloc(PVOID, SIZE_T, DWORD, ULONGLONG, DWORD);
__declspec(dllimport) BOOL WINAPI XMemAllocatePhysicalPages(ULONGLONG, PULONG_PTR, PULONG_PTR);
__declspec(dllimport) BOOL WINAPI XMemFreePhysicalPages(ULONG_PTR, PULONG_PTR);
__declspec(dllimport) PVOID WINAPI XMemMapPhysicalPages(PVOID, ULONG_PTR, PULONG_PTR);

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s (error %lu)\n", __LINE__, #x, GetLastError()); return 1; } } while (0)
#define PAGE64 65536
#define ARENA(n) XMemVirtualAlloc(NULL, n, 0x20402000, 0x21, PAGE_READWRITE)

int main(void)
{
    MEMORY_BASIC_INFORMATION m;
    for (SIZE_T gb = 16; gb <= 24; gb += 8) {
        SIZE_T size = gb << 30;
        BYTE *cpu = ARENA(size);
        BYTE *gpu = XMemVirtualAlloc(NULL, size, 0x20402000, 3, 0x801);
        CHECK(cpu && gpu && cpu != gpu && !(((UINT_PTR)cpu | (UINT_PTR)gpu) % PAGE64));
        CHECK(VirtualQuery(cpu, &m, sizeof m) && m.State == MEM_RESERVE && m.RegionSize == size);
        CHECK(VirtualQuery(gpu, &m, sizeof m) && m.State == MEM_RESERVE && m.RegionSize == size);
        CHECK(!XMemVirtualAlloc(gpu, PAGE64, 0x20401000, 3, 0x801));
        CHECK(!XMemVirtualAlloc(gpu, PAGE64, MEM_COMMIT, 1, PAGE_READWRITE));
        CHECK(VirtualFree(gpu, 0, MEM_RELEASE) && VirtualFree(cpu, 0, MEM_RELEASE));
        CHECK(VirtualQuery(cpu, &m, sizeof m) && m.State == MEM_FREE);
    }
    ULONG_PTR pages[2], n = 2;
    CHECK(!XMemAllocatePhysicalPages(0x281, &n, pages) && !n && GetLastError() == ERROR_NOT_SUPPORTED);
    n = 2;
    CHECK(XMemAllocatePhysicalPages(0, &n, pages) && n == 2);
    BYTE *a = ARENA(2 * PAGE64), *b = ARENA(PAGE64);
    BYTE *gpu = XMemVirtualAlloc(NULL, PAGE64, 0x20402000, 3, 0x801);
    CHECK(a && b && gpu);
    CHECK(!XMemMapPhysicalPages(gpu, 1, pages));
    CHECK(!XMemMapPhysicalPages(a + 1, 1, pages));
    BYTE *large = ARENA(16ULL << 30);
    BYTE *offset = large ? large + (9ULL << 30) : NULL;
    CHECK(large && XMemMapPhysicalPages(offset, 1, pages + 1) == offset);
    CHECK(XMemMapPhysicalPages(large, 1, pages + 1) == large);
    large[19] = 77;
    CHECK(offset[19] == 77);
    CHECK(XMemMapPhysicalPages(offset, 1, NULL) == offset);
    CHECK(XMemMapPhysicalPages(large, 1, NULL) == large);
    CHECK(VirtualQuery(large, &m, sizeof m) && m.RegionSize == (16ULL << 30));
    CHECK(VirtualFree(large, 0, MEM_RELEASE));
    CHECK(XMemMapPhysicalPages(a, 2, pages) == a);
    CHECK(XMemMapPhysicalPages(b, 1, pages + 1) == b);
    a[2 * PAGE64 - 1] = 91;
    CHECK(b[PAGE64 - 1] == 91);
    CHECK(!XMemFreePhysicalPages(2, pages) && GetLastError() == ERROR_BUSY);
    CHECK(XMemMapPhysicalPages(a, 2, NULL) == a);
    CHECK(VirtualQuery(a, &m, sizeof m) && m.State == MEM_RESERVE);
    CHECK(XMemMapPhysicalPages(b, 1, NULL) == b);
    CHECK(XMemMapPhysicalPages(b, 1, pages + 1) == b && b[PAGE64 - 1] == 91);
    CHECK(XMemMapPhysicalPages(b, 1, NULL) == b);
    CHECK(XMemFreePhysicalPages(2, pages));
    CHECK(!XMemMapPhysicalPages(a, 2, pages) && !XMemFreePhysicalPages(2, pages));
    CHECK(VirtualFree(a, 0, MEM_RELEASE) && VirtualFree(b, 0, MEM_RELEASE));
    CHECK(VirtualFree(gpu, 0, MEM_RELEASE));
    puts("16/24 GiB reservation pairs and CPU section alias lifetimes passed");
    return 0;
}
