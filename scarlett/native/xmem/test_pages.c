#include <windows.h>
#include <stdio.h>

__declspec(dllimport) PVOID WINAPI XMemVirtualAlloc(PVOID, SIZE_T, DWORD, ULONGLONG, DWORD);
__declspec(dllimport) BOOL WINAPI XMemAllocatePhysicalPages(ULONGLONG, PULONG_PTR, PULONG_PTR);
__declspec(dllimport) BOOL WINAPI XMemFreePhysicalPages(ULONG_PTR, PULONG_PTR);
__declspec(dllimport) PVOID WINAPI XMemMapPhysicalPages(PVOID, ULONG_PTR, PULONG_PTR);

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s (error %lu)\n", __LINE__, #x, GetLastError()); return 1; } } while (0)
#define PAGE64 65536
#define ARENA(n) XMemVirtualAlloc(NULL, n, 0x20402000, 0x21, PAGE_READWRITE)
#define DESCRIPTOR(n) XMemVirtualAlloc(NULL, n, 0x42000, 7, PAGE_NOACCESS)
#define REPLACE(p,n,prot) XMemVirtualAlloc(p, n, 0x20406000, 0x27, prot)

static int descriptor_arena(void)
{
    const DWORD prot[] = {0x1004,0x1404,0x8404,0x2404,0x2004};
    const SIZE_T len[] = {2*PAGE64,PAGE64,3*PAGE64,2*PAGE64,PAGE64};
    const SIZE_T total = 9*PAGE64;
    const DWORD split = MEM_RELEASE | MEM_PRESERVE_PLACEHOLDER;
    MEMORY_BASIC_INFORMATION m;
    ULONG_PTR n = 1, id;
    BYTE byte;
    SIZE_T copied;
    BYTE *p = DESCRIPTOR(total);
    CHECK(p && !((UINT_PTR)p % PAGE64));
    CHECK(!DESCRIPTOR(0) && !DESCRIPTOR(PAGE64+1));
    CHECK(!XMemVirtualAlloc(p,total,0x42000,7,PAGE_NOACCESS));
    CHECK(!XMemVirtualAlloc(NULL,total,0x42000,3,PAGE_NOACCESS));
    CHECK(!XMemVirtualAlloc(NULL,total,0x42000,7,PAGE_READWRITE));
    CHECK(!REPLACE(NULL,total,prot[0]) && !REPLACE(p+1,total,prot[0]));
    CHECK(!REPLACE(p,total-1,prot[0]) && !REPLACE(p,total,0x4004));
    CHECK(!REPLACE(p,total+PAGE64,prot[0]) && !REPLACE(p+PAGE64,total-PAGE64,prot[0]));
    CHECK(!XMemVirtualAlloc(p,total,0x20407000,0x27,prot[0]));
    CHECK(!XMemVirtualAlloc(p,total,0x20406000,0x100000027ULL,prot[0]));
    CHECK(VirtualQuery(p,&m,sizeof m) && m.AllocationBase == p && m.RegionSize == total && m.State == MEM_RESERVE);
    CHECK(XMemAllocatePhysicalPages(0,&n,&id) && n == 1);
    CHECK(!XMemMapPhysicalPages(p,1,&id));
    BYTE *at = p;
    for (unsigned i=0; i+1 < ARRAYSIZE(len); ++i) {
        CHECK(VirtualFree(at,len[i],split));
        at += len[i];
    }
    CHECK(!REPLACE(p,len[0]+len[1],prot[0])); /* must not merge neighbors */
    at = p;
    for (unsigned i=0; i < ARRAYSIZE(len); ++i) {
        CHECK(REPLACE(at,len[i],prot[i]) == at && !REPLACE(at,len[i],prot[i]));
        CHECK(VirtualQuery(at,&m,sizeof m) && m.AllocationBase == at && m.RegionSize == len[i] &&
              m.State == MEM_RESERVE && m.AllocationProtect == PAGE_NOACCESS);
        CHECK(!ReadProcessMemory(GetCurrentProcess(),at+len[i]-1,&byte,1,&copied));
        CHECK(!XMemMapPhysicalPages(at,1,&id));
        CHECK(!XMemVirtualAlloc(at,PAGE64,MEM_COMMIT,1,PAGE_READWRITE));
        at += len[i];
    }
    CHECK(!VirtualFree(p,total,MEM_RELEASE | MEM_COALESCE_PLACEHOLDERS));
    at = p;
    for (unsigned i=0; i < ARRAYSIZE(len); ++i) {
        CHECK(VirtualFree(at,len[i],split)); /* undo each exact replacement */
        at += len[i];
    }
    CHECK(VirtualFree(p,total,MEM_RELEASE | MEM_COALESCE_PLACEHOLDERS));
    CHECK(VirtualFree(p,0,MEM_RELEASE));
    CHECK(VirtualQuery(p,&m,sizeof m) && m.State == MEM_FREE && !REPLACE(p,total,prot[0]));
    CHECK(VirtualAlloc(p,total,MEM_RESERVE | MEM_COMMIT,PAGE_READWRITE) == p);
    p[total-1]=91;
    CHECK(!REPLACE(p,total,prot[0]) && p[total-1] == 91);
    CHECK(VirtualFree(p,0,MEM_RELEASE));
    CHECK(VirtualAlloc(p,total,MEM_RESERVE,PAGE_NOACCESS) == p);
    CHECK(!REPLACE(p,total,prot[0]) && VirtualFree(p,0,MEM_RELEASE));
    CHECK(XMemVirtualAlloc(p,total,0x20402000,0x21,PAGE_READWRITE) == p);
    CHECK(!REPLACE(p,total,prot[0]) && XMemMapPhysicalPages(p,1,&id) == p);
    CHECK(XMemMapPhysicalPages(p,1,NULL) == p && VirtualFree(p,0,MEM_RELEASE));
    CHECK(XMemFreePhysicalPages(1,&id));
    p = DESCRIPTOR(PAGE64);
    CHECK(p && REPLACE(p,PAGE64,prot[0]) == p && VirtualFree(p,0,MEM_RELEASE));
    CHECK(VirtualQuery(p,&m,sizeof m) && m.State == MEM_FREE);
    puts("PASS descriptor VA: four native splits, five exact reserve-only replacements; boundaries, collisions, restore/release and map refusal");
    return 0;
}

int main(void)
{
    CHECK(!descriptor_arena());
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
